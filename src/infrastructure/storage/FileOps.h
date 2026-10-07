#pragma once

#include <QByteArray>
#include <QList>

#include <cstddef>

namespace satcfdi {

// Costura de E/S de FilesystemPackageStorage (T008 D6). Cada operacion
// devuelve 0 si tuvo exito o un errno (POSIX) si fallo, para que las pruebas
// programen fallos por operacion y por llamada sin tocar el disco real
// (ENOSPC, fsync, promocion, fsync del directorio).
//
// Rutas: nativas y absolutas (QFile::encodeName). Hilo: el del llamador
// (OperacionExecutor). Sin estado compartido: PosixFileOps es reentrante.
class FileOps {
public:
    enum class TipoEntrada { Directorio, Archivo, Enlace, Otro };

    struct EntradaDirectorio {
        QByteArray nombre;
        TipoEntrada tipo = TipoEntrada::Otro; // por lstat: nunca sigue enlaces
    };

    virtual ~FileOps() = default;

    // lstat. ENOENT si no existe.
    virtual int tipo(const QByteArray& ruta, TipoEntrada& tipo) = 0;
    // stat (SIGUE enlaces). T009.1: solo para la raiz, que puede vivir bajo
    // enlaces del sistema (p. ej. /var -> /private/var).
    virtual int tipoResuelto(const QByteArray& ruta, TipoEntrada& tipo) = 0;
    // mkdir con `modo` exacto (aplica chmod tras crear, sin depender de umask).
    // EEXIST si ya existe.
    virtual int crearDirectorio(const QByteArray& ruta, unsigned modo) = 0;
    // open(O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC) con `modo` exacto.
    virtual int abrirExclusivo(const QByteArray& ruta, unsigned modo, int& fd) = 0;
    // Escribe los `n` bytes completos (reintenta escrituras parciales/EINTR).
    virtual int escribir(int fd, const char* datos, std::size_t n) = 0;
    // fsync + F_FULLFSYNC (macOS). F_FULLFSYNC no soportado por el volumen
    // (ENOTSUP/EINVAL) no es error si fsync tuvo exito.
    virtual int sincronizarArchivo(int fd) = 0;
    virtual int cerrar(int fd) = 0;
    // Promocion sin reemplazo: renamex_np(RENAME_EXCL) en macOS. EEXIST si el
    // destino existe; nunca sobrescribe.
    virtual int promoverSinReemplazo(const QByteArray& origen, const QByteArray& destino) = 0;
    // open(O_RDONLY|O_DIRECTORY) + fsync del directorio.
    virtual int sincronizarDirectorio(const QByteArray& directorio) = 0;
    // unlink (solo archivos).
    virtual int eliminarArchivo(const QByteArray& ruta) = 0;
    // Entradas del directorio sin "." ni "..", con su tipo por lstat.
    virtual int listar(const QByteArray& directorio, QList<EntradaDirectorio>& entradas) = 0;
    // Bytes aleatorios para el sufijo del temporal (arc4random_buf).
    virtual void aleatorio(unsigned char* destino, std::size_t n) = 0;
};

// Implementacion real POSIX/macOS.
class PosixFileOps : public FileOps {
public:
    int tipo(const QByteArray& ruta, TipoEntrada& tipo) override;
    int tipoResuelto(const QByteArray& ruta, TipoEntrada& tipo) override;
    int crearDirectorio(const QByteArray& ruta, unsigned modo) override;
    int abrirExclusivo(const QByteArray& ruta, unsigned modo, int& fd) override;
    int escribir(int fd, const char* datos, std::size_t n) override;
    int sincronizarArchivo(int fd) override;
    int cerrar(int fd) override;
    int promoverSinReemplazo(const QByteArray& origen, const QByteArray& destino) override;
    int sincronizarDirectorio(const QByteArray& directorio) override;
    int eliminarArchivo(const QByteArray& ruta) override;
    int listar(const QByteArray& directorio, QList<EntradaDirectorio>& entradas) override;
    void aleatorio(unsigned char* destino, std::size_t n) override;
};

} // namespace satcfdi
