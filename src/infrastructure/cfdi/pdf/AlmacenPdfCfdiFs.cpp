#include "infrastructure/cfdi/pdf/AlmacenPdfCfdiFs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>

#include <sys/time.h>

#include <algorithm>
#include <utility>

namespace satcfdi {

namespace {

// D11: solo eventos y conteos; nunca rutas, UUID ni nombres de archivo.
Q_LOGGING_CATEGORY(lcAlmacenPdf, "satcfdi.cfdi.pdf")

const QRegularExpression& patronPropio()
{
    static const QRegularExpression patron(QStringLiteral("^[0-9A-Za-z-]{1,64}-[0-9a-f]{64}-g[0-9]{1,6}\\.pdf$"));
    return patron;
}

QString limpiar(const QString& texto, const QString& porDefecto)
{
    QString r;
    r.reserve(texto.size());
    for (const QChar c : texto) {
        if ((c >= u'0' && c <= u'9') || (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || c == u'-') {
            r.append(c);
        }
    }
    r.truncate(64);
    return r.isEmpty() ? porDefecto : r;
}

constexpr QFileDevice::Permissions kPermisosDirectorio =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
constexpr QFileDevice::Permissions kPermisosArchivo = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

bool prepararDirectorio(const QString& directorio)
{
    if (directorio.isEmpty() || QDir::isRelativePath(directorio)) {
        return false;
    }
    const QFileInfo info(directorio);
    if (info.isSymLink()) {
        return false; // nunca escribir a traves de un enlace
    }
    if (!info.exists()) {
        if (!QDir().mkpath(directorio)) {
            return false;
        }
        QFile::setPermissions(directorio, kPermisosDirectorio);
    }
    return QFileInfo(directorio).isDir();
}

// Marca el archivo como usado ahora (fecha de modificacion = uso reciente).
void tocar(const QString& ruta)
{
    ::utimes(QFile::encodeName(ruta).constData(), nullptr);
}

bool esArchivoRegular(const QFileInfo& info)
{
    return info.exists() && info.isFile() && !info.isSymLink();
}

} // namespace

AlmacenPdfCfdiFs::AlmacenPdfCfdiFs(QString directorioCache, QString directorioTemporal, qint64 limiteBytes)
    : m_cache(QDir::cleanPath(std::move(directorioCache)))
    , m_temporal(QDir::cleanPath(std::move(directorioTemporal)))
    , m_limite(limiteBytes > 0 ? limiteBytes : kLimiteBytesPorDefecto)
{
}

QString AlmacenPdfCfdiFs::nombreArchivo(const ClavePdfCfdi& clave)
{
    const QString sha = clave.sha256Xml.toLower();
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    const QString shaValido = hex.match(sha).hasMatch() ? sha : QString(64, QLatin1Char('0'));
    return limpiar(clave.uuid.toUpper(), QStringLiteral("SINUUID")) + QLatin1Char('-') + shaValido
           + QStringLiteral("-g") + QString::number(std::max(0, clave.versionGenerador)) + QStringLiteral(".pdf");
}

std::optional<QString> AlmacenPdfCfdiFs::buscar(const ClavePdfCfdi& clave)
{
    const QString nombre = nombreArchivo(clave);
    for (const QString& directorio : {m_cache, m_temporal}) {
        const QString ruta = QDir(directorio).filePath(nombre);
        const QFileInfo info(ruta);
        if (esArchivoRegular(info) && info.size() > 0) {
            tocar(ruta);
            return info.absoluteFilePath();
        }
    }
    return std::nullopt;
}

std::optional<QString> AlmacenPdfCfdiFs::escribirEn(const QString& directorio, const QString& nombre,
                                                    const QByteArray& pdf)
{
    if (!prepararDirectorio(directorio)) {
        return std::nullopt;
    }
    const QString ruta = QDir(directorio).filePath(nombre);
    QSaveFile archivo(ruta);
    archivo.setDirectWriteFallback(false);
    if (!archivo.open(QIODevice::WriteOnly)) {
        return std::nullopt;
    }
    archivo.setPermissions(kPermisosArchivo);
    if (archivo.write(pdf) != pdf.size() || !archivo.commit()) {
        archivo.cancelWriting();
        return std::nullopt;
    }
    QFile::setPermissions(ruta, kPermisosArchivo);
    return QFileInfo(ruta).absoluteFilePath();
}

Resultado<PdfAlmacenado, ErrorAlmacenPdf> AlmacenPdfCfdiFs::guardar(const ClavePdfCfdi& clave, const QByteArray& pdf)
{
    using R = Resultado<PdfAlmacenado, ErrorAlmacenPdf>;
    const QString nombre = nombreArchivo(clave);
    if (const auto ruta = escribirEn(m_cache, nombre, pdf)) {
        expulsar(m_cache, nombre);
        return R::exito(PdfAlmacenado{*ruta, false});
    }
    qCInfo(lcAlmacenPdf) << "la cache de PDF no se pudo escribir; se usa el temporal";
    if (const auto ruta = escribirEn(m_temporal, nombre, pdf)) {
        expulsar(m_temporal, nombre);
        return R::exito(PdfAlmacenado{*ruta, true});
    }
    qCWarning(lcAlmacenPdf) << "no se pudo escribir el PDF ni en la cache ni en el temporal";
    return R::fallo(ErrorAlmacenPdf{ErrorAlmacenPdf::Tipo::Escritura});
}

void AlmacenPdfCfdiFs::expulsar(const QString& directorio, const QString& conservar)
{
    QFileInfoList archivos;
    qint64 total = 0;
    const QFileInfoList entradas =
        QDir(directorio).entryInfoList({QStringLiteral("*.pdf")}, QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    for (const QFileInfo& info : entradas) {
        if (!esArchivoRegular(info) || !patronPropio().match(info.fileName()).hasMatch()) {
            continue; // nunca se toca nada ajeno
        }
        total += info.size();
        archivos.append(info);
    }
    if (total <= m_limite) {
        return;
    }
    std::sort(archivos.begin(), archivos.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return a.lastModified() < b.lastModified();
    });
    int expulsados = 0;
    for (const QFileInfo& info : std::as_const(archivos)) {
        if (total <= m_limite) {
            break;
        }
        if (info.fileName() == conservar) {
            continue;
        }
        if (QFile::remove(info.absoluteFilePath())) {
            total -= info.size();
            ++expulsados;
        }
    }
    qCInfo(lcAlmacenPdf) << "cache de PDF: archivos expulsados por limite:" << expulsados;
}

bool AlmacenPdfCfdiFs::esPropio(const QString& rutaAbsoluta) const
{
    if (rutaAbsoluta.isEmpty() || QDir::isRelativePath(rutaAbsoluta)) {
        return false;
    }
    const QFileInfo info(rutaAbsoluta);
    if (!esArchivoRegular(info) || !patronPropio().match(info.fileName()).hasMatch()) {
        return false;
    }
    const QString padre = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (padre.isEmpty()) {
        return false;
    }
    for (const QString& directorio : {m_cache, m_temporal}) {
        const QString canonico = QFileInfo(directorio).canonicalFilePath();
        if (!canonico.isEmpty() && canonico == padre) {
            return true;
        }
    }
    return false;
}

Resultado<bool, ErrorAlmacenPdf> AlmacenPdfCfdiFs::copiar(const QString& rutaPdf, const QString& destinoAbsoluto)
{
    using R = Resultado<bool, ErrorAlmacenPdf>;
    if (destinoAbsoluto.isEmpty() || QDir::isRelativePath(destinoAbsoluto) || QFileInfo(destinoAbsoluto).isDir()) {
        return R::fallo(ErrorAlmacenPdf{ErrorAlmacenPdf::Tipo::DestinoInvalido});
    }
    if (!esPropio(rutaPdf)) {
        qCInfo(lcAlmacenPdf) << "guardar PDF rechazado: el origen no es un PDF de la cache";
        return R::fallo(ErrorAlmacenPdf{ErrorAlmacenPdf::Tipo::OrigenInvalido});
    }
    QFile origen(rutaPdf);
    if (!origen.open(QIODevice::ReadOnly)) {
        return R::fallo(ErrorAlmacenPdf{ErrorAlmacenPdf::Tipo::OrigenInvalido});
    }
    const QByteArray contenido = origen.readAll();
    origen.close();
    if (contenido.isEmpty()) {
        return R::fallo(ErrorAlmacenPdf{ErrorAlmacenPdf::Tipo::OrigenInvalido});
    }
    // Atomico: QSaveFile escribe un temporal junto al destino y lo renombra
    // al confirmar; si algo falla, el destino queda como estaba.
    QSaveFile destino(destinoAbsoluto);
    destino.setDirectWriteFallback(false);
    if (!destino.open(QIODevice::WriteOnly)) {
        qCInfo(lcAlmacenPdf) << "guardar PDF fallo: el destino no se pudo abrir";
        return R::fallo(ErrorAlmacenPdf{ErrorAlmacenPdf::Tipo::Escritura});
    }
    if (destino.write(contenido) != contenido.size() || !destino.commit()) {
        destino.cancelWriting();
        qCInfo(lcAlmacenPdf) << "guardar PDF fallo: el destino no se pudo escribir";
        return R::fallo(ErrorAlmacenPdf{ErrorAlmacenPdf::Tipo::Escritura});
    }
    tocar(rutaPdf);
    return R::exito(true);
}

} // namespace satcfdi
