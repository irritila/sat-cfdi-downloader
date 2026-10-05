#include "infrastructure/storage/FileOps.h"

#include <QRandomGenerator>

#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(Q_OS_DARWIN)
#include <cstdlib>
#endif

namespace satcfdi {

int PosixFileOps::tipo(const QByteArray& ruta, TipoEntrada& tipo)
{
    struct stat st {};
    if (::lstat(ruta.constData(), &st) != 0) {
        return errno;
    }
    if (S_ISDIR(st.st_mode)) {
        tipo = TipoEntrada::Directorio;
    } else if (S_ISREG(st.st_mode)) {
        tipo = TipoEntrada::Archivo;
    } else if (S_ISLNK(st.st_mode)) {
        tipo = TipoEntrada::Enlace;
    } else {
        tipo = TipoEntrada::Otro;
    }
    return 0;
}

int PosixFileOps::crearDirectorio(const QByteArray& ruta, unsigned modo)
{
    if (::mkdir(ruta.constData(), mode_t(modo)) != 0) {
        return errno;
    }
    // mkdir aplica umask: se fija el modo exacto.
    if (::chmod(ruta.constData(), mode_t(modo)) != 0) {
        return errno;
    }
    return 0;
}

int PosixFileOps::abrirExclusivo(const QByteArray& ruta, unsigned modo, int& fd)
{
    int f = -1;
    do {
        f = ::open(ruta.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode_t(modo));
    } while (f < 0 && errno == EINTR);
    if (f < 0) {
        return errno;
    }
    if (::fchmod(f, mode_t(modo)) != 0) {
        const int e = errno;
        ::close(f);
        ::unlink(ruta.constData());
        return e;
    }
    fd = f;
    return 0;
}

int PosixFileOps::escribir(int fd, const char* datos, std::size_t n)
{
    std::size_t escritos = 0;
    while (escritos < n) {
        const ssize_t r = ::write(fd, datos + escritos, n - escritos);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return errno;
        }
        escritos += std::size_t(r);
    }
    return 0;
}

int PosixFileOps::sincronizarArchivo(int fd)
{
    if (::fsync(fd) != 0) {
        return errno;
    }
#if defined(F_FULLFSYNC)
    if (::fcntl(fd, F_FULLFSYNC) != 0 && errno != ENOTSUP && errno != EINVAL) {
        return errno;
    }
#endif
    return 0;
}

int PosixFileOps::cerrar(int fd)
{
    return ::close(fd) == 0 ? 0 : errno;
}

int PosixFileOps::promoverSinReemplazo(const QByteArray& origen, const QByteArray& destino)
{
#if defined(Q_OS_DARWIN)
    return ::renamex_np(origen.constData(), destino.constData(), RENAME_EXCL) == 0 ? 0 : errno;
#else
    // Sin renamex_np: link() falla con EEXIST si el destino existe (nunca
    // sobrescribe); luego se retira el nombre temporal.
    if (::link(origen.constData(), destino.constData()) != 0) {
        return errno;
    }
    ::unlink(origen.constData());
    return 0;
#endif
}

int PosixFileOps::sincronizarDirectorio(const QByteArray& directorio)
{
    const int fd = ::open(directorio.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return errno;
    }
    const int r = ::fsync(fd) == 0 ? 0 : errno;
    ::close(fd);
    return r;
}

int PosixFileOps::eliminarArchivo(const QByteArray& ruta)
{
    return ::unlink(ruta.constData()) == 0 ? 0 : errno;
}

int PosixFileOps::listar(const QByteArray& directorio, QList<EntradaDirectorio>& entradas)
{
    DIR* d = ::opendir(directorio.constData());
    if (!d) {
        return errno;
    }
    entradas.clear();
    errno = 0;
    while (const dirent* e = ::readdir(d)) {
        const QByteArray nombre(e->d_name);
        if (nombre == "." || nombre == "..") {
            continue;
        }
        EntradaDirectorio entrada;
        entrada.nombre = nombre;
        if (tipo(directorio + '/' + nombre, entrada.tipo) != 0) {
            entrada.tipo = TipoEntrada::Otro; // desaparecio o ilegible: se ignora
        }
        entradas.append(entrada);
        errno = 0;
    }
    const int error = errno;
    ::closedir(d);
    return error;
}

void PosixFileOps::aleatorio(unsigned char* destino, std::size_t n)
{
#if defined(Q_OS_DARWIN)
    ::arc4random_buf(destino, n);
#else
    for (std::size_t i = 0; i < n; ++i) {
        destino[i] = static_cast<unsigned char>(QRandomGenerator::system()->bounded(256));
    }
#endif
}

} // namespace satcfdi
