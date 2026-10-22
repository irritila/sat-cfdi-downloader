#include "app_core/ArchivosRestauracion.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <cerrno>
#include <dirent.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/xattr.h>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

namespace satcfdi::archivos_restauracion {

namespace {

QByteArray nativa(const QString& ruta)
{
    return QFile::encodeName(ruta);
}

int abrir(const QString& ruta, int flags, mode_t modo = 0)
{
    int fd = -1;
    do {
        fd = ::open(nativa(ruta).constData(), flags | O_CLOEXEC, modo);
    } while (fd < 0 && errno == EINTR);
    return fd;
}

bool sincronizarFd(int fd)
{
    if (::fsync(fd) != 0) {
        return false;
    }
#ifdef F_FULLFSYNC
    if (::fcntl(fd, F_FULLFSYNC) != 0 && errno != ENOTSUP && errno != EINVAL && errno != ENOTTY) {
        return false;
    }
#endif
    return true;
}

bool escribirTodo(int fd, const char* datos, qsizetype n)
{
    while (n > 0) {
        const ssize_t w = ::write(fd, datos, static_cast<size_t>(n));
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        datos += w;
        n -= w;
    }
    return true;
}

qint64 ns(const struct timespec& t)
{
    return static_cast<qint64>(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}

} // namespace

bool sincronizarArchivo(const QString& ruta)
{
    const int fd = abrir(ruta, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        return false;
    }
    const bool ok = sincronizarFd(fd);
    ::close(fd);
    return ok;
}

bool sincronizarDirectorio(const QString& ruta)
{
    const int fd = abrir(ruta, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        return false;
    }
    const bool ok = sincronizarFd(fd);
    ::close(fd);
    return ok;
}

bool escribirAtomico(const QString& ruta, const QByteArray& datos)
{
    const QString tmp = ruta + QStringLiteral(".tmp");
    const int fd = abrir(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        return false;
    }
    bool ok = ::fchmod(fd, 0600) == 0 && escribirTodo(fd, datos.constData(), datos.size()) && sincronizarFd(fd);
    ok = (::close(fd) == 0) && ok;
    if (!ok || ::rename(nativa(tmp).constData(), nativa(ruta).constData()) != 0) {
        ::unlink(nativa(tmp).constData());
        return false;
    }
    return sincronizarDirectorio(QFileInfo(ruta).absolutePath());
}

bool existe(const QString& ruta)
{
    struct stat st {};
    return ::lstat(nativa(ruta).constData(), &st) == 0;
}

bool esDirectorioPrivado(const QString& ruta)
{
    struct stat st {};
    return ::lstat(nativa(ruta).constData(), &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 07777) == 0700
        && st.st_uid == ::geteuid();
}

bool crearDirectorioPrivado(const QString& ruta)
{
    if (::mkdir(nativa(ruta).constData(), 0700) != 0) {
        return false;
    }
    return ::chmod(nativa(ruta).constData(), 0700) == 0 && esDirectorioPrivado(ruta);
}

bool asegurarDirectorioPrivado(const QString& ruta)
{
    if (!existe(ruta)) {
        return crearDirectorioPrivado(ruta);
    }
    return esDirectorioPrivado(ruta);
}

namespace {

// Lista los nombres de `dirfd` (sin "." ni ".."). El descriptor no se consume.
bool nombresEn(int dirfd, std::vector<std::string>& nombres)
{
    const int copia = ::dup(dirfd);
    if (copia < 0) {
        return false;
    }
    DIR* d = ::fdopendir(copia);
    if (!d) {
        ::close(copia);
        return false;
    }
    ::rewinddir(d);
    while (struct dirent* e = ::readdir(d)) {
        const std::string n(e->d_name);
        if (n != "." && n != "..") {
            nombres.push_back(n);
        }
    }
    ::closedir(d);
    return true;
}

// Vacia la carpeta `dirfd` sin seguir enlaces: cada hija se abre con
// openat(O_DIRECTORY|O_NOFOLLOW) y se borra con unlinkat sobre su padre.
bool vaciarEn(int dirfd)
{
    std::vector<std::string> nombres;
    if (!nombresEn(dirfd, nombres)) {
        return false;
    }
    bool ok = true;
    for (const std::string& n : nombres) {
        struct stat st {};
        if (::fstatat(dirfd, n.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
            ok = ok && errno == ENOENT;
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            const int hijo = ::openat(dirfd, n.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (hijo < 0) {
                ok = false;
                continue;
            }
            ok = vaciarEn(hijo) && ok;
            ::close(hijo);
            ok = ::unlinkat(dirfd, n.c_str(), AT_REMOVEDIR) == 0 && ok;
        } else {
            ok = ::unlinkat(dirfd, n.c_str(), 0) == 0 && ok;
        }
    }
    return ok;
}

bool sincronizarEn(int dirfd)
{
    std::vector<std::string> nombres;
    if (!nombresEn(dirfd, nombres)) {
        return false;
    }
    for (const std::string& n : nombres) {
        struct stat st {};
        if (::fstatat(dirfd, n.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
            return false;
        }
        int fd = -1;
        if (S_ISDIR(st.st_mode)) {
            fd = ::openat(dirfd, n.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            const bool ok = fd >= 0 && sincronizarEn(fd);
            if (fd >= 0) {
                ::close(fd);
            }
            if (!ok) {
                return false;
            }
        } else if (S_ISREG(st.st_mode)) {
            fd = ::openat(dirfd, n.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            const bool ok = fd >= 0 && sincronizarFd(fd);
            if (fd >= 0) {
                ::close(fd);
            }
            if (!ok) {
                return false;
            }
        } else {
            return false; // enlaces u otros: el staging solo tiene archivos y carpetas
        }
    }
    return sincronizarFd(dirfd);
}

} // namespace

bool borrarArbol(const QString& ruta)
{
    struct stat st {};
    if (::lstat(nativa(ruta).constData(), &st) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(st.st_mode)) {
        return ::unlink(nativa(ruta).constData()) == 0;
    }
    const int fd = abrir(ruta, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) {
        return false;
    }
    const bool ok = vaciarEn(fd);
    ::close(fd);
    return ::rmdir(nativa(ruta).constData()) == 0 && ok;
}

bool sincronizarArbol(const QString& ruta)
{
    struct stat st {};
    if (::lstat(nativa(ruta).constData(), &st) != 0) {
        return false;
    }
    if (S_ISREG(st.st_mode)) {
        return sincronizarArchivo(ruta);
    }
    const int fd = abrir(ruta, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) {
        return false;
    }
    const bool ok = sincronizarEn(fd);
    ::close(fd);
    return ok;
}

bool excluirDeRespaldoDelSistema(const QString& ruta)
{
#if defined(Q_OS_DARWIN)
    // Lo que escribe NSURLIsExcludedFromBackupKey (exclusion "sticky" de Time
    // Machine): el xattr com.apple.metadata:com_apple_backup_excludeItem con
    // el bplist00 de la cadena "com.apple.backupd". Sin Foundation en app_core.
    static const unsigned char kPlist[] = {
        'b', 'p', 'l', 'i', 's', 't', '0', '0', 0x5F, 0x10, 0x11, 'c', 'o', 'm', '.', 'a', 'p', 'p', 'l', 'e', '.',
        'b', 'a', 'c', 'k', 'u', 'p', 'd', 0x08, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0x1C};
    return ::setxattr(nativa(ruta).constData(), kXattrExcluirRespaldo, kPlist, sizeof(kPlist), 0, XATTR_NOFOLLOW) == 0;
#else
    Q_UNUSED(ruta);
    return true;
#endif
}

qint64 espacioLibre(const QString& ruta)
{
    struct statfs st {};
    if (::statfs(nativa(ruta).constData(), &st) != 0) {
        return -1;
    }
    return static_cast<qint64>(st.f_bavail) * static_cast<qint64>(st.f_bsize);
}

bool copiarDuradero(const QString& origen, const QString& destino)
{
    const int in = abrir(origen, O_RDONLY | O_NOFOLLOW);
    if (in < 0) {
        return false;
    }
    struct stat st {};
    if (::fstat(in, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(in);
        return false;
    }
    const int out = abrir(destino, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (out < 0) {
        ::close(in);
        return false;
    }
    std::vector<char> buf(1024 * 1024);
    bool ok = ::fchmod(out, 0600) == 0;
    while (ok) {
        const ssize_t r = ::read(in, buf.data(), buf.size());
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        if (r == 0) {
            break;
        }
        ok = escribirTodo(out, buf.data(), r);
    }
    if (ok) {
        const struct timespec tiempos[2] = {st.st_atimespec, st.st_mtimespec};
        ok = ::futimens(out, tiempos) == 0 && sincronizarFd(out);
    }
    ok = (::close(out) == 0) && ok;
    ::close(in);
    if (!ok) {
        ::unlink(nativa(destino).constData());
        return false;
    }
    return sincronizarDirectorio(QFileInfo(destino).absolutePath());
}

qint64 dispositivo(const QString& ruta)
{
    struct stat st {};
    return ::stat(nativa(ruta).constData(), &st) == 0 ? static_cast<qint64>(st.st_dev) : -1;
}

QByteArray sha256(const QString& ruta)
{
    const int fd = abrir(ruta, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        return {};
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    std::vector<char> buf(1024 * 1024);
    bool ok = true;
    for (;;) {
        const ssize_t r = ::read(fd, buf.data(), buf.size());
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        if (r == 0) {
            break;
        }
        h.addData(QByteArrayView(buf.data(), r));
    }
    ::close(fd);
    return ok ? h.result() : QByteArray();
}

bool acumularArbol(const QString& raiz, const QString& etiqueta, QByteArray& acumulado)
{
    std::vector<QString> pila{QString()};
    QStringList lineas;
    while (!pila.empty()) {
        const QString rel = pila.back();
        pila.pop_back();
        const QString abs = rel.isEmpty() ? raiz : QDir(raiz).filePath(rel);
        struct stat st {};
        if (::lstat(nativa(abs).constData(), &st) != 0) {
            return false;
        }
        if (S_ISDIR(st.st_mode)) {
            // Carpetas: solo su ruta (un rename del padre cambia su ctime).
            if (!rel.isEmpty()) {
                lineas.append(QStringLiteral("%1|%2|d").arg(etiqueta, rel));
            }
            const QStringList hijos =
                QDir(abs).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
            for (const QString& h : hijos) {
                pila.push_back(rel.isEmpty() ? h : rel + QLatin1Char('/') + h);
            }
        } else if (S_ISREG(st.st_mode) && !rel.isEmpty()) {
            lineas.append(QStringLiteral("%1|%2|f|%3|%4|%5|%6")
                              .arg(etiqueta, rel)
                              .arg(static_cast<qint64>(st.st_size))
                              .arg(static_cast<quint64>(st.st_ino))
                              .arg(ns(st.st_mtimespec))
                              .arg(ns(st.st_ctimespec)));
        } else {
            return false;
        }
    }
    lineas.sort();
    for (const QString& l : std::as_const(lineas)) {
        acumulado += l.toUtf8();
        acumulado += '\n';
    }
    return true;
}

} // namespace satcfdi::archivos_restauracion
