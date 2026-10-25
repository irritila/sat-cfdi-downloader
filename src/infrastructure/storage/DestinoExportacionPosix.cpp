#include "infrastructure/storage/DestinoExportacionPosix.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/acl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace satcfdi {

Q_LOGGING_CATEGORY(lcDestinoExportacion, "satcfdi.exportacion.destino")

using exportacion::ArchivoParcialExportacion;
using exportacion::ErrorDestinoExportacion;
using exportacion::PublicacionExportacion;
using exportacion::ResumenLimpiezaExportacion;
using Codigo = ErrorDestinoExportacion::Codigo;
using Fase = DestinoExportacionPosix::Fase;

namespace {

constexpr int kFormatoRegistro = 1;
constexpr qint64 kCaducidadRegistroSegundos = qint64{90} * 24 * 60 * 60;

class Descriptor {
public:
    explicit Descriptor(int fd = -1) noexcept : m_fd(fd) {}
    ~Descriptor() { (void)cerrar(); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&& o) noexcept : m_fd(std::exchange(o.m_fd, -1)) {}
    Descriptor& operator=(Descriptor&& o) noexcept
    {
        if (this != &o) {
            (void)cerrar();
            m_fd = std::exchange(o.m_fd, -1);
        }
        return *this;
    }
    int fd() const noexcept { return m_fd; }
    bool valido() const noexcept { return m_fd >= 0; }
    // close(); devuelve el errno si fallo (0 si no). El descriptor queda
    // invalido en cualquier caso (POSIX: no se reintenta close).
    int cerrar() noexcept
    {
        if (m_fd < 0) {
            return 0;
        }
        const int r = ::close(std::exchange(m_fd, -1));
        return r == 0 ? 0 : errno;
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

ErrorDestinoExportacion errorDe(int codigoErrno, Codigo porDefecto)
{
    return ErrorDestinoExportacion::de(DestinoExportacionPosix::codigoDeErrno(codigoErrno, porDefecto), codigoErrno);
}

void registrarFalla(Fase fase, const ErrorDestinoExportacion& e)
{
    qCWarning(lcDestinoExportacion).noquote()
        << "falla; fase:" << DestinoExportacionPosix::claveFase(fase) << "codigo:" << claveEstable(e.codigo)
        << "errno:" << e.errnoSistema;
}

// Nombre final valido: no vacio, sin '/', NUL ni controles, no oculto (D15 ya
// lo saneo; esto es la defensa del adaptador) y <= 255 bytes en UTF-8.
bool esNombreFinalValido(const QString& nombre)
{
    if (nombre.isEmpty() || nombre.startsWith(QLatin1Char('.')) || nombre.toUtf8().size() > 255) {
        return false;
    }
    for (const QChar c : nombre) {
        if (c == QLatin1Char('/') || c.unicode() < 0x20 || c.unicode() == 0x7f) {
            return false;
        }
    }
    return true;
}

QString nombreParcialNuevo()
{
    unsigned char b[8] = {};
    ::arc4random_buf(b, sizeof(b));
    return exportacion::nombres::kPrefijoParcialExportacion
        + QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(b), sizeof(b)).toHex())
        + exportacion::nombres::kSufijoParcialExportacion;
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

// Conservado: no es un archivo regular propio (enlace, directorio); nunca se
// tocara. Fallo: es regular pero no se pudo borrar (E/S, volumen): se reintenta.
enum class Borrado { Borrado, Ausente, Conservado, Fallo };

// Borra `nombre` solo si es un archivo regular (sin seguir enlaces) y sigue
// siendo el mismo objeto que se abrio. `fallaInyectada` (pruebas): errno con
// el que falla el unlinkat sin ejecutarse.
Borrado borrarSiRegular(int dirfd, const QByteArray& nombre, int fallaInyectada = 0)
{
    Descriptor f(reintentar(
        [&] { return ::openat(dirfd, nombre.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC); }));
    if (!f.valido()) {
        const int e = errno;
        return e == ENOENT ? Borrado::Ausente : (e == ELOOP ? Borrado::Conservado : Borrado::Fallo);
    }
    struct stat abierto {};
    struct stat porNombre {};
    if (::fstat(f.fd(), &abierto) != 0 || !S_ISREG(abierto.st_mode)
        || ::fstatat(dirfd, nombre.constData(), &porNombre, AT_SYMLINK_NOFOLLOW) != 0
        || porNombre.st_dev != abierto.st_dev || porNombre.st_ino != abierto.st_ino) {
        return Borrado::Conservado;
    }
    (void)f.cerrar();
    if (fallaInyectada != 0) {
        return fallaInyectada == ENOENT ? Borrado::Ausente : Borrado::Fallo;
    }
    if (::unlinkat(dirfd, nombre.constData(), 0) != 0) {
        return errno == ENOENT ? Borrado::Ausente : Borrado::Fallo;
    }
    return Borrado::Borrado;
}

// Seguridad 6: true si `dirfd` es `datos` o esta dentro de el, por identidad
// (st_dev/st_ino) subiendo con ".." desde el descriptor (inmune a enlaces y a
// rutas equivalentes). Si algo no se puede leer, no rechaza.
bool estaBajo(int dirfd, const QString& datos)
{
    struct stat sd {};
    if (datos.isEmpty() || ::stat(QFile::encodeName(QDir::cleanPath(datos)).constData(), &sd) != 0) {
        return false;
    }
    Descriptor actual(::dup(dirfd));
    for (int nivel = 0; nivel < 512 && actual.valido(); ++nivel) {
        struct stat st {};
        if (::fstat(actual.fd(), &st) != 0) {
            return false;
        }
        if (st.st_dev == sd.st_dev && st.st_ino == sd.st_ino) {
            return true;
        }
        Descriptor padre(reintentar([&] { return ::openat(actual.fd(), "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC); }));
        struct stat sp {};
        if (!padre.valido() || ::fstat(padre.fd(), &sp) != 0 || (sp.st_dev == st.st_dev && sp.st_ino == st.st_ino)) {
            return false; // raiz o ilegible
        }
        actual = std::move(padre);
    }
    return false;
}

// Abre (y crea si `crear`) un subdirectorio propio de la app: directorio real
// del usuario, sin seguir enlaces, en 0700.
Descriptor abrirNivelPropio(int padre, const char* nombre, bool crear, int& codigo)
{
    codigo = 0;
    if (crear && ::mkdirat(padre, nombre, 0700) != 0 && errno != EEXIST) {
        codigo = errno;
        return Descriptor();
    }
    Descriptor d(reintentar([&] { return ::openat(padre, nombre, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); }));
    if (!d.valido()) {
        codigo = errno;
        return Descriptor();
    }
    struct stat st {};
    if (::fstat(d.fd(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::getuid()) {
        codigo = ENOTDIR;
        return Descriptor();
    }
    if (crear && (st.st_mode & 07777) != 0700 && ::fchmod(d.fd(), 0700) != 0) {
        codigo = errno;
        return Descriptor();
    }
    return d;
}

// --- Registro de parciales en otro volumen -----------------------------------

struct EntradaRegistro {
    QString carpeta;
    quint64 dispositivo = 0;
    quint64 inodo = 0;
    QStringList parciales;
    qint64 actualizadoUtc = 0; // segundos Unix del ultimo registro (caducidad)
};

} // namespace

struct DestinoExportacionPosix::Compartido {
    Opciones opciones;
    QMutex mutex;             // registro y activos
    QSet<QString> activos;    // nombres de `.partial` reservados y vivos

    int falla(Fase fase) const { return opciones.inyectarFalla ? opciones.inyectarFalla(fase) : 0; }

    QString rutaRegistro() const { return DestinoExportacionPosix::rutaRegistro(opciones.directorioDatos); }

    // Bajo `mutex`. Sin archivo: vacio. Ilegible: nullopt.
    std::optional<QList<EntradaRegistro>> cargarRegistro() const
    {
        QList<EntradaRegistro> r;
        QFile f(rutaRegistro());
        if (!f.exists()) {
            return r;
        }
        if (!f.open(QIODevice::ReadOnly)) {
            return std::nullopt;
        }
        QJsonParseError e{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &e);
        if (e.error != QJsonParseError::NoError || !doc.isObject()
            || doc.object().value(QStringLiteral("formato")).toInt() != kFormatoRegistro) {
            return std::nullopt;
        }
        for (const QJsonValue& v : doc.object().value(QStringLiteral("destinos")).toArray()) {
            const QJsonObject o = v.toObject();
            EntradaRegistro en;
            en.carpeta = o.value(QStringLiteral("carpeta")).toString();
            en.dispositivo = o.value(QStringLiteral("dispositivo")).toString().toULongLong();
            en.inodo = o.value(QStringLiteral("inodo")).toString().toULongLong();
            en.actualizadoUtc = o.value(QStringLiteral("actualizado")).toString().toLongLong();
            for (const QJsonValue& p : o.value(QStringLiteral("parciales")).toArray()) {
                if (exportacion::nombres::esParcialExportacion(p.toString())) {
                    en.parciales.append(p.toString());
                }
            }
            if (QDir::isAbsolutePath(en.carpeta) && en.inodo != 0 && !en.parciales.isEmpty()) {
                r.append(en);
            }
        }
        return r;
    }

    // Bajo `mutex`. Escritura atomica (QSaveFile: temporal, fsync, rename);
    // sin entradas borra el archivo. Devuelve el errno (0 si bien).
    int guardarRegistro(const QList<EntradaRegistro>& entradas)
    {
        const QString ruta = rutaRegistro();
        if (entradas.isEmpty()) {
            return (QFile::exists(ruta) && !QFile::remove(ruta)) ? EIO : 0;
        }
        QJsonArray destinos;
        for (const EntradaRegistro& en : entradas) {
            QJsonObject o;
            o.insert(QStringLiteral("carpeta"), en.carpeta);
            o.insert(QStringLiteral("dispositivo"), QString::number(en.dispositivo));
            o.insert(QStringLiteral("inodo"), QString::number(en.inodo));
            o.insert(QStringLiteral("parciales"), QJsonArray::fromStringList(en.parciales));
            o.insert(QStringLiteral("actualizado"), QString::number(en.actualizadoUtc));
            destinos.append(o);
        }
        QJsonObject raiz;
        raiz.insert(QStringLiteral("formato"), kFormatoRegistro);
        raiz.insert(QStringLiteral("destinos"), destinos);
        QSaveFile f(ruta);
        if (!f.open(QIODevice::WriteOnly)
            || !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            || f.write(QJsonDocument(raiz).toJson(QJsonDocument::Compact)) < 0 || !f.commit()) {
            return EIO;
        }
        return 0;
    }

    qint64 ahoraUtc() const
    {
        return (opciones.reloj ? opciones.reloj() : QDateTime::currentDateTimeUtc()).toSecsSinceEpoch();
    }

    // Bajo `mutex`. Seguridad 2: un registro ilegible no se pierde ni se
    // reemplaza en su lugar: se aparta a `parciales-destinos.ilegible-<n>.json`
    // (0600, rename sin reemplazo) para diagnostico; la limpieza lo ignora.
    // Devuelve el errno (0 si quedo apartado).
    int apartarRegistroIlegible()
    {
        const QString ruta = rutaRegistro();
        const QString base = QFileInfo(ruta).absolutePath();
        int codigo = EEXIST;
        for (int n = 1; n <= 1000 && codigo == EEXIST; ++n) {
            const QByteArray destino =
                QFile::encodeName(QDir(base).filePath(QStringLiteral("parciales-destinos.ilegible-%1.json").arg(n)));
            if (::renamex_np(QFile::encodeName(ruta).constData(), destino.constData(), RENAME_EXCL) == 0) {
                (void)::chmod(destino.constData(), 0600);
                codigo = 0;
            } else {
                codigo = errno;
            }
        }
        qCWarning(lcDestinoExportacion).noquote()
            << "registro de parciales ilegible apartado; codigo: registro_ilegible errno:" << codigo;
        return codigo;
    }

    // Bajo `mutex`. Registro legible; uno ilegible se aparta y se empieza vacio.
    std::optional<QList<EntradaRegistro>> cargarOApartar()
    {
        if (auto r = cargarRegistro()) {
            return r;
        }
        if (apartarRegistroIlegible() != 0) {
            return std::nullopt;
        }
        return QList<EntradaRegistro>{};
    }

    // Registra `parcial` en `carpeta` antes de crearlo. Devuelve el errno.
    int registrar(const QString& carpeta, quint64 dispositivo, quint64 inodo, const QString& parcial)
    {
        QMutexLocker l(&mutex);
        if (const int f = falla(Fase::Registrar)) {
            return f;
        }
        auto entradas = cargarOApartar();
        if (!entradas) {
            return EIO; // ni legible ni apartable: no se crea un .partial sin registro
        }
        const qint64 ahora = ahoraUtc();
        bool agregado = false;
        for (EntradaRegistro& en : *entradas) {
            if (en.carpeta == carpeta && en.dispositivo == dispositivo && en.inodo == inodo) {
                en.parciales.append(parcial);
                en.actualizadoUtc = ahora;
                agregado = true;
                break;
            }
        }
        if (!agregado) {
            entradas->append(EntradaRegistro{carpeta, dispositivo, inodo, {parcial}, ahora});
        }
        return guardarRegistro(*entradas);
    }

    // Retira `parcial` del registro (best effort).
    void desregistrar(const QString& parcial)
    {
        QMutexLocker l(&mutex);
        auto entradas = cargarRegistro();
        if (!entradas) {
            return;
        }
        bool cambio = false;
        for (qsizetype i = entradas->size() - 1; i >= 0; --i) {
            if ((*entradas)[i].parciales.removeAll(parcial) > 0) {
                cambio = true;
                if ((*entradas)[i].parciales.isEmpty()) {
                    entradas->removeAt(i);
                }
            }
        }
        if (cambio) {
            (void)guardarRegistro(*entradas);
        }
    }

    void activar(const QString& parcial)
    {
        QMutexLocker l(&mutex);
        activos.insert(parcial);
    }
    void desactivar(const QString& parcial)
    {
        QMutexLocker l(&mutex);
        activos.remove(parcial);
    }
};

namespace {

using Compartido = DestinoExportacionPosix::Compartido;

// Dispositivo de acceso aleatorio sobre el descriptor: pwrite/pread en pos().
class DispositivoPosicional final : public QIODevice {
public:
    DispositivoPosicional(int fd, Compartido& compartido) : m_fd(fd), m_compartido(compartido) {}

    bool isSequential() const override { return false; }
    qint64 size() const override
    {
        struct stat st {};
        return (m_fd >= 0 && ::fstat(m_fd, &st) == 0) ? static_cast<qint64>(st.st_size) : 0;
    }
    const std::optional<ErrorDestinoExportacion>& error() const { return m_error; }
    bool escribioDesdeSincronizar() const { return m_sucio; }
    void marcarSincronizado() { m_sucio = false; }
    void soltar() { m_fd = -1; }

protected:
    qint64 readData(char* datos, qint64 n) override
    {
        if (m_fd < 0) {
            return -1;
        }
        ssize_t r = -1;
        do {
            r = ::pread(m_fd, datos, static_cast<size_t>(n), static_cast<off_t>(pos()));
        } while (r < 0 && errno == EINTR);
        return r < 0 ? -1 : static_cast<qint64>(r);
    }

    qint64 writeData(const char* datos, qint64 n) override
    {
        if (m_fd < 0 || m_error) {
            return -1;
        }
        if (const int f = m_compartido.falla(Fase::Escribir)) {
            fallar(f);
            return -1;
        }
        m_sucio = true;
        qint64 escrito = 0;
        const qint64 base = pos();
        while (escrito < n) {
            const ssize_t r = ::pwrite(m_fd, datos + escrito, static_cast<size_t>(n - escrito),
                                       static_cast<off_t>(base + escrito));
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fallar(errno);
                return -1;
            }
            escrito += r;
        }
        return escrito;
    }

private:
    void fallar(int codigo)
    {
        m_error = errorDe(codigo, Codigo::Escritura);
        registrarFalla(Fase::Escribir, *m_error);
    }

    int m_fd;
    Compartido& m_compartido;
    std::optional<ErrorDestinoExportacion> m_error;
    bool m_sucio = false;
};

class ParcialPosix final : public ArchivoParcialExportacion {
public:
    ParcialPosix(std::shared_ptr<Compartido> compartido, Descriptor carpetaParcial, Descriptor carpetaDestino,
                 Descriptor archivo, QString nombreParcial, QString nombreFinal, bool enDestino)
        : m_compartido(std::move(compartido)), m_carpetaParcial(std::move(carpetaParcial)),
          m_carpetaDestino(std::move(carpetaDestino)), m_archivo(std::move(archivo)),
          m_nombreParcial(std::move(nombreParcial)), m_nombreFinal(std::move(nombreFinal)), m_enDestino(enDestino),
          m_dispositivo(m_archivo.fd(), *m_compartido)
    {
        m_dispositivo.open(QIODevice::ReadWrite | QIODevice::Unbuffered);
    }

    ~ParcialPosix() override { descartar(); }

    QIODevice& dispositivo() override { return m_dispositivo; }
    bool enCarpetaDestino() const override { return m_enDestino; }

    std::optional<ErrorDestinoExportacion> errorEscritura() const override { return m_dispositivo.error(); }

    Resultado<Exito, ErrorDestinoExportacion> truncar(qint64 tamano) override
    {
        using R = Resultado<Exito, ErrorDestinoExportacion>;
        if (!m_archivo.valido() || tamano < 0) {
            return R::fallo(ErrorDestinoExportacion::de(Codigo::Estado));
        }
        int codigo = m_compartido->falla(Fase::Truncar);
        if (codigo == 0 && reintentar([&] { return ::ftruncate(m_archivo.fd(), static_cast<off_t>(tamano)); }) != 0) {
            codigo = errno;
        }
        if (codigo != 0) {
            const auto e = errorDe(codigo, Codigo::Escritura);
            registrarFalla(Fase::Truncar, e);
            return R::fallo(e);
        }
        return R::exito(Exito{});
    }

    Resultado<Exito, ErrorDestinoExportacion> sincronizar() override
    {
        using R = Resultado<Exito, ErrorDestinoExportacion>;
        if (!m_archivo.valido()) {
            return R::fallo(ErrorDestinoExportacion::de(Codigo::Estado));
        }
        int codigo = m_compartido->falla(Fase::Sincronizar);
        if (codigo == 0 && !sincronizarFd(m_archivo.fd())) {
            codigo = errno;
        }
        if (codigo != 0) {
            const auto e = errorDe(codigo, Codigo::Sincronizacion);
            registrarFalla(Fase::Sincronizar, e);
            return R::fallo(e);
        }
        m_dispositivo.marcarSincronizado();
        m_sincronizadoAlgunaVez = true;
        return R::exito(Exito{});
    }

    Resultado<PublicacionExportacion, ErrorDestinoExportacion> publicar() override
    {
        return publicarComo(m_nombreFinal);
    }

    Resultado<PublicacionExportacion, ErrorDestinoExportacion> publicarComo(const QString& nombreFinal) override
    {
        using R = Resultado<PublicacionExportacion, ErrorDestinoExportacion>;
        if (m_publicado || m_descartado) {
            return R::fallo(ErrorDestinoExportacion::de(Codigo::Estado));
        }
        if (!esNombreFinalValido(nombreFinal)) {
            return R::fallo(ErrorDestinoExportacion::de(Codigo::NombreInvalido));
        }
        const auto fallarYDescartar = [&](Fase fase, ErrorDestinoExportacion e) {
            registrarFalla(fase, e);
            descartar();
            return R::fallo(e);
        };
        if (const auto e = m_dispositivo.error()) {
            descartar();
            return R::fallo(*e);
        }
        if (m_archivo.valido()) {
            if (m_dispositivo.escribioDesdeSincronizar() || !m_sincronizadoAlgunaVez) {
                int codigo = m_compartido->falla(Fase::Sincronizar);
                if (codigo == 0 && !sincronizarFd(m_archivo.fd())) {
                    codigo = errno;
                }
                if (codigo != 0) {
                    return fallarYDescartar(Fase::Sincronizar, errorDe(codigo, Codigo::Sincronizacion));
                }
                m_dispositivo.marcarSincronizado();
                m_sincronizadoAlgunaVez = true;
            }
            retirarAtributosExtendidos(m_archivo.fd());
            retirarAcl(m_archivo.fd());
            (void)::fchmod(m_archivo.fd(), 0600);
            m_dispositivo.close();
            m_dispositivo.soltar();
            int codigo = m_compartido->falla(Fase::Cerrar);
            const int cierre = m_archivo.cerrar(); // siempre se cierra
            if (codigo == 0) {
                codigo = cierre;
            }
            if (codigo != 0) {
                return fallarYDescartar(Fase::Cerrar, errorDe(codigo, Codigo::Cierre));
            }
        }

        const QByteArray origen = QFile::encodeName(m_nombreParcial);
        const QByteArray destino = QFile::encodeName(nombreFinal);
        int codigo = m_compartido->falla(Fase::Renombrar);
        if (codigo == 0
            && ::renameatx_np(m_carpetaParcial.fd(), origen.constData(), m_carpetaDestino.fd(), destino.constData(),
                              RENAME_EXCL)
                != 0) {
            codigo = errno;
        }
        if (codigo == EEXIST) {
            const auto e = ErrorDestinoExportacion::de(Codigo::DestinoExistente, codigo);
            registrarFalla(Fase::Renombrar, e); // el .partial sigue: se puede pedir otro nombre
            return R::fallo(e);
        }
        if (codigo == ENOTSUP || codigo == EINVAL) {
            return fallarYDescartar(Fase::Renombrar, ErrorDestinoExportacion::de(Codigo::ExclusivoNoSoportado, codigo));
        }
        if (codigo != 0) {
            return fallarYDescartar(Fase::Renombrar, errorDe(codigo, Codigo::Publicacion));
        }

        m_publicado = true;
        m_compartido->desactivar(m_nombreParcial);
        if (m_enDestino) {
            m_compartido->desregistrar(m_nombreParcial);
        }
        PublicacionExportacion pub;
        const auto sincronizarCarpeta = [&](Fase fase, int fd) {
            int c = m_compartido->falla(fase);
            if (c == 0 && !sincronizarFd(fd)) {
                c = errno;
            }
            if (c != 0 && !pub.advertenciaDurabilidad) {
                pub.advertenciaDurabilidad = true;
                pub.errnoDurabilidad = c;
                qCWarning(lcDestinoExportacion).noquote()
                    << "publicado con advertencia de durabilidad; fase:" << DestinoExportacionPosix::claveFase(fase)
                    << "errno:" << c;
            }
        };
        sincronizarCarpeta(Fase::SincronizarCarpetaDestino, m_carpetaDestino.fd());
        if (!m_enDestino) {
            sincronizarCarpeta(Fase::SincronizarCarpetaPrivada, m_carpetaParcial.fd());
        }
        struct stat st {};
        if (::fstatat(m_carpetaDestino.fd(), destino.constData(), &st, AT_SYMLINK_NOFOLLOW) == 0) {
            pub.tamanoBytes = static_cast<quint64>(st.st_size);
        }
        qCInfo(lcDestinoExportacion).noquote()
            << "publicado; bytes:" << pub.tamanoBytes << "advertencia_durabilidad:" << pub.advertenciaDurabilidad;
        return R::exito(pub);
    }

    void descartar() noexcept override
    {
        if (m_publicado || m_descartado) {
            return;
        }
        m_descartado = true;
        m_dispositivo.close();
        m_dispositivo.soltar();
        (void)m_archivo.cerrar();
        // Seguridad 1: solo se retira del registro si el .partial ya no
        // existe; si el borrado falla (p. ej. disco desconectado), la entrada
        // queda para la limpieza del siguiente arranque.
        int codigo = m_compartido->falla(Fase::Borrar);
        if (codigo == 0 && m_carpetaParcial.valido()
            && ::unlinkat(m_carpetaParcial.fd(), QFile::encodeName(m_nombreParcial).constData(), 0) != 0) {
            codigo = errno;
        }
        if (codigo != 0 && codigo != ENOENT) {
            registrarFalla(Fase::Borrar, errorDe(codigo, Codigo::Escritura));
        }
        m_compartido->desactivar(m_nombreParcial);
        if (m_enDestino && (codigo == 0 || codigo == ENOENT)) {
            m_compartido->desregistrar(m_nombreParcial);
        }
    }

private:
    // Seguridad 5: el ZIP final no conserva una ACL heredada de la carpeta
    // destino (solo cuenta el modo 0600). ACL extendida vacia; best effort
    // (exFAT/SMB pueden no admitir ACL ni modos POSIX).
    static void retirarAcl(int fd)
    {
        acl_t vacia = ::acl_init(0);
        if (vacia != nullptr) {
            (void)::acl_set_fd_np(fd, vacia, ACL_TYPE_EXTENDED);
            (void)::acl_free(vacia);
        }
    }

    // D12: el ZIP final no lleva atributos extendidos (quarantine,
    // provenance u otros que el sistema agregue). Best effort.
    static void retirarAtributosExtendidos(int fd)
    {
        const ssize_t n = ::flistxattr(fd, nullptr, 0, 0);
        if (n <= 0) {
            return;
        }
        QByteArray nombres(static_cast<qsizetype>(n), '\0');
        const ssize_t m = ::flistxattr(fd, nombres.data(), static_cast<size_t>(n), 0);
        if (m <= 0) {
            return;
        }
        for (const QByteArray& nombre : nombres.left(static_cast<qsizetype>(m)).split('\0')) {
            if (!nombre.isEmpty()) {
                (void)::fremovexattr(fd, nombre.constData(), 0);
            }
        }
    }

    std::shared_ptr<Compartido> m_compartido;
    Descriptor m_carpetaParcial;
    Descriptor m_carpetaDestino;
    Descriptor m_archivo;
    QString m_nombreParcial;
    QString m_nombreFinal;
    bool m_enDestino;
    DispositivoPosicional m_dispositivo;
    bool m_publicado = false;
    bool m_descartado = false;
    bool m_sincronizadoAlgunaVez = false;
};

} // namespace

DestinoExportacionPosix::DestinoExportacionPosix(Opciones opciones) : m_compartido(std::make_shared<Compartido>())
{
    m_compartido->opciones = std::move(opciones);
}

DestinoExportacionPosix::~DestinoExportacionPosix() = default;

QString DestinoExportacionPosix::carpetaParciales(const QString& directorioDatos)
{
    return QDir(directorioDatos).filePath(QStringLiteral("exportacion/parciales"));
}

QString DestinoExportacionPosix::rutaRegistro(const QString& directorioDatos)
{
    return QDir(directorioDatos).filePath(QStringLiteral("exportacion/parciales-destinos.json"));
}

QString DestinoExportacionPosix::claveFase(Fase fase)
{
    switch (fase) {
    case Fase::CrearCarpetaPrivada: return QStringLiteral("crear_carpeta_privada");
    case Fase::Registrar: return QStringLiteral("registrar");
    case Fase::Crear: return QStringLiteral("crear");
    case Fase::Escribir: return QStringLiteral("escribir");
    case Fase::Truncar: return QStringLiteral("truncar");
    case Fase::Sincronizar: return QStringLiteral("sincronizar");
    case Fase::Cerrar: return QStringLiteral("cerrar");
    case Fase::Renombrar: return QStringLiteral("renombrar");
    case Fase::SincronizarCarpetaDestino: return QStringLiteral("sincronizar_carpeta_destino");
    case Fase::SincronizarCarpetaPrivada: return QStringLiteral("sincronizar_carpeta_privada");
    case Fase::Borrar: return QStringLiteral("borrar");
    }
    return QStringLiteral("desconocida");
}

Codigo DestinoExportacionPosix::codigoDeErrno(int codigo, Codigo porDefecto)
{
    switch (codigo) {
    case ENOSPC:
    case EDQUOT: return Codigo::EspacioInsuficiente;
    case EACCES:
    case EPERM:
    case EROFS: return Codigo::SinPermiso;
    case ENOTCONN:
    case ESTALE:
    case ETIMEDOUT:
    case ENXIO:
    case ENETDOWN:
    case ENETUNREACH:
    case EHOSTDOWN:
    case EHOSTUNREACH: return Codigo::CarpetaNoDisponible;
    default: return porDefecto;
    }
}

Resultado<std::unique_ptr<ArchivoParcialExportacion>, ErrorDestinoExportacion>
DestinoExportacionPosix::reservar(const QString& carpetaDestino, const QString& nombreFinal)
{
    using R = Resultado<std::unique_ptr<ArchivoParcialExportacion>, ErrorDestinoExportacion>;
    Compartido& c = *m_compartido;
    const auto fallar = [](Fase fase, ErrorDestinoExportacion e) {
        registrarFalla(fase, e);
        return R::fallo(e);
    };
    if (!esNombreFinalValido(nombreFinal)) {
        return R::fallo(ErrorDestinoExportacion::de(Codigo::NombreInvalido));
    }
    if (!QDir::isAbsolutePath(carpetaDestino) || !QDir::isAbsolutePath(c.opciones.directorioDatos)) {
        return R::fallo(ErrorDestinoExportacion::de(Codigo::CarpetaNoDisponible));
    }
    const QString carpeta = QDir::cleanPath(carpetaDestino);

    // Carpeta destino.
    const QByteArray nativa = QFile::encodeName(carpeta);
    Descriptor destino(reintentar([&] { return ::open(nativa.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); }));
    if (!destino.valido()) {
        const int e = errno;
        return fallar(Fase::Crear, ErrorDestinoExportacion::de(
                                       e == ENOTDIR ? Codigo::NoEsCarpeta
                                                    : codigoDeErrno(e, Codigo::CarpetaNoDisponible),
                                       e));
    }
    struct stat stDestino {};
    if (::fstat(destino.fd(), &stDestino) != 0 || !S_ISDIR(stDestino.st_mode)) {
        return R::fallo(ErrorDestinoExportacion::de(Codigo::NoEsCarpeta));
    }
    if (estaBajo(destino.fd(), c.opciones.directorioDatos)) {
        return fallar(Fase::Crear, ErrorDestinoExportacion::de(Codigo::CarpetaNoPermitida));
    }
    const QByteArray nombreNativo = QFile::encodeName(nombreFinal);
    {
        struct stat st {};
        if (::fstatat(destino.fd(), nombreNativo.constData(), &st, AT_SYMLINK_NOFOLLOW) == 0) {
            return R::fallo(ErrorDestinoExportacion::de(Codigo::DestinoExistente, EEXIST));
        }
        if (errno != ENOENT) {
            const int e = errno;
            return fallar(Fase::Crear, errorDe(e, Codigo::Creacion));
        }
    }

    // Carpeta privada (<datos>/exportacion/parciales, 0700).
    Descriptor privada;
    {
        int codigo = c.falla(Fase::CrearCarpetaPrivada);
        if (codigo == 0) {
            Descriptor datos(reintentar([&] {
                return ::open(QFile::encodeName(QDir::cleanPath(c.opciones.directorioDatos)).constData(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            }));
            if (!datos.valido()) {
                codigo = errno;
            } else {
                Descriptor exportacionDir = abrirNivelPropio(datos.fd(), "exportacion", true, codigo);
                if (exportacionDir.valido()) {
                    privada = abrirNivelPropio(exportacionDir.fd(), "parciales", true, codigo);
                }
            }
        }
        if (!privada.valido()) {
            return fallar(Fase::CrearCarpetaPrivada, errorDe(codigo != 0 ? codigo : EIO, Codigo::Creacion));
        }
    }
    struct stat stPrivada {};
    if (::fstat(privada.fd(), &stPrivada) != 0) {
        return fallar(Fase::CrearCarpetaPrivada, errorDe(errno, Codigo::Creacion));
    }
    const bool enDestino = c.opciones.simularOtroVolumen || stPrivada.st_dev != stDestino.st_dev;

    Descriptor carpetaParcial(::dup(enDestino ? destino.fd() : privada.fd()));
    if (!carpetaParcial.valido()) {
        return fallar(Fase::Crear, errorDe(errno, Codigo::Interno));
    }
    for (int intento = 0; intento < 5; ++intento) {
        const QString nombre = nombreParcialNuevo();
        // Seguridad 3: activo ANTES de registrarlo o crearlo, para que una
        // limpieza concurrente nunca lo vea como huerfano.
        c.activar(nombre);
        if (enDestino) {
            if (const int e = c.registrar(carpeta, static_cast<quint64>(stDestino.st_dev),
                                          static_cast<quint64>(stDestino.st_ino), nombre)) {
                c.desactivar(nombre);
                return fallar(Fase::Registrar, ErrorDestinoExportacion::de(Codigo::Registro, e));
            }
        }
        const QByteArray nativo = QFile::encodeName(nombre);
        int codigo = c.falla(Fase::Crear);
        Descriptor f;
        if (codigo == 0) {
            f = Descriptor(reintentar([&] {
                return ::openat(carpetaParcial.fd(), nativo.constData(),
                                O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            }));
            if (!f.valido()) {
                codigo = errno;
            }
        }
        if (f.valido()) {
            (void)::fchmod(f.fd(), 0600);
            return R::exito(std::make_unique<ParcialPosix>(m_compartido, std::move(carpetaParcial),
                                                           std::move(destino), std::move(f), nombre, nombreFinal,
                                                           enDestino));
        }
        if (enDestino) {
            c.desregistrar(nombre);
        }
        c.desactivar(nombre);
        if (codigo != EEXIST) {
            return fallar(Fase::Crear, errorDe(codigo, Codigo::Creacion));
        }
    }
    return fallar(Fase::Crear, ErrorDestinoExportacion::de(Codigo::Creacion, EEXIST));
}

Resultado<ResumenLimpiezaExportacion, ErrorDestinoExportacion> DestinoExportacionPosix::limpiarParcialesPropios()
{
    using R = Resultado<ResumenLimpiezaExportacion, ErrorDestinoExportacion>;
    Compartido& c = *m_compartido;
    ResumenLimpiezaExportacion resumen;
    if (!QDir::isAbsolutePath(c.opciones.directorioDatos)) {
        return R::fallo(ErrorDestinoExportacion::de(Codigo::Interno));
    }
    QMutexLocker l(&c.mutex);
    const auto contar = [&](Borrado b) {
        if (b == Borrado::Borrado) {
            ++resumen.borrados;
        } else if (b == Borrado::Conservado || b == Borrado::Fallo) {
            ++resumen.conservados;
        }
    };

    // 1) Carpeta privada (sin crearla: arrancar no escribe si no hay nada).
    {
        Descriptor datos(reintentar([&] {
            return ::open(QFile::encodeName(QDir::cleanPath(c.opciones.directorioDatos)).constData(),
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        }));
        int codigo = 0;
        Descriptor exportacionDir =
            datos.valido() ? abrirNivelPropio(datos.fd(), "exportacion", false, codigo) : Descriptor();
        Descriptor privada =
            exportacionDir.valido() ? abrirNivelPropio(exportacionDir.fd(), "parciales", false, codigo) : Descriptor();
        if (privada.valido()) {
            if (const auto nombres = nombresEn(privada.fd())) {
                for (const QByteArray& n : *nombres) {
                    const QString nombre = QFile::decodeName(n);
                    if (exportacion::nombres::esParcialExportacion(nombre) && !c.activos.contains(nombre)) {
                        contar(borrarSiRegular(privada.fd(), n, c.falla(Fase::Borrar)));
                    }
                }
            }
        }
    }

    // 2) Carpetas de otro volumen registradas: solo si conservan su identidad
    //    y solo los nombres registrados.
    int carpetasRegistradas = 0;
    int caducadas = 0;
    const qint64 ahora = c.ahoraUtc();
    if (auto entradas = c.cargarOApartar()) {
        carpetasRegistradas = static_cast<int>(entradas->size());
        bool cambio = false;
        for (qsizetype i = entradas->size() - 1; i >= 0; --i) {
            EntradaRegistro& en = (*entradas)[i];
            Descriptor d(reintentar([&] {
                return ::open(QFile::encodeName(en.carpeta).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            }));
            struct stat st {};
            if (!d.valido() || ::fstat(d.fd(), &st) != 0 || static_cast<quint64>(st.st_dev) != en.dispositivo
                || static_cast<quint64>(st.st_ino) != en.inodo) {
                // No verificable: no se toca. Seguridad 4: tras 90 dias sin
                // registros nuevos la entrada (con su ruta) caduca.
                if (ahora - en.actualizadoUtc > kCaducidadRegistroSegundos) {
                    entradas->removeAt(i);
                    cambio = true;
                    ++caducadas;
                } else {
                    resumen.conservados += static_cast<int>(en.parciales.size());
                }
                continue;
            }
            QStringList quedan;
            for (const QString& nombre : std::as_const(en.parciales)) {
                if (c.activos.contains(nombre)) {
                    quedan.append(nombre);
                    continue;
                }
                const Borrado b = borrarSiRegular(d.fd(), QFile::encodeName(nombre), c.falla(Fase::Borrar));
                contar(b);
                if (b == Borrado::Fallo) {
                    quedan.append(nombre); // seguridad 1: se reintenta en otro arranque
                }
            }
            if (quedan.size() != en.parciales.size()) {
                cambio = true;
                if (quedan.isEmpty()) {
                    entradas->removeAt(i);
                } else {
                    en.parciales = quedan;
                }
            }
        }
        if (cambio) {
            (void)c.guardarRegistro(*entradas);
        }
    }

    qCInfo(lcDestinoExportacion).noquote() << "limpieza de parciales; borrados:" << resumen.borrados
                                           << "conservados:" << resumen.conservados
                                           << "carpetas_registradas:" << carpetasRegistradas
                                           << "caducadas:" << caducadas;
    return R::exito(resumen);
}

} // namespace satcfdi
