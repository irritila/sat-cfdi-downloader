#pragma once

#include "ports/respaldo/DestinoRespaldo.h"

#include <QString>

#include <functional>
#include <memory>

namespace satcfdi {

// T025.3 (D8, D20, D21): DestinoRespaldo sobre POSIX/macOS.
//
// abrir() (una vez por respaldo, M2):
// - Componentes intermedios de la ruta: si alguno es un enlace del usuario
//   (st_uid != 0) -> EnlaceIntermedio; los del sistema (root: /var, /tmp,
//   /Volumes/Macintosh HD) se aceptan. El ultimo componente puede ser un
//   enlace (p. ej. ~/Dropbox; T025.4 lo resuelve desde su bookmark).
// - open(O_DIRECTORY) y fstat: directorio con st_uid == getuid() (si no,
//   NoEsCarpeta / DuenoDistinto).
// - Volumen (M3), por descriptor: F_GETPATH da la ruta real; si esta bajo
//   /Volumes/<n>, fstatfs debe estar montado en /Volumes/<n> (un punto de
//   montaje vacio en el disco de arranque = VolumenNoDisponible).
// - abrirParaLeer() (N1, D20): setiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_
//   DATALESS_FILES, IOPOL_SCOPE_THREAD, IOPOL_MATERIALIZE_DATALESS_FILES_OFF)
//   solo mientras vive el dispositivo devuelto; al destruirlo (o ante error) se
//   restaura la politica anterior del hilo (getiopolicy_np). La escritura del
//   respaldo corre con la politica normal.
//
// .partial (D21): en `carpetaParciales` (datos de la app, 0700) si esta en el
// mismo volumen (st_dev) que la carpeta; si no, dentro de la carpeta. Se crea
// con O_CREAT|O_EXCL|O_NOFOLLOW (0600) y se publica con
// renameatx_np(RENAME_EXCL) entre las dos carpetas.
//
// errno -> categoria (B1): EDEADLK -> SinDescargar ("carpeta.sin_descargar");
// ENOTCONN, ESTALE, ETIMEDOUT, ENXIO al abrir ->
// VolumenNoDisponible; ENOSPC/EDQUOT -> EspacioInsuficiente; EACCES/EPERM/EROFS
// -> SinEscritura; ENOTSUP/EINVAL en RENAME_EXCL -> ExclusivoNoSoportado.
//
// Hilo: el del RespaldoService (no thread-safe).
class DestinoRespaldoPosix final : public respaldo::DestinoRespaldo {
public:
    struct Opciones {
        // <AppData>/respaldo/parciales; vacio = .partial siempre en la carpeta.
        QString carpetaParciales;
        // Costuras de prueba (vacias = sistema real).
        std::function<int(int, const char*, int, const char*, unsigned)> renombrar;
        std::function<QString(int fd)> rutaDeDescriptor;
        std::function<void()> trasDesvincularPrueba;
        bool simularParcialesEnOtroVolumen = false;
    };

    explicit DestinoRespaldoPosix(QString carpetaAbsoluta);
    DestinoRespaldoPosix(QString carpetaAbsoluta, Opciones opciones);
    ~DestinoRespaldoPosix() override;

    const QString& carpeta() const noexcept { return m_carpeta; }

    // errno -> categoria (B1). Pura, para pruebas.
    static respaldo::ErrorDestino::Categoria categoriaDeErrno(int codigo, respaldo::ErrorDestino::Categoria porDefecto);

    Resultado<Exito, respaldo::ErrorDestino> abrir() override;
    QString identidad() const override;
    Resultado<Exito, respaldo::ErrorDestino> comprobarIdentidad() override;
    Resultado<respaldo::EstadoDestino, respaldo::ErrorDestino> validar(quint64 bytesNecesarios) override;
    Resultado<std::unique_ptr<respaldo::ArchivoParcial>, respaldo::ErrorDestino>
    crearParcial(const QString& propietario) override;
    Resultado<QList<respaldo::EntradaDestino>, respaldo::ErrorDestino> listar() override;
    Resultado<std::unique_ptr<QIODevice>, respaldo::ErrorDestino> abrirParaLeer(const QString& nombre) override;
    Resultado<Exito, respaldo::ErrorDestino> borrar(const QString& nombre) override;
    Resultado<int, respaldo::ErrorDestino> limpiarParciales(const QString& propietario) override;

    struct Estado;

private:
    QString m_carpeta;
    std::unique_ptr<Estado> m_estado;
};

// T025.3 (D20): registro local de lo publicado en un JSON escrito con QSaveFile
// (temporal + rename atomico; 0600, carpeta 0700 creada al primer guardado).
// JSON porque es pequeno (decenas de entradas), legible para diagnostico,
// versionado ("formato": 1) y de Qt Core; los enteros de 64 bits van como texto
// para no perder precision.
class RegistroPublicadosJson final : public respaldo::RegistroPublicados {
public:
    explicit RegistroPublicadosJson(QString ruta) : m_ruta(std::move(ruta)) {}
    Resultado<respaldo::EstadoRegistro, respaldo::ErrorDestino> cargar() override;
    Resultado<Exito, respaldo::ErrorDestino> guardar(const respaldo::EstadoRegistro& estado) override;

private:
    QString m_ruta;
};

} // namespace satcfdi
