#pragma once

#include "ports/exportacion/DestinoExportacion.h"

#include <QDateTime>
#include <QString>

#include <functional>
#include <memory>

namespace satcfdi {

// T026.1 (D12, D20): DestinoExportacion sobre POSIX/macOS. Reutiliza el
// diseno del destino del respaldo (T025.3, DestinoRespaldoPosix).
//
// Carpeta privada: <directorioDatos>/exportacion/parciales (0700 en los dos
// niveles propios, creados con mkdirat y abiertos con O_NOFOLLOW; fuera de lo
// que respalda T025 y de lo que sustituye la restauracion T025.5).
//
// reservar():
// - Abre la carpeta destino (O_DIRECTORY) y rechaza el nombre si ya existe
//   (fstatat sin seguir enlaces): DestinoExistente.
// - Si la carpeta privada esta en el mismo volumen (st_dev) que el destino,
//   el `.partial` va ahi; si no, en la carpeta destino y ANTES de crearlo se
//   registra (carpeta, st_dev/st_ino y nombre del `.partial`) en
//   <directorioDatos>/exportacion/parciales-destinos.json (0600, QSaveFile:
//   temporal + fsync + rename atomico).
// - `.partial`: `.archivo-fiscal-exportacion-<16 hex>.partial` (arc4random),
//   openat(O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC, 0600) + fchmod 0600.
//
// publicar(): F_FULLFSYNC (fsync si no se admite), retira los atributos
// extendidos (flistxattr/fremovexattr), fchmod 0600, close, renameatx_np(
// RENAME_EXCL) de la carpeta del `.partial` a la destino y fsync de la carpeta
// destino y, si es distinta, de la privada. Solo una falla de esos fsync de
// carpeta da exito con advertencia. Al publicar o descartar un `.partial` de
// otro volumen se retira del registro.
//
// limpiarParcialesPropios(): en la carpeta privada borra los archivos
// regulares con el patron exacto (openat O_NOFOLLOW + fstat + misma identidad
// que fstatat antes de unlinkat); en cada carpeta registrada, solo si su
// st_dev/st_ino coincide con lo registrado, y solo los nombres registrados.
// Nunca toca un `.partial` reservado y vivo en este proceso.
//
// Revision de seguridad T026.1:
// - Un `.partial` de otro volumen solo sale del registro si unlinkat da 0 o
//   ENOENT (al descartar y en la limpieza); si no, se reintenta al arrancar.
// - Un registro ilegible se aparta a parciales-destinos.ilegible-<n>.json
//   (0600, sin reemplazo) y se empieza vacio; la limpieza ignora los apartados.
// - El nombre del `.partial` queda activo antes de registrarlo o crearlo.
// - Entradas de carpetas no verificables (no disponibles u otra identidad)
//   caducan a los 90 dias del ultimo registro (Opciones::reloj).
// - publicar() aplica una ACL extendida vacia (acl_set_fd_np, best effort).
// - reservar() rechaza con CarpetaNoPermitida una carpeta destino que, por
//   identidad (subiendo con ".."), sea el directorio de datos o este dentro.
//
// Logs (categoria satcfdi.exportacion.destino): fase, codigo y errno;
// conteos en la limpieza. Nunca rutas ni nombres.
class DestinoExportacionPosix final : public exportacion::DestinoExportacion {
public:
    // Fases con costura de fallas para pruebas.
    enum class Fase {
        CrearCarpetaPrivada,
        Registrar, // escribir el registro de parciales de otro volumen
        Crear,     // openat del `.partial`
        Escribir,  // cada pwrite del dispositivo (ENOSPC = disco lleno)
        Truncar,
        Sincronizar, // F_FULLFSYNC del `.partial`
        Cerrar,
        Renombrar,
        SincronizarCarpetaDestino, // tras el rename
        SincronizarCarpetaPrivada, // tras el rename (solo si el .partial estaba ahi)
        Borrar,                    // unlinkat de un .partial (descartar o limpieza)
    };

    struct Opciones {
        // Directorio de datos de la app (absoluto).
        QString directorioDatos;
        // Costura de prueba: se llama justo antes de la operacion real de la
        // fase; un valor distinto de 0 es el errno con el que la fase falla
        // sin ejecutarse. Puede tener efectos (p. ej. crear el nombre final
        // antes del rename). Vacia = sistema real.
        std::function<int(Fase)> inyectarFalla;
        // Prueba: trata la carpeta privada como de otro volumen.
        bool simularOtroVolumen = false;
        // Hora UTC para la caducidad del registro (vacio = sistema).
        std::function<QDateTime()> reloj;
    };

    explicit DestinoExportacionPosix(Opciones opciones);
    ~DestinoExportacionPosix() override;

    DestinoExportacionPosix(const DestinoExportacionPosix&) = delete;
    DestinoExportacionPosix& operator=(const DestinoExportacionPosix&) = delete;

    static QString carpetaParciales(const QString& directorioDatos);
    static QString rutaRegistro(const QString& directorioDatos);
    static QString claveFase(Fase fase);
    // errno -> codigo (EACCES/EPERM/EROFS, ENOSPC/EDQUOT, volumen no disponible).
    static exportacion::ErrorDestinoExportacion::Codigo codigoDeErrno(
        int codigo, exportacion::ErrorDestinoExportacion::Codigo porDefecto);

    Resultado<std::unique_ptr<exportacion::ArchivoParcialExportacion>, exportacion::ErrorDestinoExportacion>
    reservar(const QString& carpetaDestino, const QString& nombreFinal) override;

    Resultado<exportacion::ResumenLimpiezaExportacion, exportacion::ErrorDestinoExportacion>
    limpiarParcialesPropios() override;

    struct Compartido;

private:
    std::shared_ptr<Compartido> m_compartido;
};

} // namespace satcfdi
