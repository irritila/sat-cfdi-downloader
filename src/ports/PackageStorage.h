#pragma once

#include "domain/common/Resultado.h"
#include "ports/persistence/Exito.h"
#include "ports/storage/RutaPaquete.h"
#include "ports/storage/TiposAlmacenamiento.h"

#include <QList>
#include <QString>

namespace satcfdi {

// Puerto de almacenamiento local de paquetes ZIP (T008, ADR 0004, ADR 0014).
// Frontera de FILESYSTEM: devuelve hechos y nunca cambia estados de
// PaqueteSolicitud ni escribe LogSolicitud (D4); eso lo hace T007.
//
// Hilo: el del OperacionExecutor (E/S bloqueante); nunca el hilo grafico.
// Rutas: siempre RELATIVAS a la raiz inyectada en el adaptador (D8); la raiz
// la resuelve el composition root.
// Garantias: ninguna operacion sobrescribe ni borra un archivo final; no hay
// operacion para borrar finales. Ningun error ni diagnostico lleva bytes ZIP.
// Una entrada invalida devuelve EntradaInvalida sin tocar el filesystem.
class PackageStorage {
public:
    virtual ~PackageStorage() = default;

    // Ruta relativa que tendra el paquete (pura y determinista, D5). Igual a
    // rutapaquete::derivarRutaRelativa.
    static Resultado<QString, ErrorAlmacenamiento> derivarRutaRelativa(const UbicacionPaquete& ubicacion)
    {
        return rutapaquete::derivarRutaRelativa(ubicacion);
    }

    // Escribe la fuente por chunks en un temporal exclusivo (0600) de la carpeta
    // final (directorios nuevos 0700), lo sincroniza y lo promueve SIN reemplazo
    // (D6). Errores: Fuente (la fuente fallo) o Almacenamiento: EntradaInvalida,
    // Permiso, SinEspacio, Escritura, Durabilidad (no promovido), Promocion,
    // ColisionDestino (el final previo no cambia) o Cancelada. Ante cualquier
    // error no queda temporal ni final nuevo. Exito con
    // advertenciaDurabilidad=true si fallo el fsync del directorio tras
    // promover (D7): el final existe.
    virtual Resultado<ArchivoFinal, ErrorGuardarZip> guardarAtomico(const UbicacionPaquete& ubicacion,
                                                                     FuenteZipPorChunks& fuente,
                                                                     const Cancelacion& cancelacion) = 0;

    // true si `rutaRelativa` (forma exacta de D5) es un archivo final regular;
    // false si no existe. EntradaInvalida (ruta no propia, '..', absoluta) o
    // LecturaRaiz (error de lectura distinto de "no existe"). Solo lectura.
    virtual Resultado<bool, ErrorAlmacenamiento> existeArchivoFinal(const QString& rutaRelativa) = 0;

    // Recorre la raiz y reporta SOLO estructura propia (D9): finales y
    // temporales dentro de <RFC>/<yyyy-mm>/<UUID canonico>/, ordenados por
    // rutaRelativa. Lo ajeno no se reporta ni se borra. Raiz inexistente ->
    // lista vacia. LecturaRaiz si la raiz o una carpeta propia no se puede leer.
    virtual Resultado<QList<HallazgoFilesystem>, ErrorAlmacenamiento> escanearRecuperacion() = 0;

    // Borra un temporal PROPIO reportado por escanearRecuperacion (D10).
    // Un hallazgo Final, una ruta que no cumple el patron de temporal o algo
    // que no es archivo regular -> EntradaInvalida sin borrar. Ya ausente ->
    // Exito (idempotente). Permiso/Escritura si unlink falla.
    virtual Resultado<Exito, ErrorAlmacenamiento> eliminarTemporal(const HallazgoFilesystem& temporal) = 0;
};

} // namespace satcfdi
