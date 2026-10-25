#pragma once

// T026.1 (D12, C2): escritor del ZIP de exportacion en streaming sobre un
// `.partial` reservado (ArchivoParcialExportacion).
//
// Ciclo: abrir() -> agregar()* -> cerrar() -> (el llamador) publicar().
// - agregar() escribe la entrada DE INMEDIATO (encabezado local, datos
//   comprimidos y descriptor de datos) y no retiene la vista, fuentes,
//   callbacks ni rutas: al volver con exito la entrada ya pertenece al
//   `.partial`. El llamador valida los bytes (UUID, hash) antes de llamar.
// - cerrar() solo escribe el directorio central y el fin de archivo; nunca
//   relee ni renderiza nada.
// - Cualquier error (incluida la cancelacion) deja el escritor terminal y el
//   `.partial` con un ZIP incompleto: el llamador debe descartarlo
//   (ArchivoParcialExportacion::descartar()); el escritor nunca publica ni
//   descarta por su cuenta. abortar() solo marca el escritor como terminal.
// - Cancelacion: `continuar` (opcional) se consulta antes de cada entrada,
//   entre trozos de compresion y periodicamente al escribir el directorio
//   central; si devuelve false la operacion en curso falla con Cancelado.
//   Se llama en el hilo del escritor; debe ser barato y thread-safe respecto
//   de quien cancela (p. ej. leer un std::atomic<bool>).
// - Errores tipados por codigo; nunca llevan nombres de entradas ni rutas.
//
// Hilo: el del llamador (no thread-safe); el mismo que usa el `.partial`.

#include "domain/common/Resultado.h"
#include "ports/exportacion/ArchivoParcialExportacion.h"
#include "ports/exportacion/ErrorDestinoExportacion.h"
#include "ports/persistence/Exito.h"

#include <QByteArrayView>
#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include <functional>
#include <memory>
#include <optional>

namespace satcfdi::exportacion {

struct ErrorZipExportacion {
    enum class Codigo {
        Estado,          // llamada fuera de orden o tras un error/abortar()
        NombreInvalido,  // vacio, fuera de [A-Za-z0-9._-] y '/', absoluto, '..', '.', componente vacio, `._*`, `__MACOSX`
        NombreDuplicado, // ya se agrego una entrada con ese nombre
        EntradaGrande,   // una sola entrada no cabe en el formato soportado
        Cancelado,       // `continuar` devolvio false
        Escritura,       // el dispositivo del `.partial` fallo (ver `destino`)
        Compresion,      // zlib fallo
        Interno,
    };

    Codigo codigo = Codigo::Interno;
    int errnoSistema = 0;
    // Con Escritura: el codigo del destino (p. ej. EspacioInsuficiente).
    std::optional<ErrorDestinoExportacion::Codigo> destino;

    static ErrorZipExportacion de(Codigo c, int errnoSistema = 0,
                                  std::optional<ErrorDestinoExportacion::Codigo> destino = std::nullopt)
    {
        ErrorZipExportacion e;
        e.codigo = c;
        e.errnoSistema = errnoSistema;
        e.destino = destino;
        return e;
    }

    bool operator==(const ErrorZipExportacion&) const = default;
};

inline QString claveEstable(ErrorZipExportacion::Codigo c)
{
    using C = ErrorZipExportacion::Codigo;
    switch (c) {
    case C::Estado: return QStringLiteral("estado");
    case C::NombreInvalido: return QStringLiteral("nombre_invalido");
    case C::NombreDuplicado: return QStringLiteral("nombre_duplicado");
    case C::EntradaGrande: return QStringLiteral("entrada_grande");
    case C::Cancelado: return QStringLiteral("cancelado");
    case C::Escritura: return QStringLiteral("escritura");
    case C::Compresion: return QStringLiteral("compresion");
    case C::Interno: return QStringLiteral("interno");
    }
    return QStringLiteral("interno");
}

struct ResumenZipExportacion {
    int entradas = 0;
    quint64 bytes = 0;  // tamano total del ZIP escrito
    bool zip64 = false; // se uso algun registro ZIP64

    bool operator==(const ResumenZipExportacion&) const = default;
};

class EscritorZipExportacion {
public:
    virtual ~EscritorZipExportacion() = default;

    // `parcial` debe estar vacio y en la posicion 0, y vivir hasta cerrar()
    // o abortar(). `fechaExportacionUtc` es la fecha de todas las entradas.
    virtual Resultado<Exito, ErrorZipExportacion> abrir(ArchivoParcialExportacion& parcial,
                                                        QDateTime fechaExportacionUtc,
                                                        std::function<bool()> continuar = {}) = 0;

    // `nombreEntrada`: ruta relativa de [A-Za-z0-9._-] con '/' (p. ej.
    // "emitidas/2026-01/<UUID>.xml"), ya asignada (D18).
    virtual Resultado<Exito, ErrorZipExportacion> agregar(const QString& nombreEntrada,
                                                          QByteArrayView bytesValidados) = 0;

    virtual Resultado<ResumenZipExportacion, ErrorZipExportacion> cerrar() = 0;

    virtual void abortar() noexcept = 0;
};

// Calidad T026.1: un escritor sirve para UN ZIP (queda terminal tras cerrar,
// cancelar o fallar). El servicio de exportacion (T026.2) recibe esta fabrica
// y crea uno nuevo por exportacion sin conocer la implementacion. Debe poder
// llamarse desde cualquier hilo; nunca devuelve nulo.
using FabricaEscritorZip = std::function<std::unique_ptr<EscritorZipExportacion>()>;

} // namespace satcfdi::exportacion
