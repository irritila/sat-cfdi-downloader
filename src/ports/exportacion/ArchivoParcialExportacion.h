#pragma once

// T026.1 (D12): `.partial` propio reservado para un ZIP de exportacion.
//
// Ciclo: DestinoExportacion::reservar() -> escribir por dispositivo() (con
// seek / escritura posicional) -> publicar() -> fin. Cualquier salida sin
// publicar (descartar(), destruccion del objeto o falla de publicar() distinta
// de DestinoExistente) borra SOLO este `.partial`; nunca toca otro archivo.
// Una terminacion forzada del proceso deja el `.partial`, que borra
// DestinoExportacion::limpiarParcialesPropios() en el siguiente arranque.
//
// Hilo: el de quien lo reservo (no thread-safe).

#include "domain/common/Resultado.h"
#include "ports/exportacion/ErrorDestinoExportacion.h"
#include "ports/persistence/Exito.h"

#include <QString>
#include <QtGlobal>

#include <optional>

QT_BEGIN_NAMESPACE
class QIODevice;
QT_END_NAMESPACE

namespace satcfdi::exportacion {

// Resultado de una publicacion exitosa. Con `advertenciaDurabilidad` el ZIP
// YA esta publicado con su nombre final (el rename ocurrio), pero el fsync de
// alguna carpeta posterior al rename fallo: un corte de energia inmediato
// podria perder la entrada de directorio. No se despublica; se informa.
struct PublicacionExportacion {
    quint64 tamanoBytes = 0;
    bool advertenciaDurabilidad = false;
    int errnoDurabilidad = 0; // errno del fsync de carpeta que fallo (0 si ninguno)

    bool operator==(const PublicacionExportacion&) const = default;
};

class ArchivoParcialExportacion {
public:
    virtual ~ArchivoParcialExportacion() = default;

    // Dispositivo de acceso aleatorio (QIODevice::ReadWrite | Unbuffered, no
    // secuencial) sobre el descriptor reservado, en la posicion 0: seek()
    // fija la posicion y write()/read() usan pwrite/pread en ella, de modo
    // que un escritor ZIP puede volver a escribir cabeceras. size() es el
    // tamano actual del archivo. Valido mientras viva el objeto y hasta
    // publicar o descartar.
    virtual QIODevice& dispositivo() = 0;

    // Primera falla de escritura del dispositivo (codigo + errno), si hubo.
    // Tras una falla el archivo no debe publicarse.
    virtual std::optional<ErrorDestinoExportacion> errorEscritura() const = 0;

    // ftruncate del `.partial` (p. ej. al reiniciar una escritura).
    virtual Resultado<Exito, ErrorDestinoExportacion> truncar(qint64 tamano) = 0;

    // F_FULLFSYNC del contenido (fsync si el volumen no admite F_FULLFSYNC).
    // publicar() lo hace de nuevo si hubo escrituras despues.
    virtual Resultado<Exito, ErrorDestinoExportacion> sincronizar() = 0;

    // Publica con el nombre final reservado: sincroniza, deja el archivo en
    // 0600 sin atributos extendidos, lo cierra y lo renombra en la carpeta
    // destino con RENAME_EXCL (nunca sobrescribe, tampoco si el nombre
    // aparecio despues de reservar). Despues sincroniza las carpetas.
    // - DestinoExistente: el `.partial` sigue intacto y se puede reintentar
    //   con publicarComo() y otro nombre.
    // - Cualquier otra falla: el `.partial` ya se borro (estado terminal).
    // - Exito: con o sin advertenciaDurabilidad.
    virtual Resultado<PublicacionExportacion, ErrorDestinoExportacion> publicar() = 0;

    // Igual que publicar() con otro nombre final (ya saneado) en la misma
    // carpeta destino; NombreInvalido no consume el `.partial`.
    virtual Resultado<PublicacionExportacion, ErrorDestinoExportacion>
    publicarComo(const QString& nombreFinal) = 0;

    // Cierra y borra el `.partial` propio. Idempotente; no hace nada tras
    // publicar.
    virtual void descartar() noexcept = 0;

    // true si el `.partial` vive en la carpeta destino (otro volumen que los
    // datos de la app); false si en la carpeta privada. Para diagnostico.
    virtual bool enCarpetaDestino() const = 0;
};

} // namespace satcfdi::exportacion
