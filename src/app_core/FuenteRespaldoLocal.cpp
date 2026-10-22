#include "app_core/FuenteRespaldoLocal.h"

#include "application/operaciones/OperacionExecutor.h"
#include "domain/common/UuidCanonico.h"
#include "ports/respaldo/ContenedorRespaldo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSettings>

#include <sys/stat.h>
#include <unistd.h>

namespace satcfdi {

using respaldo::CapturaRespaldo;
using respaldo::EntradaCaptura;
using respaldo::ErrorCaptura;
using C = ErrorCaptura::Categoria;
using ResultadoCaptura = Resultado<CapturaRespaldo, ErrorCaptura>;

namespace {

constexpr QFileDevice::Permissions kCarpetaPrivada =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
constexpr QFileDevice::Permissions kArchivoPrivado = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

ErrorCaptura error(C c, const char* diagnostico)
{
    return ErrorCaptura::de(c, QString::fromLatin1(diagnostico));
}

// lstat: directorio real (no enlace) con modo exacto 0700.
bool esCarpetaPrivada(const QString& ruta)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(ruta).constData(), &st) == 0 && S_ISDIR(st.st_mode)
        && (st.st_mode & 07777) == 0700 && st.st_uid == ::geteuid();
}

bool existeSinSeguir(const QString& ruta)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(ruta).constData(), &st) == 0;
}

// Crea (si falta) una carpeta privada y la deja en 0700. Rechaza enlaces.
bool asegurarCarpetaPrivada(const QString& ruta)
{
    if (!existeSinSeguir(ruta) && !QDir().mkdir(ruta)) {
        return false;
    }
    QFileInfo info(ruta);
    if (info.isSymLink() || !info.isDir()) {
        return false;
    }
    return QFile::setPermissions(ruta, kCarpetaPrivada) && esCarpetaPrivada(ruta);
}

// lstat de la hoja (sin seguir enlaces); nullopt si no existe. El escritor
// rechaza despues enlaces y no regulares al abrir.
std::optional<respaldo::HuellaArchivo> huellaDe(const QString& raiz, const QString& relativa)
{
    struct stat st {};
    if (::lstat(QFile::encodeName(QDir(raiz).filePath(relativa)).constData(), &st) != 0) {
        return std::nullopt;
    }
    respaldo::HuellaArchivo h;
    h.tamano = static_cast<quint64>(st.st_size);
    h.mtimeNs = static_cast<qint64>(st.st_mtimespec.tv_sec) * 1'000'000'000 + st.st_mtimespec.tv_nsec;
    h.inodo = static_cast<quint64>(st.st_ino);
    h.dispositivo = static_cast<quint64>(st.st_dev);
    return h;
}

void borrarArbol(const QString& ruta)
{
    QFileInfo info(ruta);
    if (info.isSymLink() || !info.isDir()) {
        QFile::remove(ruta);
        return;
    }
    QDir(ruta).removeRecursively(); // no sigue enlaces a carpetas
}

// D14: lista blanca de claves exactas con validacion de valor.
bool esUuidCanonico(const QString& valor)
{
    return uuid::esCanonico(valor);
}

} // namespace

const QList<FuenteRespaldoLocal::ClavePreferencia>& FuenteRespaldoLocal::clavesPreferencias()
{
    static const QList<ClavePreferencia> claves = {
        {"facturas/ultimoPerfil", &esUuidCanonico}, // T023.4: id del perfil (UUID canonico)
    };
    return claves;
}

// Estado compartido con las continuaciones de captura y la tarea de limpieza
// inicial: no depende de la vida de FuenteRespaldoLocal ni del root.
struct FuenteRespaldoLocal::Estado {
    Opciones opciones;
    std::shared_ptr<respaldo::LectorCopiaBaseDatos> lector;
    mutable QMutex mutex;
    QSet<QString> activas; // nombres `captura-<uuid>` en curso o sin limpiar

    QString raiz() const { return QDir(opciones.directorioDatos).filePath(QString::fromLatin1(kCarpetaStaging)); }

    void terminar(const QString& staging)
    {
        QMutexLocker l(&mutex);
        if (existeSinSeguir(staging)) {
            borrarArbol(staging);
        }
        activas.remove(QFileInfo(staging).fileName());
    }

    int limpiarHuerfanos()
    {
        QMutexLocker l(&mutex);
        const QString r = raiz();
        if (!existeSinSeguir(r)) {
            return 0;
        }
        if (QFileInfo(r).isSymLink() || !QFileInfo(r).isDir()) {
            if (!activas.isEmpty()) {
                return 0; // imposible con capturas activas (exigen raiz privada); no se toca
            }
            QFile::remove(r); // se recrea privada en la proxima captura
            return 1;
        }
        int borrados = 0;
        const QStringList nombres =
            QDir(r).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QString& nombre : nombres) {
            if (activas.contains(nombre)) {
                continue; // C3: nunca una captura activa
            }
            borrarArbol(QDir(r).filePath(nombre));
            ++borrados;
        }
        return borrados;
    }

    ResultadoCaptura completar(const QString& staging, qint64 pausaMs) const;
};

FuenteRespaldoLocal::FuenteRespaldoLocal(OperacionExecutor& ejecutor,
                                         std::shared_ptr<respaldo::LectorCopiaBaseDatos> lector, Opciones opciones)
    : m_ejecutor(ejecutor), m_estado(std::make_shared<Estado>())
{
    m_estado->opciones = std::move(opciones);
    m_estado->lector = std::move(lector);
}

FuenteRespaldoLocal::~FuenteRespaldoLocal() = default;

QString FuenteRespaldoLocal::raizStaging() const
{
    return m_estado->raiz();
}

int FuenteRespaldoLocal::capturasActivas() const
{
    QMutexLocker l(&m_estado->mutex);
    return static_cast<int>(m_estado->activas.size());
}

QFuture<ResultadoCaptura> FuenteRespaldoLocal::capturar()
{
    const std::shared_ptr<Estado> estado = m_estado;
    const QString raiz = estado->raiz();
    const QString nombre = QString::fromLatin1(kPrefijoCaptura) + uuid::generarCanonico();
    const QString staging = QDir(raiz).filePath(nombre);
    {
        // C3: registrar y crear bajo el mismo mutex que la limpieza.
        QMutexLocker l(&estado->mutex);
        if (!QDir::isAbsolutePath(estado->opciones.directorioDatos) || !asegurarCarpetaPrivada(raiz)) {
            return QtFuture::makeReadyValueFuture(ResultadoCaptura::fallo(error(C::Staging, "staging.raiz")));
        }
        if (!asegurarCarpetaPrivada(staging)) {
            borrarArbol(staging);
            return QtFuture::makeReadyValueFuture(ResultadoCaptura::fallo(error(C::Staging, "staging.captura")));
        }
        estado->activas.insert(nombre);
    }
    const QString copia = QDir(staging).filePath(respaldo::kNombreCopiaBase);
    // C1: la continuacion solo retiene `estado` (compartido); nunca `this`.
    return m_ejecutor.copiarBaseParaRespaldo(copia).then(
        QtFuture::Launch::Async, [estado, staging](Resultado<qint64, ErrorCaptura> r) {
            if (!r) {
                estado->terminar(staging);
                return ResultadoCaptura::fallo(std::move(r).error());
            }
            auto captura = estado->completar(staging, r.valor());
            if (!captura) {
                estado->terminar(staging);
            }
            return captura;
        });
}

ResultadoCaptura FuenteRespaldoLocal::Estado::completar(const QString& staging, qint64 pausaMs) const
{
    const QString copia = QDir(staging).filePath(respaldo::kNombreCopiaBase);
    auto contenido = lector->leer(copia);
    if (!contenido) {
        return ResultadoCaptura::fallo(std::move(contenido).error());
    }
    if (!QFile::setPermissions(copia, kArchivoPrivado)) {
        return ResultadoCaptura::fallo(error(C::Copia, "copia.permisos"));
    }

    CapturaRespaldo captura;
    captura.staging = staging;
    captura.pausaEjecutorMs = pausaMs;
    captura.metadatos.versionEsquema = contenido.valor().versionEsquema;
    captura.metadatos.versionApp = opciones.versionApp;
    captura.metadatos.capturadaEnUtc = opciones.reloj().toUTC();
    captura.entradas.append(EntradaCaptura{EntradaCaptura::Tipo::Base, respaldo::kRutaRespaldoBase, staging,
                                           respaldo::kNombreCopiaBase, huellaDe(staging, respaldo::kNombreCopiaBase)});

    if (!opciones.rutaPreferencias.isEmpty() && QFileInfo(opciones.rutaPreferencias).isFile()) {
        const QString destino = QDir(staging).filePath(respaldo::kNombrePreferencias);
        const int claves = filtrarPreferencias(opciones.rutaPreferencias, destino);
        if (claves < 0) {
            return ResultadoCaptura::fallo(error(C::Preferencias, "preferencias.escribir"));
        }
        if (claves > 0) {
            captura.entradas.append(EntradaCaptura{EntradaCaptura::Tipo::Preferencias,
                                                   respaldo::kRutaRespaldoPreferencias, staging,
                                                   respaldo::kNombrePreferencias,
                                                   huellaDe(staging, respaldo::kNombrePreferencias)});
        }
    }

    for (const QString& rutaLocal : contenido.valor().rutasPaquetes) {
        const QString enRespaldo = respaldo::kPrefijoRespaldoPaquetes + rutaLocal;
        if (!respaldo::esRutaRelativaValida(rutaLocal) || !respaldo::esRutaRelativaValida(enRespaldo)) {
            ++captura.paquetesOmitidos;
            continue;
        }
        // D11: la huella se toma al congelar; sin archivo, faltaba al congelar.
        captura.entradas.append(EntradaCaptura{EntradaCaptura::Tipo::Paquete, enRespaldo, opciones.raizPaquetes,
                                               rutaLocal, huellaDe(opciones.raizPaquetes, rutaLocal)});
    }
    return ResultadoCaptura::exito(std::move(captura));
}

Resultado<Exito, ErrorCaptura> FuenteRespaldoLocal::limpiar(const CapturaRespaldo& captura)
{
    using R = Resultado<Exito, ErrorCaptura>;
    const QFileInfo info(captura.staging);
    // Solo carpetas de captura directamente bajo la raiz de staging.
    if (captura.staging.isEmpty() || QDir::cleanPath(info.absolutePath()) != QDir::cleanPath(raizStaging())
        || !info.fileName().startsWith(QString::fromLatin1(kPrefijoCaptura))) {
        return R::fallo(error(C::Staging, "staging.ajeno"));
    }
    m_estado->terminar(captura.staging);
    return existeSinSeguir(captura.staging) ? R::fallo(error(C::Staging, "staging.borrar")) : R::exito(Exito{});
}

int FuenteRespaldoLocal::limpiarHuerfanos()
{
    return m_estado->limpiarHuerfanos();
}

std::function<void(const CapturaRespaldo&)> FuenteRespaldoLocal::limpiadorAutonomo() const
{
    const QString raiz = raizStaging();
    return [estado = m_estado, raiz](const CapturaRespaldo& captura) {
        const QFileInfo info(captura.staging);
        if (!captura.staging.isEmpty() && QDir::cleanPath(info.absolutePath()) == QDir::cleanPath(raiz)
            && info.fileName().startsWith(QString::fromLatin1(kPrefijoCaptura))) {
            estado->terminar(captura.staging);
        }
    };
}

std::function<int()> FuenteRespaldoLocal::tareaLimpiezaHuerfanos() const
{
    return [estado = m_estado] { return estado->limpiarHuerfanos(); };
}

int FuenteRespaldoLocal::filtrarPreferencias(const QString& origen, const QString& destino)
{
    QSettings fuente(origen, QSettings::IniFormat);
    if (fuente.status() != QSettings::NoError) {
        return -1;
    }
    QSettings salida(destino, QSettings::IniFormat);
    int escritas = 0;
    for (const ClavePreferencia& c : clavesPreferencias()) {
        const QString clave = QString::fromLatin1(c.clave);
        if (!fuente.contains(clave)) {
            continue;
        }
        const QVariant valor = fuente.value(clave);
        // Solo texto que valida; listas, binarios o valores invalidos fuera.
        if (valor.typeId() != QMetaType::QString || !c.valida(valor.toString())) {
            continue;
        }
        salida.setValue(clave, valor.toString());
        ++escritas;
    }
    salida.sync();
    if (salida.status() != QSettings::NoError) {
        return -1;
    }
    if (escritas > 0 && !QFile::setPermissions(destino, kArchivoPrivado)) {
        return -1;
    }
    return escritas;
}

} // namespace satcfdi
