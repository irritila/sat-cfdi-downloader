#include "app_core/AreaRestauracionLocal.h"

#include "app_core/ArchivosRestauracion.h"
#include "app_core/MarcadorRestauracion.h"
#include "domain/common/UuidCanonico.h"
#include "ports/respaldo/FuenteRespaldo.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

namespace satcfdi {

using respaldo::DatosPreparado;
using respaldo::ErrorRestauracion;
using respaldo::StagingRestauracion;
using C = ErrorRestauracion::Categoria;
using Fase = MarcadorRestauracion::Fase;
namespace ar = archivos_restauracion;
namespace mr = marcador_restauracion;

namespace {

ErrorRestauracion error(C c, const char* diagnostico)
{
    return ErrorRestauracion::de(c, QString::fromLatin1(diagnostico));
}

template <typename T>
Resultado<T, ErrorRestauracion> falla(C c, const char* d)
{
    return Resultado<T, ErrorRestauracion>::fallo(error(c, d));
}

Resultado<Exito, ErrorRestauracion> ok()
{
    return Resultado<Exito, ErrorRestauracion>::exito(Exito{});
}

bool esRegular(const QString& ruta)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(ruta).constData(), &st) == 0 && S_ISREG(st.st_mode);
}

// Mueve un archivo regular: rename en el mismo volumen, copia durable si no.
bool mover(const QString& origen, const QString& destino)
{
    if (ar::existe(destino)) {
        return false;
    }
    if (ar::dispositivo(QFileInfo(origen).absolutePath()) == ar::dispositivo(QFileInfo(destino).absolutePath())) {
        if (::rename(QFile::encodeName(origen).constData(), QFile::encodeName(destino).constData()) != 0) {
            return false;
        }
        return ar::sincronizarDirectorio(QFileInfo(destino).absolutePath())
            && ar::sincronizarDirectorio(QFileInfo(origen).absolutePath());
    }
    if (!ar::copiarDuradero(origen, destino)) {
        return false;
    }
    return ::unlink(QFile::encodeName(origen).constData()) == 0;
}

constexpr quint64 kMargenMinimo = 64ULL * 1024 * 1024;

quint64 conMargen(quint64 bytes)
{
    return bytes + std::max<quint64>(bytes / 10, kMargenMinimo);
}

// Solo las entradas esperadas en cada staging.
bool soloContiene(const QString& carpeta, const QStringList& permitidas)
{
    const QStringList hay =
        QDir(carpeta).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QString& h : hay) {
        if (!permitidas.contains(h)) {
            return false;
        }
    }
    return true;
}

} // namespace

AreaRestauracionLocal::AreaRestauracionLocal(Opciones opciones) : m_opciones(std::move(opciones))
{
    m_opciones.directorioDatos = QDir::cleanPath(QDir(m_opciones.directorioDatos).absolutePath());
}

QByteArray AreaRestauracionLocal::huellaComponentes(const QString& base, const QString& paquetes,
                                                    const QString& preferencias)
{
    QByteArray acumulado;
    struct stat st {};
    if (::lstat(QFile::encodeName(base).constData(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return {};
    }
    const QByteArray shaBase = ar::sha256(base);
    if (shaBase.isEmpty()) {
        return {};
    }
    acumulado += "base|" + shaBase.toHex() + '|' + QByteArray::number(static_cast<qint64>(st.st_size)) + '\n';
    if (ar::existe(paquetes) && !ar::acumularArbol(paquetes, QStringLiteral("paquetes"), acumulado)) {
        return {};
    }
    if (ar::existe(preferencias)) {
        const QByteArray shaPrefs = ar::sha256(preferencias);
        if (shaPrefs.isEmpty()) {
            return {};
        }
        acumulado += "preferencias|" + shaPrefs.toHex() + '\n';
    }
    return QCryptographicHash::hash(acumulado, QCryptographicHash::Sha256);
}

QByteArray AreaRestauracionLocal::calcularHuella(const QString& stagingDatos, const QString& stagingExtraccion)
{
    // Estructura exacta: nada fuera de lo que se promueve.
    if (!soloContiene(stagingDatos, {QStringLiteral("satcfdi.sqlite3"), respaldo::kNombrePreferencias})
        || !soloContiene(stagingExtraccion, {QStringLiteral("paquetes")})) {
        return {};
    }
    return huellaComponentes(QDir(stagingDatos).filePath(QStringLiteral("satcfdi.sqlite3")),
                             QDir(stagingExtraccion).filePath(QStringLiteral("paquetes")),
                             QDir(stagingDatos).filePath(respaldo::kNombrePreferencias));
}

Resultado<StagingRestauracion, ErrorRestauracion> AreaRestauracionLocal::crearStaging(quint64 tamanoRespaldo)
{
    using R = Resultado<StagingRestauracion, ErrorRestauracion>;
    QMutexLocker l(&m_mutex);
    const QString datos = m_opciones.directorioDatos;
    MarcadorRestauracion previo;
    switch (mr::leer(datos, previo)) {
    case mr::Lectura::Invalido:
        return falla<StagingRestauracion>(C::Marcador, "marcador.invalido");
    case mr::Lectura::Leido:
        if (previo.fase != Fase::Preparando && previo.fase != Fase::Preparado) {
            return falla<StagingRestauracion>(C::RestauracionPendiente, "marcador.pendiente");
        }
        // Preparacion anterior abandonada o no confirmada: se descarta. Como al
        // arrancar, la extraccion solo se borra si su raiz es la de esta
        // sesion; si no, solo lo que vive bajo <datos>/restauracion.
        {
            const bool raizDeLaSesion = !previo.raizPaquetes.isEmpty()
                && QFileInfo(m_opciones.raizPaquetes).canonicalFilePath() == previo.raizPaquetes;
            if (!ar::borrarArbol(previo.stagingDatos) || (raizDeLaSesion && !ar::borrarArbol(previo.stagingExtraccion))
                || !mr::borrar(datos)) {
                return falla<StagingRestauracion>(C::Staging, "staging.previo");
            }
        }
        break;
    case mr::Lectura::Ausente:
        break;
    }

    const QFileInfo raiz(m_opciones.raizPaquetes);
    const QString canonica = raiz.canonicalFilePath();
    if (canonica.isEmpty() || !QFileInfo(canonica).isDir()) {
        return falla<StagingRestauracion>(C::Destino, "raiz.no_existe");
    }
    const QString padre = QFileInfo(canonica).absolutePath();
    const qint64 devRaiz = ar::dispositivo(canonica);
    if (devRaiz < 0 || devRaiz != ar::dispositivo(padre)) {
        return falla<StagingRestauracion>(C::Destino, "raiz.punto_de_montaje");
    }
    // R8: espacio para la extraccion completa en el volumen de la raiz.
    const qint64 libre = m_opciones.espacioLibre ? m_opciones.espacioLibre(padre) : ar::espacioLibre(padre);
    if (libre < 0 || static_cast<quint64>(libre) < conMargen(tamanoRespaldo)) {
        return falla<StagingRestauracion>(C::EspacioInsuficiente, "espacio.raiz");
    }
    // N1: si los datos estan en otro volumen, la base se copiara ahi; se exige
    // ya el margen minimo (el tamano exacto se exige en colocarBase()).
    if (ar::dispositivo(datos) != devRaiz) {
        const qint64 libreDatos = m_opciones.espacioLibre ? m_opciones.espacioLibre(datos) : ar::espacioLibre(datos);
        if (libreDatos < 0 || static_cast<quint64>(libreDatos) < conMargen(0)) {
            return falla<StagingRestauracion>(C::EspacioInsuficiente, "espacio.datos");
        }
    }
    if (!mr::asegurarCarpeta(datos)) {
        return falla<StagingRestauracion>(C::Staging, "staging.carpeta");
    }

    MarcadorRestauracion m;
    m.id = uuid::generarCanonico();
    m.fase = Fase::Preparando;
    m.directorioDatos = datos;
    m.raizPaquetes = canonica;
    m.stagingDatos = QDir(mr::rutaCarpeta(datos)).filePath(QLatin1String(mr::kPrefijoStagingDatos) + m.id);
    m.stagingExtraccion = QDir(padre).filePath(QLatin1String(mr::kPrefijoStagingExtraccion) + m.id
                                               + QLatin1String(mr::kSufijoStagingExtraccion));
    // Primero el marcador: todo staging queda referenciado y el arranque lo borra.
    if (!mr::escribir(datos, m)) {
        return falla<StagingRestauracion>(C::Marcador, "marcador.escribir");
    }
    if (!ar::crearDirectorioPrivado(m.stagingDatos) || !ar::crearDirectorioPrivado(m.stagingExtraccion)) {
        ar::borrarArbol(m.stagingDatos);
        ar::borrarArbol(m.stagingExtraccion);
        mr::borrar(datos);
        return falla<StagingRestauracion>(C::Staging, "staging.crear");
    }
    // R8: fuera de Time Machine (la extraccion lleva .noindex para Spotlight).
    // Sin exito (p. ej. volumen sin xattr) solo se registra.
    if (!ar::excluirDeRespaldoDelSistema(m.stagingDatos) || !ar::excluirDeRespaldoDelSistema(m.stagingExtraccion)) {
        qWarning("restauracion: no se pudo excluir el staging del respaldo del sistema");
    }
    return R::exito(StagingRestauracion{m.id, m.stagingExtraccion, m.stagingDatos});
}

Resultado<Exito, ErrorRestauracion> AreaRestauracionLocal::colocarBase(const StagingRestauracion& s)
{
    QMutexLocker l(&m_mutex);
    const QString base = QDir(s.extraccion).filePath(respaldo::kRutaRespaldoBase);
    if (!esRegular(base)) {
        return falla<Exito>(C::RutaInesperada, "staging.sin_base");
    }
    if (ar::dispositivo(QFileInfo(base).absolutePath()) != ar::dispositivo(s.datos)) {
        struct stat st {};
        const qint64 libre = m_opciones.espacioLibre ? m_opciones.espacioLibre(s.datos) : ar::espacioLibre(s.datos);
        if (::lstat(QFile::encodeName(base).constData(), &st) != 0 || libre < 0
            || static_cast<quint64>(libre) < conMargen(static_cast<quint64>(st.st_size))) {
            return falla<Exito>(C::EspacioInsuficiente, "espacio.datos");
        }
    }
    if (!mover(base, s.rutaBase())) {
        return falla<Exito>(C::Staging, "staging.colocar_base");
    }
    ::rmdir(QFile::encodeName(QFileInfo(base).absolutePath()).constData());
    const QString prefs = QDir(s.extraccion).filePath(respaldo::kRutaRespaldoPreferencias);
    if (ar::existe(prefs)) {
        if (!esRegular(prefs) || !mover(prefs, QDir(s.datos).filePath(respaldo::kNombrePreferencias))) {
            return falla<Exito>(C::Staging, "staging.colocar_preferencias");
        }
        ::rmdir(QFile::encodeName(QFileInfo(prefs).absolutePath()).constData());
    }
    return ok();
}

Resultado<Exito, ErrorRestauracion> AreaRestauracionLocal::fijarMtime(const StagingRestauracion& s,
                                                                      const QString& rutaLocal, qint64 ms)
{
    if (!respaldo::esRutaRelativaValida(rutaLocal)) {
        return falla<Exito>(C::RutaInesperada, "mtime.ruta");
    }
    const QString ruta = QDir(s.extraccion).filePath(respaldo::kPrefijoRespaldoPaquetes + rutaLocal);
    int fd = -1;
    do {
        fd = ::open(QFile::encodeName(ruta).constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        return falla<Exito>(C::Staging, "mtime.abrir");
    }
    struct timespec t {};
    t.tv_sec = static_cast<time_t>(ms / 1000);
    t.tv_nsec = static_cast<long>((ms % 1000) * 1'000'000);
    if (t.tv_nsec < 0) {
        t.tv_sec -= 1;
        t.tv_nsec += 1'000'000'000;
    }
    const struct timespec tiempos[2] = {t, t};
    const bool exito = ::futimens(fd, tiempos) == 0;
    ::close(fd);
    return exito ? ok() : falla<Exito>(C::Staging, "mtime.futimens");
}

Resultado<Exito, ErrorRestauracion> AreaRestauracionLocal::sincronizar(const StagingRestauracion& s)
{
    QMutexLocker l(&m_mutex);
    if (!ar::sincronizarArbol(s.datos) || !ar::sincronizarArbol(s.extraccion)
        || !ar::sincronizarDirectorio(QFileInfo(s.datos).absolutePath())
        || !ar::sincronizarDirectorio(QFileInfo(s.extraccion).absolutePath())) {
        return falla<Exito>(C::Staging, "staging.fsync");
    }
    return ok();
}

Resultado<QByteArray, ErrorRestauracion> AreaRestauracionLocal::huella(const StagingRestauracion& s)
{
    QMutexLocker l(&m_mutex);
    const QByteArray h = calcularHuella(s.datos, s.extraccion);
    if (h.isEmpty()) {
        return falla<QByteArray>(C::Staging, "staging.huella");
    }
    return Resultado<QByteArray, ErrorRestauracion>::exito(h);
}

Resultado<Exito, ErrorRestauracion> AreaRestauracionLocal::marcarPreparado(const StagingRestauracion& s,
                                                                           const DatosPreparado& d)
{
    QMutexLocker l(&m_mutex);
    MarcadorRestauracion m;
    if (mr::leer(m_opciones.directorioDatos, m) != mr::Lectura::Leido || m.id != s.id || m.fase != Fase::Preparando) {
        return falla<Exito>(C::Marcador, "marcador.estado");
    }
    m.fase = Fase::Preparado;
    m.huellaStaging = d.huellaStaging;
    m.origenTamano = static_cast<qint64>(d.huellaOrigen.tamano);
    m.origenMtimeNs = d.huellaOrigen.mtimeNs;
    m.origenInodo = static_cast<qint64>(d.huellaOrigen.inodo);
    m.origenDispositivo = static_cast<qint64>(d.huellaOrigen.dispositivo);
    m.idRespaldo = d.idRespaldo;
    m.creadoRespaldoUtc = d.creadoEnUtc;
    return mr::escribir(m_opciones.directorioDatos, m) ? ok() : falla<Exito>(C::Marcador, "marcador.escribir");
}

Resultado<Exito, ErrorRestauracion> AreaRestauracionLocal::marcarSolicitado(const StagingRestauracion& s)
{
    QMutexLocker l(&m_mutex);
    MarcadorRestauracion m;
    if (mr::leer(m_opciones.directorioDatos, m) != mr::Lectura::Leido || m.id != s.id || m.fase != Fase::Preparado) {
        return falla<Exito>(C::Marcador, "marcador.estado");
    }
    const QByteArray actual = calcularHuella(s.datos, s.extraccion);
    if (actual.isEmpty() || actual != m.huellaStaging) {
        return falla<Exito>(C::StagingCambio, "staging.cambio");
    }
    m.fase = Fase::Solicitado;
    return mr::escribir(m_opciones.directorioDatos, m) ? ok() : falla<Exito>(C::Marcador, "marcador.escribir");
}

void AreaRestauracionLocal::descartar(const StagingRestauracion& s)
{
    QMutexLocker l(&m_mutex);
    MarcadorRestauracion m;
    const auto lectura = mr::leer(m_opciones.directorioDatos, m);
    if (lectura == mr::Lectura::Leido && m.id == s.id && m.fase != Fase::Preparando && m.fase != Fase::Preparado) {
        return; // ya solicitada: la aplica (o descarta) el arranque
    }
    ar::borrarArbol(s.datos);
    ar::borrarArbol(s.extraccion);
    if (lectura == mr::Lectura::Leido && m.id == s.id) {
        mr::borrar(m_opciones.directorioDatos);
    }
}

Resultado<std::unique_ptr<QIODevice>, ErrorRestauracion> OrigenRestauracionArchivo::abrir()
{
    using R = Resultado<std::unique_ptr<QIODevice>, ErrorRestauracion>;
    auto f = std::make_unique<QFile>(m_ruta);
    if (!f->open(QIODevice::ReadOnly)) {
        return R::fallo(error(C::Origen, "origen.abrir"));
    }
    return R::exito(std::unique_ptr<QIODevice>(std::move(f)));
}

Resultado<respaldo::HuellaArchivo, ErrorRestauracion> OrigenRestauracionArchivo::huella()
{
    using R = Resultado<respaldo::HuellaArchivo, ErrorRestauracion>;
    struct stat st {};
    if (::stat(QFile::encodeName(m_ruta).constData(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return R::fallo(error(C::Origen, "origen.stat"));
    }
    respaldo::HuellaArchivo h;
    h.tamano = static_cast<quint64>(st.st_size);
    h.mtimeNs = static_cast<qint64>(st.st_mtimespec.tv_sec) * 1'000'000'000 + st.st_mtimespec.tv_nsec;
    h.inodo = static_cast<quint64>(st.st_ino);
    h.dispositivo = static_cast<quint64>(st.st_dev);
    return R::exito(h);
}

} // namespace satcfdi
