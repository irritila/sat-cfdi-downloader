#include "infrastructure/storage/DestinoRespaldoPosix.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimeZone>

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace satcfdi {

using respaldo::ArchivoParcial;
using respaldo::EntradaDestino;
using respaldo::ErrorDestino;
using respaldo::EstadoDestino;
using C = ErrorDestino::Categoria;

namespace {

ErrorDestino error(C c, const char* diagnostico)
{
    return ErrorDestino::de(c, QString::fromLatin1(diagnostico));
}

// errno -> error con su categoria (B1, N1). EDEADLK (archivo o carpeta sin
// descargar con la materializacion desactivada) tiene diagnostico propio.
ErrorDestino errorDeErrno(int codigo, C porDefecto, const char* diagnostico)
{
    if (codigo == EDEADLK) {
        return error(C::SinDescargar, "carpeta.sin_descargar");
    }
    return error(DestinoRespaldoPosix::categoriaDeErrno(codigo, porDefecto), diagnostico);
}

// N1: desactiva la materializacion de archivos sin descargar SOLO mientras
// vive (lectura de cabeceras en la retencion) y restaura la politica anterior
// del hilo al destruirse. Anidable por hilo.
class SinMaterializar {
public:
    SinMaterializar()
    {
        if (s_profundidad++ == 0) {
            s_anterior = ::getiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_THREAD);
            (void)::setiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_THREAD,
                                   IOPOL_MATERIALIZE_DATALESS_FILES_OFF);
        }
    }
    ~SinMaterializar()
    {
        if (--s_profundidad == 0) {
            (void)::setiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_THREAD,
                                   s_anterior >= 0 ? s_anterior : IOPOL_MATERIALIZE_DATALESS_FILES_DEFAULT);
        }
    }
    SinMaterializar(const SinMaterializar&) = delete;
    SinMaterializar& operator=(const SinMaterializar&) = delete;

private:
    static thread_local int s_profundidad;
    static thread_local int s_anterior;
};

thread_local int SinMaterializar::s_profundidad = 0;
thread_local int SinMaterializar::s_anterior = IOPOL_MATERIALIZE_DATALESS_FILES_DEFAULT;

// Archivo de lectura que mantiene la materializacion desactivada mientras vive.
class ArchivoSinMaterializar final : public QFile {
public:
    explicit ArchivoSinMaterializar(std::unique_ptr<SinMaterializar> guardia) : m_guardia(std::move(guardia)) {}
    ~ArchivoSinMaterializar() override { close(); } // cerrar antes de restaurar la politica

private:
    std::unique_ptr<SinMaterializar> m_guardia;
};

class Descriptor {
public:
    explicit Descriptor(int fd = -1) noexcept : m_fd(fd) {}
    ~Descriptor() { cerrar(); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&& o) noexcept : m_fd(std::exchange(o.m_fd, -1)) {}
    Descriptor& operator=(Descriptor&& o) noexcept
    {
        if (this != &o) {
            cerrar();
            m_fd = std::exchange(o.m_fd, -1);
        }
        return *this;
    }
    int fd() const noexcept { return m_fd; }
    bool valido() const noexcept { return m_fd >= 0; }
    int liberar() noexcept { return std::exchange(m_fd, -1); }
    bool cerrar() noexcept
    {
        if (m_fd < 0) {
            return true;
        }
        const int r = ::close(m_fd);
        m_fd = -1;
        return r == 0;
    }

private:
    int m_fd;
};

template <typename F>
int reintentar(F&& f)
{
    int r = -1;
    do {
        r = f();
    } while (r < 0 && errno == EINTR);
    return r;
}

bool esNombreSimple(const QString& nombre)
{
    return !nombre.isEmpty() && !nombre.contains(QLatin1Char('/')) && nombre != QLatin1String(".")
        && nombre != QLatin1String("..") && !nombre.contains(QChar(u'\0'));
}

QString hexAleatorio(int bytes)
{
    QByteArray b(bytes, '\0');
    for (int i = 0; i < bytes; ++i) {
        b[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return QString::fromLatin1(b.toHex());
}

// Volumen /Volumes/<n> de una ruta (vacio si no esta bajo /Volumes).
QString volumenDe(const QString& ruta)
{
    const QString limpia = QDir::cleanPath(ruta);
    if (!limpia.startsWith(QLatin1String("/Volumes/"))) {
        return {};
    }
    const QString nombre = limpia.mid(9).section(QLatin1Char('/'), 0, 0);
    return nombre.isEmpty() ? QString() : QStringLiteral("/Volumes/") + nombre;
}

QString rutaRealDe(int fd)
{
    char buffer[MAXPATHLEN] = {};
    if (::fcntl(fd, F_GETPATH, buffer) != 0) {
        return {};
    }
    return QFile::decodeName(buffer);
}

bool sincronizarFd(int fd)
{
#if defined(F_FULLFSYNC)
    if (::fcntl(fd, F_FULLFSYNC) == 0) {
        return true;
    }
    if (errno != ENOTSUP && errno != EINVAL && errno != ENOTTY) {
        return false;
    }
#endif
    return ::fsync(fd) == 0;
}

std::optional<EntradaDestino> entradaDe(int dirfd, const QByteArray& nombre)
{
    struct stat st {};
    if (::fstatat(dirfd, nombre.constData(), &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode)) {
        return std::nullopt;
    }
    EntradaDestino e;
    e.nombre = QFile::decodeName(nombre);
    e.tamano = static_cast<quint64>(st.st_size);
    e.dispositivo = static_cast<quint64>(st.st_dev);
    e.inodo = static_cast<quint64>(st.st_ino);
#if defined(SF_DATALESS)
    e.sinDescargar = (st.st_flags & SF_DATALESS) != 0;
#endif
    return e;
}

// Nombres de un directorio (sobre un duplicado del descriptor).
std::optional<std::vector<QByteArray>> nombresEn(int dirfd)
{
    const int copia = ::dup(dirfd);
    DIR* dir = copia >= 0 ? ::fdopendir(copia) : nullptr;
    if (dir == nullptr) {
        if (copia >= 0) {
            ::close(copia);
        }
        return std::nullopt;
    }
    ::rewinddir(dir);
    std::vector<QByteArray> r;
    while (const dirent* e = ::readdir(dir)) {
        if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) {
            r.emplace_back(e->d_name);
        }
    }
    ::closedir(dir);
    return r;
}

class DispositivoFd final : public QIODevice {
public:
    explicit DispositivoFd(int fd) : m_fd(fd) {}
    bool isSequential() const override { return true; }
    std::optional<int> error() const { return m_errno; }

protected:
    qint64 readData(char*, qint64) override { return -1; }
    qint64 writeData(const char* datos, qint64 n) override
    {
        qint64 escrito = 0;
        while (escrito < n) {
            const ssize_t r = ::write(m_fd, datos + escrito, static_cast<size_t>(n - escrito));
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                m_errno = errno;
                return escrito > 0 ? escrito : -1;
            }
            escrito += r;
        }
        return escrito;
    }

private:
    int m_fd;
    std::optional<int> m_errno;
};

using Renombrar = std::function<int(int, const char*, int, const char*, unsigned)>;

class ParcialPosix final : public ArchivoParcial {
public:
    ParcialPosix(Descriptor carpetaParcial, Descriptor carpetaDestino, Descriptor archivo, QString nombre,
                 bool enDestino, Renombrar renombrar)
        : m_carpetaParcial(std::move(carpetaParcial)), m_carpetaDestino(std::move(carpetaDestino)),
          m_archivo(std::move(archivo)), m_nombre(std::move(nombre)), m_enDestino(enDestino),
          m_renombrar(std::move(renombrar)), m_dispositivo(m_archivo.fd())
    {
        m_dispositivo.open(QIODevice::WriteOnly | QIODevice::Unbuffered);
    }

    ~ParcialPosix() override { m_dispositivo.close(); } // terminacion forzada: el archivo queda

    QString nombre() const override { return m_nombre; }
    bool enCarpetaDestino() const override { return m_enDestino; }
    QIODevice& dispositivo() override { return m_dispositivo; }

    std::optional<ErrorDestino> errorEscritura() const override
    {
        if (const auto e = m_dispositivo.error()) {
            return errorDeErrno(*e, C::Escritura, "escritura.io");
        }
        return std::nullopt;
    }

    Resultado<Exito, ErrorDestino> sincronizar() override
    {
        using R = Resultado<Exito, ErrorDestino>;
        if (!m_archivo.valido()) {
            return R::fallo(error(C::Sincronizacion, "parcial.cerrado"));
        }
        if (sincronizarFd(m_archivo.fd())) {
            return R::exito(Exito{});
        }
        return R::fallo(errorDeErrno(errno, C::Sincronizacion, "fsync"));
    }

    Resultado<EntradaDestino, ErrorDestino> publicar(const QString& nombreFinal) override
    {
        using R = Resultado<EntradaDestino, ErrorDestino>;
        if (!esNombreSimple(nombreFinal) || m_publicado || m_descartado) {
            return R::fallo(error(C::Publicacion, "publicar.estado"));
        }
        m_dispositivo.close();
        if (m_archivo.valido() && !m_archivo.cerrar()) {
            return R::fallo(error(C::Escritura, "parcial.cerrar"));
        }
        const QByteArray origen = QFile::encodeName(m_nombre);
        const QByteArray destino = QFile::encodeName(nombreFinal);
        if (m_renombrar(m_carpetaParcial.fd(), origen.constData(), m_carpetaDestino.fd(), destino.constData(),
                        RENAME_EXCL)
            != 0) {
            const int codigo = errno;
            switch (codigo) {
            case EEXIST: return R::fallo(error(C::Conflicto, "publicar.existe"));
            case ENOTSUP:
            case EINVAL: return R::fallo(error(C::ExclusivoNoSoportado, "publicar.excl_no_soportado"));
            case EXDEV: return R::fallo(error(C::Publicacion, "publicar.otro_volumen"));
            default: return R::fallo(errorDeErrno(codigo, C::Publicacion, "publicar.rename"));
            }
        }
        m_publicado = true;
        (void)sincronizarFd(m_carpetaDestino.fd());
        if (!m_enDestino) {
            (void)sincronizarFd(m_carpetaParcial.fd());
        }
        auto entrada = entradaDe(m_carpetaDestino.fd(), destino);
        if (!entrada) {
            return R::fallo(error(C::Lectura, "publicar.stat"));
        }
        return R::exito(*entrada);
    }

    void descartar() override
    {
        m_dispositivo.close();
        m_archivo.cerrar();
        if (!m_publicado && !m_descartado && m_carpetaParcial.valido()) {
            (void)::unlinkat(m_carpetaParcial.fd(), QFile::encodeName(m_nombre).constData(), 0);
        }
        m_descartado = true;
    }

private:
    Descriptor m_carpetaParcial;
    Descriptor m_carpetaDestino;
    Descriptor m_archivo;
    QString m_nombre;
    bool m_enDestino;
    Renombrar m_renombrar;
    DispositivoFd m_dispositivo;
    bool m_publicado = false;
    bool m_descartado = false;
};

} // namespace

struct DestinoRespaldoPosix::Estado {
    Opciones opciones;
    Descriptor carpeta;
    quint64 dispositivo = 0;
    quint64 inodo = 0;
};

DestinoRespaldoPosix::DestinoRespaldoPosix(QString carpetaAbsoluta)
    : DestinoRespaldoPosix(std::move(carpetaAbsoluta), Opciones{})
{
}

DestinoRespaldoPosix::DestinoRespaldoPosix(QString carpetaAbsoluta, Opciones opciones)
    : m_carpeta(std::move(carpetaAbsoluta)), m_estado(std::make_unique<Estado>())
{
    m_estado->opciones = std::move(opciones);
    if (!m_estado->opciones.renombrar) {
        m_estado->opciones.renombrar = [](int a, const char* b, int c, const char* d, unsigned f) {
            return ::renameatx_np(a, b, c, d, f);
        };
    }
    if (!m_estado->opciones.rutaDeDescriptor) {
        m_estado->opciones.rutaDeDescriptor = rutaRealDe;
    }
}

DestinoRespaldoPosix::~DestinoRespaldoPosix() = default;

ErrorDestino::Categoria DestinoRespaldoPosix::categoriaDeErrno(int codigo, ErrorDestino::Categoria porDefecto)
{
    switch (codigo) {
    case EDEADLK: return C::SinDescargar;
    case ENOTCONN:
    case ESTALE:
    case ETIMEDOUT:
    case ENXIO:
    case ENETDOWN:
    case ENETUNREACH:
    case EHOSTDOWN:
    case EHOSTUNREACH: return C::VolumenNoDisponible;
    case ENOSPC:
    case EDQUOT: return C::EspacioInsuficiente;
    case EACCES:
    case EPERM:
    case EROFS: return C::SinEscritura;
    default: return porDefecto;
    }
}

Resultado<Exito, ErrorDestino> DestinoRespaldoPosix::abrir()
{
    using R = Resultado<Exito, ErrorDestino>;
    if (m_estado->carpeta.valido()) {
        return R::exito(Exito{});
    }
    if (!QDir::isAbsolutePath(m_carpeta)) {
        return R::fallo(error(C::CarpetaInexistente, "carpeta.relativa"));
    }
    const QString limpia = QDir::cleanPath(m_carpeta);

    // Componentes intermedios: un enlace del usuario no se acepta.
    const QStringList partes = limpia.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString prefijo;
    for (qsizetype i = 0; i + 1 < partes.size(); ++i) {
        prefijo += QLatin1Char('/') + partes.at(i);
        struct stat st {};
        if (::lstat(QFile::encodeName(prefijo).constData(), &st) != 0) {
            const int codigo = errno;
            if (codigo == ENOENT) {
                return R::fallo(volumenDe(prefijo).isEmpty() ? error(C::CarpetaInexistente, "carpeta.no_existe")
                                                             : error(C::VolumenNoDisponible, "volumen.no_montado"));
            }
            return R::fallo(errorDeErrno(codigo, C::Lectura, "carpeta.ruta"));
        }
        if (S_ISLNK(st.st_mode) && st.st_uid != 0) {
            return R::fallo(error(C::EnlaceIntermedio, "carpeta.enlace_intermedio"));
        }
    }

    const QByteArray nativa = QFile::encodeName(limpia);
    Descriptor d(reintentar([&] { return ::open(nativa.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); }));
    if (!d.valido()) {
        const int codigo = errno;
        if (codigo == ENOENT) {
            // Enlace final hacia /Volumes/<n> ausente, o ruta bajo /Volumes.
            char destinoEnlace[MAXPATHLEN] = {};
            const ssize_t n = ::readlink(nativa.constData(), destinoEnlace, sizeof(destinoEnlace) - 1);
            const QString objetivo = n > 0 ? QFile::decodeName(QByteArray(destinoEnlace, static_cast<int>(n))) : limpia;
            const QString volumen = volumenDe(objetivo);
            struct stat st {};
            if (!volumen.isEmpty() && ::lstat(QFile::encodeName(volumen).constData(), &st) != 0) {
                return R::fallo(error(C::VolumenNoDisponible, "volumen.no_montado"));
            }
            return R::fallo(error(C::CarpetaInexistente, "carpeta.no_existe"));
        }
        if (codigo == ENOTDIR) {
            return R::fallo(error(C::NoEsCarpeta, "carpeta.no_directorio"));
        }
        return R::fallo(errorDeErrno(codigo, C::Lectura, "carpeta.abrir"));
    }
    struct stat st {};
    if (::fstat(d.fd(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        return R::fallo(error(C::NoEsCarpeta, "carpeta.no_directorio"));
    }
    if (st.st_uid != ::getuid()) {
        return R::fallo(error(C::DuenoDistinto, "carpeta.dueno"));
    }

    // M3: volumen deducido del descriptor (ruta real + montaje).
    const QString real = m_estado->opciones.rutaDeDescriptor(d.fd());
    const QString volumen = volumenDe(real);
    if (!volumen.isEmpty()) {
        struct statfs sf {};
        if (::fstatfs(d.fd(), &sf) != 0) {
            return R::fallo(error(C::VolumenNoDisponible, "volumen.statfs"));
        }
        const QString montaje = QFile::decodeName(sf.f_mntonname);
        if (montaje != volumen && !montaje.startsWith(volumen + QLatin1Char('/'))) {
            return R::fallo(error(C::VolumenNoDisponible, "volumen.no_montado"));
        }
    }

    m_estado->dispositivo = static_cast<quint64>(st.st_dev);
    m_estado->inodo = static_cast<quint64>(st.st_ino);
    m_estado->carpeta = std::move(d);
    return R::exito(Exito{});
}

QString DestinoRespaldoPosix::identidad() const
{
    if (!m_estado->carpeta.valido()) {
        return {};
    }
    return QStringLiteral("%1:%2").arg(m_estado->dispositivo).arg(m_estado->inodo);
}

Resultado<Exito, ErrorDestino> DestinoRespaldoPosix::comprobarIdentidad()
{
    using R = Resultado<Exito, ErrorDestino>;
    if (!m_estado->carpeta.valido()) {
        return R::fallo(error(C::Interno, "carpeta.sin_abrir"));
    }
    const QByteArray nativa = QFile::encodeName(QDir::cleanPath(m_carpeta));
    Descriptor d(reintentar([&] { return ::open(nativa.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); }));
    struct stat st {};
    if (!d.valido()) {
        return R::fallo(errorDeErrno(errno, C::CarpetaCambiada, "carpeta.identidad"));
    }
    if (::fstat(d.fd(), &st) != 0 || static_cast<quint64>(st.st_dev) != m_estado->dispositivo
        || static_cast<quint64>(st.st_ino) != m_estado->inodo) {
        return R::fallo(error(C::CarpetaCambiada, "carpeta.cambiada"));
    }
    return R::exito(Exito{});
}

Resultado<EstadoDestino, ErrorDestino> DestinoRespaldoPosix::validar(quint64 bytesNecesarios)
{
    using R = Resultado<EstadoDestino, ErrorDestino>;
    if (auto a = abrir(); !a) {
        return R::fallo(std::move(a).error());
    }
    const int dirfd = m_estado->carpeta.fd();
    // Escritura real. El archivo de prueba se desvincula APENAS se crea: si el
    // proceso muere despues, no queda nada en la carpeta.
    const QByteArray prueba = QFile::encodeName(QStringLiteral(".archivo-fiscal-prueba-") + hexAleatorio(8));
    {
        Descriptor f(reintentar([&] {
            return ::openat(dirfd, prueba.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        }));
        if (!f.valido()) {
            return R::fallo(errorDeErrno(errno, C::Escritura, "prueba.crear"));
        }
        const bool desvinculado = ::unlinkat(dirfd, prueba.constData(), 0) == 0;
        const int codigoUnlink = errno;
        if (m_estado->opciones.trasDesvincularPrueba) {
            m_estado->opciones.trasDesvincularPrueba();
        }
        if (!desvinculado) {
            return R::fallo(errorDeErrno(codigoUnlink, C::SinEscritura, "prueba.desvincular"));
        }
        const char byte = 0;
        const ssize_t w = ::write(f.fd(), &byte, 1);
        const int codigo = errno;
        const bool cerrado = f.cerrar();
        if (w != 1) {
            return R::fallo(errorDeErrno(codigo, C::Escritura, "prueba.escribir"));
        }
        if (!cerrado) {
            return R::fallo(error(C::Escritura, "prueba.cerrar"));
        }
    }
    struct statfs sf {};
    if (::fstatfs(dirfd, &sf) != 0) {
        return R::fallo(errorDeErrno(errno, C::Lectura, "carpeta.statfs"));
    }
    EstadoDestino estado;
    estado.bytesLibres = static_cast<quint64>(sf.f_bavail) * static_cast<quint64>(sf.f_bsize);
    if (estado.bytesLibres < bytesNecesarios) {
        return R::fallo(error(C::EspacioInsuficiente, "espacio.libre"));
    }
    return R::exito(estado);
}

namespace {

// <AppData>/respaldo/parciales: crea (0700) cada nivel que falte; directorio
// real del usuario, sin enlaces en esos niveles.
Descriptor abrirCarpetaParciales(const QString& ruta)
{
    if (ruta.isEmpty() || !QDir::isAbsolutePath(ruta)) {
        return Descriptor();
    }
    const QString limpia = QDir::cleanPath(ruta);
    const QString padre = QFileInfo(limpia).absolutePath();
    if (!QDir().mkpath(limpia)) {
        return Descriptor();
    }
    // Los dos niveles propios de la app (<datos>/respaldo y .../parciales):
    // directorios reales del usuario en 0700.
    for (const QString& nivel : {padre, limpia}) {
        const QByteArray n = QFile::encodeName(nivel);
        struct stat st {};
        if (::lstat(n.constData(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::getuid()
            || ::chmod(n.constData(), 0700) != 0) {
            return Descriptor();
        }
    }
    return Descriptor(::open(QFile::encodeName(limpia).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
}

} // namespace

Resultado<std::unique_ptr<ArchivoParcial>, ErrorDestino> DestinoRespaldoPosix::crearParcial(const QString& propietario)
{
    using R = Resultado<std::unique_ptr<ArchivoParcial>, ErrorDestino>;
    if (propietario.size() != 8 || !respaldo::nombres::esHex(propietario)) {
        return R::fallo(error(C::Interno, "parcial.propietario"));
    }
    if (auto a = abrir(); !a) {
        return R::fallo(std::move(a).error());
    }
    // D21: datos de la app si estan en el mismo volumen que la carpeta.
    Descriptor local = abrirCarpetaParciales(m_estado->opciones.carpetaParciales);
    bool enDestino = true;
    if (local.valido() && !m_estado->opciones.simularParcialesEnOtroVolumen) {
        struct stat st {};
        enDestino = ::fstat(local.fd(), &st) != 0 || static_cast<quint64>(st.st_dev) != m_estado->dispositivo;
    }
    Descriptor carpetaParcial(::dup(enDestino ? m_estado->carpeta.fd() : local.fd()));
    Descriptor carpetaDestino(::dup(m_estado->carpeta.fd()));
    if (!carpetaParcial.valido() || !carpetaDestino.valido()) {
        return R::fallo(error(C::Interno, "parcial.dup"));
    }
    for (int intento = 0; intento < 5; ++intento) {
        const QString nombre =
            respaldo::nombres::prefijoParcial(propietario) + hexAleatorio(8) + respaldo::nombres::kSufijoParcial;
        const QByteArray nativo = QFile::encodeName(nombre);
        Descriptor f(reintentar([&] {
            return ::openat(carpetaParcial.fd(), nativo.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                            0600);
        }));
        if (f.valido()) {
            return R::exito(std::make_unique<ParcialPosix>(std::move(carpetaParcial), std::move(carpetaDestino),
                                                           std::move(f), nombre, enDestino,
                                                           m_estado->opciones.renombrar));
        }
        if (errno != EEXIST) {
            return R::fallo(errorDeErrno(errno, C::Escritura, "parcial.crear"));
        }
    }
    return R::fallo(error(C::Escritura, "parcial.nombre"));
}

Resultado<QList<EntradaDestino>, ErrorDestino> DestinoRespaldoPosix::listar()
{
    using R = Resultado<QList<EntradaDestino>, ErrorDestino>;
    if (auto a = abrir(); !a) {
        return R::fallo(std::move(a).error());
    }
    const auto nombres = nombresEn(m_estado->carpeta.fd());
    if (!nombres) {
        return R::fallo(errorDeErrno(errno, C::Lectura, "carpeta.listar"));
    }
    QList<EntradaDestino> r;
    for (const QByteArray& n : *nombres) {
        if (auto e = entradaDe(m_estado->carpeta.fd(), n)) {
            r.append(*e);
        }
    }
    return R::exito(r);
}

Resultado<std::unique_ptr<QIODevice>, ErrorDestino> DestinoRespaldoPosix::abrirParaLeer(const QString& nombre)
{
    using R = Resultado<std::unique_ptr<QIODevice>, ErrorDestino>;
    if (!esNombreSimple(nombre)) {
        return R::fallo(error(C::Lectura, "leer.nombre"));
    }
    if (auto a = abrir(); !a) {
        return R::fallo(std::move(a).error());
    }
    // N1/D20: sin materializar solo mientras se abre y se lee este archivo;
    // la politica anterior vuelve al destruir el dispositivo (o ante error).
    auto guardia = std::make_unique<SinMaterializar>();
    const QByteArray nativo = QFile::encodeName(nombre);
    const auto entrada = entradaDe(m_estado->carpeta.fd(), nativo);
    if (!entrada) {
        return R::fallo(error(C::NoRegular, "leer.no_regular"));
    }
    if (entrada->sinDescargar) {
        return R::fallo(error(C::SinDescargar, "carpeta.sin_descargar")); // D20: nunca se descarga
    }
    Descriptor f(reintentar([&] {
        return ::openat(m_estado->carpeta.fd(), nativo.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    }));
    if (!f.valido()) {
        return R::fallo(errno == ELOOP ? error(C::NoRegular, "leer.enlace")
                                       : errorDeErrno(errno, C::Lectura, "leer.abrir"));
    }
    struct stat st {};
    if (::fstat(f.fd(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return R::fallo(error(C::NoRegular, "leer.no_regular"));
    }
    auto archivo = std::make_unique<ArchivoSinMaterializar>(std::move(guardia));
    if (!archivo->open(f.fd(), QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        return R::fallo(error(C::Lectura, "leer.qfile"));
    }
    (void)f.liberar();
    return R::exito(std::unique_ptr<QIODevice>(std::move(archivo)));
}

Resultado<Exito, ErrorDestino> DestinoRespaldoPosix::borrar(const QString& nombre)
{
    using R = Resultado<Exito, ErrorDestino>;
    if (!esNombreSimple(nombre)) {
        return R::fallo(error(C::NoRegular, "borrar.nombre"));
    }
    if (auto a = abrir(); !a) {
        return R::fallo(std::move(a).error());
    }
    const QByteArray nativo = QFile::encodeName(nombre);
    if (!entradaDe(m_estado->carpeta.fd(), nativo)) {
        return R::fallo(error(C::NoRegular, "borrar.no_regular"));
    }
    if (::unlinkat(m_estado->carpeta.fd(), nativo.constData(), 0) != 0) {
        return R::fallo(errorDeErrno(errno, C::Escritura, "borrar.unlink"));
    }
    return R::exito(Exito{});
}

Resultado<int, ErrorDestino> DestinoRespaldoPosix::limpiarParciales(const QString& propietario)
{
    using R = Resultado<int, ErrorDestino>;
    if (auto a = abrir(); !a) {
        return R::fallo(std::move(a).error());
    }
    int borrados = 0;
    const auto limpiarEn = [&](int dirfd) {
        const auto nombres = nombresEn(dirfd);
        if (!nombres) {
            return;
        }
        for (const QByteArray& n : *nombres) {
            if (respaldo::nombres::esParcialDe(QFile::decodeName(n), propietario) && entradaDe(dirfd, n)
                && ::unlinkat(dirfd, n.constData(), 0) == 0) {
                ++borrados;
            }
        }
    };
    limpiarEn(m_estado->carpeta.fd());
    if (!m_estado->opciones.carpetaParciales.isEmpty()) {
        Descriptor local(::open(QFile::encodeName(QDir::cleanPath(m_estado->opciones.carpetaParciales)).constData(),
                                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (local.valido()) {
            limpiarEn(local.fd());
        }
    }
    return R::exito(borrados);
}

// --- Registro de publicados (D20) ---------------------------------------------

Resultado<respaldo::EstadoRegistro, ErrorDestino> RegistroPublicadosJson::cargar()
{
    using R = Resultado<respaldo::EstadoRegistro, ErrorDestino>;
    respaldo::EstadoRegistro estado;
    QFile f(m_ruta);
    if (!f.exists()) {
        return R::exito(estado);
    }
    if (!f.open(QIODevice::ReadOnly)) {
        return R::fallo(error(C::Lectura, "registro.abrir"));
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject() || doc.object().value(QStringLiteral("formato")).toInt() != 1) {
        return R::fallo(error(C::Lectura, "registro.formato"));
    }
    const QJsonObject o = doc.object();
    for (const QJsonValue& v : o.value(QStringLiteral("publicados")).toArray()) {
        const QJsonObject p = v.toObject();
        respaldo::EntradaPublicada ep;
        ep.carpeta = p.value(QStringLiteral("carpeta")).toString();
        ep.nombre = p.value(QStringLiteral("nombre")).toString();
        ep.idRespaldo = QByteArray::fromHex(p.value(QStringLiteral("idRespaldo")).toString().toLatin1());
        ep.fechaUtc = QDateTime::fromString(p.value(QStringLiteral("fecha")).toString(), Qt::ISODateWithMs);
        ep.fechaUtc.setTimeZone(QTimeZone::UTC);
        ep.tamano = p.value(QStringLiteral("tamano")).toString().toULongLong();
        ep.dispositivo = p.value(QStringLiteral("dispositivo")).toString().toULongLong();
        ep.inodo = p.value(QStringLiteral("inodo")).toString().toULongLong();
        if (!ep.nombre.isEmpty() && ep.idRespaldo.size() == 16 && ep.fechaUtc.isValid()) {
            estado.publicados.append(ep);
        }
    }
    for (const QJsonValue& v : o.value(QStringLiteral("duplicadosAvisados")).toArray()) {
        estado.duplicadosAvisados.append(QByteArray::fromHex(v.toString().toLatin1()));
    }
    return R::exito(estado);
}

Resultado<Exito, ErrorDestino> RegistroPublicadosJson::guardar(const respaldo::EstadoRegistro& estado)
{
    using R = Resultado<Exito, ErrorDestino>;
    const QString carpeta = QFileInfo(m_ruta).absolutePath();
    if (!QDir().mkpath(carpeta)
        || !QFile::setPermissions(carpeta, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        return R::fallo(error(C::Escritura, "registro.carpeta"));
    }
    QJsonArray publicados;
    for (const auto& p : estado.publicados) {
        QJsonObject o;
        o.insert(QStringLiteral("carpeta"), p.carpeta);
        o.insert(QStringLiteral("nombre"), p.nombre);
        o.insert(QStringLiteral("idRespaldo"), QString::fromLatin1(p.idRespaldo.toHex()));
        o.insert(QStringLiteral("fecha"), p.fechaUtc.toUTC().toString(Qt::ISODateWithMs));
        o.insert(QStringLiteral("tamano"), QString::number(p.tamano));
        o.insert(QStringLiteral("dispositivo"), QString::number(p.dispositivo));
        o.insert(QStringLiteral("inodo"), QString::number(p.inodo));
        publicados.append(o);
    }
    QJsonArray duplicados;
    for (const auto& id : estado.duplicadosAvisados) {
        duplicados.append(QString::fromLatin1(id.toHex()));
    }
    QJsonObject raiz;
    raiz.insert(QStringLiteral("formato"), 1);
    raiz.insert(QStringLiteral("publicados"), publicados);
    raiz.insert(QStringLiteral("duplicadosAvisados"), duplicados);
    QSaveFile f(m_ruta);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(raiz).toJson(QJsonDocument::Indented)) < 0
        || !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || !f.commit()) {
        return R::fallo(error(C::Escritura, "registro.guardar"));
    }
    return R::exito(Exito{});
}

} // namespace satcfdi
