#include "infrastructure/secrets/macos/DirectorioCredenciales.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace satcfdi::secrets {

namespace {

using Categoria = ErrorSecretStore::Categoria;

ErrorSecretStore errorEscritura(const char* diagnostico, int numero)
{
    return ErrorSecretStore::de(Categoria::FalloEscritura, QString::fromLatin1(diagnostico), numero);
}

// Descriptor con cierre garantizado.
class Descriptor {
public:
    explicit Descriptor(int fd) noexcept : m_fd(fd) {}
    ~Descriptor() { cerrar(); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    int get() const noexcept { return m_fd; }
    bool valido() const noexcept { return m_fd >= 0; }
    int cerrar() noexcept
    {
        int r = 0;
        if (m_fd >= 0) {
            r = ::close(m_fd);
            m_fd = -1;
        }
        return r;
    }

private:
    int m_fd;
};

bool escribirTodo(int fd, const char* datos, std::size_t tamano)
{
    while (tamano > 0) {
        const ssize_t n = ::write(fd, datos, tamano);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        datos += n;
        tamano -= static_cast<std::size_t>(n);
    }
    return true;
}

bool leerTodo(int fd, std::uint8_t* destino, std::size_t tamano)
{
    while (tamano > 0) {
        const ssize_t n = ::read(fd, destino, tamano);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false; // el archivo se acorto durante la lectura
        }
        destino += n;
        tamano -= static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace

// Nombres generados por el adaptador: un solo componente, sin separadores.
static bool nombreSeguro(const QString& nombre)
{
    return !nombre.isEmpty() && nombre != u"." && nombre != u".." && !nombre.contains(u'/')
        && !nombre.contains(QChar(u'\0'));
}

DirectorioCredenciales::DirectorioCredenciales(QString ruta)
    : m_ruta(std::move(ruta))
{
}

// Abre el directorio de credenciales anclado a un descriptor (TOCTOU): sin
// seguir enlaces en el ultimo componente, y valida con fstat sobre ESE
// descriptor que es un directorio del usuario efectivo con permisos 0700.
// Todas las operaciones posteriores son relativas al descriptor (openat,
// renameat, unlinkat, fdopendir), asi que sustituir la ruta despues de abrir
// no redirige escrituras ni borrados. Devuelve -1 con errno; un directorio no
// valido deja errno = ENOTDIR (tipo), EPERM (dueno) o EACCES (permisos).
int DirectorioCredenciales::abrirAnclado(bool corregirPermisos) const
{
    const QByteArray ruta = QFile::encodeName(m_ruta);
    const int fd = ::open(ruta.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ELOOP) {
            errno = ENOTDIR; // enlace simbolico en lugar del directorio
        }
        return -1;
    }
    struct stat st {};
    int numero = 0;
    if (::fstat(fd, &st) != 0) {
        numero = errno;
    } else if (!S_ISDIR(st.st_mode)) {
        numero = ENOTDIR;
    } else if (st.st_uid != ::geteuid()) {
        numero = EPERM;
    } else if ((st.st_mode & 07777) != 0700) {
        if (!corregirPermisos || ::fchmod(fd, 0700) != 0) {
            numero = corregirPermisos ? errno : EACCES;
        }
    }
    if (numero != 0) {
        ::close(fd);
        errno = numero;
        return -1;
    }
    return fd;
}

Resultado<Exito, ErrorSecretStore> DirectorioCredenciales::asegurar() const
{
    using R = Resultado<Exito, ErrorSecretStore>;
    const QByteArray ruta = QFile::encodeName(m_ruta);
    // Padres con permisos por defecto; el directorio final se crea 0700.
    const QString padre = QFileInfo(m_ruta).absolutePath();
    if (!QDir().mkpath(padre)) {
        return R::fallo(errorEscritura("dir.mkpath", 0));
    }
    if (::mkdir(ruta.constData(), 0700) != 0 && errno != EEXIST) {
        return R::fallo(errorEscritura("dir.mkdir", errno));
    }
    Descriptor dir(abrirAnclado(true));
    if (!dir.valido()) {
        return R::fallo(errorEscritura("dir.abrir", errno));
    }
    return R::exito(Exito{});
}

Resultado<Exito, ErrorSecretStore> DirectorioCredenciales::escribirAtomico(const QString& nombre,
                                                                         const QString& nombreTemporal,
                                                                         const QByteArray& datos) const
{
    using R = Resultado<Exito, ErrorSecretStore>;
    if (!nombreSeguro(nombre) || !nombreSeguro(nombreTemporal)) {
        return R::fallo(errorEscritura("archivo.nombre", EINVAL));
    }
    Descriptor dir(abrirAnclado(false));
    if (!dir.valido()) {
        return R::fallo(errorEscritura("dir.abrir", errno));
    }
    const QByteArray temporal = QFile::encodeName(nombreTemporal);
    const QByteArray destino = QFile::encodeName(nombre);

    Descriptor fd(::openat(dir.get(), temporal.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                           0600));
    if (!fd.valido()) {
        return R::fallo(errorEscritura("archivo.crear", errno));
    }
    const auto abortar = [&](const char* diagnostico) {
        const int numero = errno;
        fd.cerrar();
        ::unlinkat(dir.get(), temporal.constData(), 0);
        return R::fallo(errorEscritura(diagnostico, numero));
    };
    if (!escribirTodo(fd.get(), datos.constData(), static_cast<std::size_t>(datos.size()))) {
        return abortar("archivo.escribir");
    }
    if (::fsync(fd.get()) != 0) {
        return abortar("archivo.fsync");
    }
    if (fd.cerrar() != 0) {
        return abortar("archivo.cerrar");
    }
    if (::renameat(dir.get(), temporal.constData(), dir.get(), destino.constData()) != 0) {
        return abortar("archivo.rename");
    }
    if (::fsync(dir.get()) != 0) {
        // El archivo ya esta en su lugar; sin fsync del directorio la
        // durabilidad del rename no esta garantizada: se reporta el fallo y el
        // llamador descarta la generacion.
        return R::fallo(errorEscritura("dir.fsync", errno));
    }
    return R::exito(Exito{});
}

DirectorioCredenciales::Lectura DirectorioCredenciales::leer(const QString& nombre, std::size_t maximo,
                                                             QByteArray& datos) const
{
    datos.clear();
    if (!nombreSeguro(nombre)) {
        return Lectura::Error;
    }
    Descriptor dir(abrirAnclado(false));
    if (!dir.valido()) {
        return errno == ENOENT ? Lectura::NoExiste : Lectura::Error;
    }
    Descriptor fd(::openat(dir.get(), QFile::encodeName(nombre).constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (!fd.valido()) {
        return errno == ENOENT ? Lectura::NoExiste : Lectura::Error;
    }
    struct stat st {};
    if (::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0
        || static_cast<std::size_t>(st.st_size) > maximo) {
        return Lectura::Error;
    }
    datos.resize(static_cast<qsizetype>(st.st_size));
    if (!leerTodo(fd.get(), reinterpret_cast<std::uint8_t*>(datos.data()), static_cast<std::size_t>(st.st_size))) {
        datos.clear();
        return Lectura::Error;
    }
    return Lectura::Ok;
}

Resultado<Exito, ErrorSecretStore> DirectorioCredenciales::eliminar(const QString& nombre) const
{
    using R = Resultado<Exito, ErrorSecretStore>;
    if (!nombreSeguro(nombre)) {
        return R::fallo(errorEscritura("archivo.nombre", EINVAL));
    }
    Descriptor dir(abrirAnclado(false));
    if (!dir.valido()) {
        if (errno == ENOENT) {
            return R::exito(Exito{}); // sin directorio no hay nada que borrar
        }
        return R::fallo(errorEscritura("dir.abrir", errno));
    }
    // fstatat sin seguir enlaces: solo se borran archivos regulares propios.
    const QByteArray nativo = QFile::encodeName(nombre);
    struct stat st {};
    if (::fstatat(dir.get(), nativo.constData(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            return R::exito(Exito{});
        }
        return R::fallo(errorEscritura("archivo.stat", errno));
    }
    if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode)) {
        return R::fallo(errorEscritura("archivo.tipo", EISDIR));
    }
    // unlinkat sobre un enlace borra el enlace, nunca su destino.
    if (::unlinkat(dir.get(), nativo.constData(), 0) != 0 && errno != ENOENT) {
        return R::fallo(errorEscritura("archivo.eliminar", errno));
    }
    if (::fsync(dir.get()) != 0) {
        return R::fallo(errorEscritura("dir.fsync", errno));
    }
    return R::exito(Exito{});
}

Resultado<QStringList, ErrorSecretStore> DirectorioCredenciales::listar() const
{
    using R = Resultado<QStringList, ErrorSecretStore>;
    const int fd = abrirAnclado(false);
    if (fd < 0) {
        if (errno == ENOENT) {
            return R::exito(QStringList{});
        }
        return R::fallo(errorEscritura("dir.listar", errno));
    }
    DIR* dir = ::fdopendir(fd); // toma posesion de fd
    if (dir == nullptr) {
        const int numero = errno;
        ::close(fd);
        return R::fallo(errorEscritura("dir.listar", numero));
    }
    QStringList nombres;
    errno = 0;
    while (const dirent* entrada = ::readdir(dir)) {
        const QString nombre = QFile::decodeName(entrada->d_name);
        if (nombre != u"." && nombre != u"..") {
            nombres.append(nombre);
        }
        errno = 0;
    }
    const int numero = errno;
    ::closedir(dir);
    if (numero != 0) {
        return R::fallo(errorEscritura("dir.listar", numero));
    }
    return R::exito(std::move(nombres));
}

Resultado<BufferSecreto, ErrorSecretStore> leerArchivoEntrada(const QString& ruta, std::size_t maximo)
{
    using R = Resultado<BufferSecreto, ErrorSecretStore>;
    const auto ilegible = [](const char* diagnostico, int numero) {
        return R::fallo(ErrorSecretStore::de(Categoria::ArchivoIlegible, QString::fromLatin1(diagnostico), numero));
    };
    if (ruta.isEmpty()) {
        return ilegible("entrada.ruta", 0);
    }
    const QByteArray nativa = QFile::encodeName(ruta);
    Descriptor fd(::open(nativa.constData(), O_RDONLY | O_CLOEXEC));
    if (!fd.valido()) {
        return ilegible("entrada.abrir", errno);
    }
    struct stat st {};
    if (::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) {
        return ilegible("entrada.tipo", errno);
    }
    const auto tamano = static_cast<std::size_t>(st.st_size);
    if (tamano == 0 || tamano > maximo) {
        return R::fallo(ErrorSecretStore::de(Categoria::FormatoInvalido, QStringLiteral("entrada.tamano")));
    }
    BufferSecreto buffer(tamano);
    if (!leerTodo(fd.get(), buffer.datos(), tamano)) {
        return ilegible("entrada.leer", errno);
    }
    return R::exito(std::move(buffer));
}

} // namespace satcfdi::secrets
