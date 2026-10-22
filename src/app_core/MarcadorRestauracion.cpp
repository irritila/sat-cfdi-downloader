#include "app_core/MarcadorRestauracion.h"

#include "app_core/ArchivosRestauracion.h"
#include "domain/common/UuidCanonico.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

#include <cerrno>
#include <unistd.h>

namespace satcfdi {

namespace {

constexpr qsizetype kMaxBytesMarcador = 64 * 1024;

const QString kFormatoFecha = QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ");

struct ParFase {
    MarcadorRestauracion::Fase fase;
    const char* clave;
};

constexpr ParFase kFases[] = {
    {MarcadorRestauracion::Fase::Preparando, "preparando"},
    {MarcadorRestauracion::Fase::Preparado, "preparado"},
    {MarcadorRestauracion::Fase::Solicitado, "solicitado"},
    {MarcadorRestauracion::Fase::PreElegida, "pre_elegida"},
    {MarcadorRestauracion::Fase::MoviendoActuales, "moviendo_actuales"},
    {MarcadorRestauracion::Fase::ActualesMovidos, "actuales_movidos"},
    {MarcadorRestauracion::Fase::Promoviendo, "promoviendo"},
    {MarcadorRestauracion::Fase::Promovido, "promovido"},
    {MarcadorRestauracion::Fase::Revirtiendo, "revirtiendo"},
};

QString fecha(const QDateTime& f)
{
    return f.isValid() ? f.toUTC().toString(kFormatoFecha) : QString();
}

QDateTime leerFecha(const QJsonValue& v)
{
    if (!v.isString() || v.toString().isEmpty()) {
        return {};
    }
    QDateTime f = QDateTime::fromString(v.toString(), kFormatoFecha);
    f.setTimeZone(QTimeZone::UTC);
    return f;
}

bool rutaAbsoluta(const QString& r)
{
    return !r.isEmpty() && QDir::isAbsolutePath(r) && QDir::cleanPath(r) == r;
}

} // namespace

QString MarcadorRestauracion::claveFase(Fase f)
{
    for (const ParFase& p : kFases) {
        if (p.fase == f) {
            return QString::fromLatin1(p.clave);
        }
    }
    return QString();
}

std::optional<MarcadorRestauracion::Fase> MarcadorRestauracion::faseDesde(const QString& clave)
{
    for (const ParFase& p : kFases) {
        if (clave == QLatin1String(p.clave)) {
            return p.fase;
        }
    }
    return std::nullopt;
}

QByteArray MarcadorRestauracion::serializar() const
{
    QJsonObject o;
    o.insert(QStringLiteral("formato"), kFormato);
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("fase"), claveFase(fase));
    o.insert(QStringLiteral("directorioDatos"), directorioDatos);
    o.insert(QStringLiteral("raizPaquetes"), raizPaquetes);
    o.insert(QStringLiteral("stagingDatos"), stagingDatos);
    o.insert(QStringLiteral("stagingExtraccion"), stagingExtraccion);
    o.insert(QStringLiteral("huellaStaging"), QString::fromLatin1(huellaStaging.toHex()));
    QJsonObject origen;
    origen.insert(QStringLiteral("tamano"), QString::number(origenTamano));
    origen.insert(QStringLiteral("mtimeNs"), QString::number(origenMtimeNs));
    origen.insert(QStringLiteral("inodo"), QString::number(origenInodo));
    origen.insert(QStringLiteral("dispositivo"), QString::number(origenDispositivo));
    o.insert(QStringLiteral("origen"), origen);
    o.insert(QStringLiteral("idRespaldo"), QString::fromLatin1(idRespaldo.toHex()));
    o.insert(QStringLiteral("creadoRespaldo"), fecha(creadoRespaldoUtc));
    o.insert(QStringLiteral("fechaPre"), fechaPre);
    o.insert(QStringLiteral("preDatos"), preDatos);
    o.insert(QStringLiteral("prePaquetes"), prePaquetes);
    o.insert(QStringLiteral("faseAntesDeRevertir"), claveFase(faseAntesDeRevertir));
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

std::optional<MarcadorRestauracion> MarcadorRestauracion::desdeBytes(const QByteArray& json)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("formato")).toInt(-1) != kFormato) {
        return std::nullopt;
    }
    MarcadorRestauracion m;
    m.id = o.value(QStringLiteral("id")).toString();
    const auto fase = faseDesde(o.value(QStringLiteral("fase")).toString());
    const auto antes = faseDesde(o.value(QStringLiteral("faseAntesDeRevertir")).toString());
    if (!uuid::esCanonico(m.id) || !fase || !antes) {
        return std::nullopt;
    }
    m.fase = *fase;
    m.faseAntesDeRevertir = *antes;
    m.directorioDatos = o.value(QStringLiteral("directorioDatos")).toString();
    m.raizPaquetes = o.value(QStringLiteral("raizPaquetes")).toString();
    m.stagingDatos = o.value(QStringLiteral("stagingDatos")).toString();
    m.stagingExtraccion = o.value(QStringLiteral("stagingExtraccion")).toString();
    if (!rutaAbsoluta(m.directorioDatos) || !rutaAbsoluta(m.raizPaquetes) || !rutaAbsoluta(m.stagingDatos)
        || !rutaAbsoluta(m.stagingExtraccion)) {
        return std::nullopt;
    }
    // Los stagings tienen nombres fijos con el id: nada fuera de ellos se borra.
    if (m.stagingDatos
            != QDir(QDir(m.directorioDatos).filePath(QLatin1String(marcador_restauracion::kCarpeta)))
                   .filePath(QLatin1String(marcador_restauracion::kPrefijoStagingDatos) + m.id)
        || m.stagingExtraccion
            != QDir(QFileInfo(m.raizPaquetes).absolutePath())
                   .filePath(QLatin1String(marcador_restauracion::kPrefijoStagingExtraccion) + m.id
                             + QLatin1String(marcador_restauracion::kSufijoStagingExtraccion))) {
        return std::nullopt;
    }
    m.huellaStaging = QByteArray::fromHex(o.value(QStringLiteral("huellaStaging")).toString().toLatin1());
    const QJsonObject origen = o.value(QStringLiteral("origen")).toObject();
    m.origenTamano = origen.value(QStringLiteral("tamano")).toString().toLongLong();
    m.origenMtimeNs = origen.value(QStringLiteral("mtimeNs")).toString().toLongLong();
    m.origenInodo = origen.value(QStringLiteral("inodo")).toString().toLongLong();
    m.origenDispositivo = origen.value(QStringLiteral("dispositivo")).toString().toLongLong();
    m.idRespaldo = QByteArray::fromHex(o.value(QStringLiteral("idRespaldo")).toString().toLatin1());
    m.creadoRespaldoUtc = leerFecha(o.value(QStringLiteral("creadoRespaldo")));
    m.fechaPre = o.value(QStringLiteral("fechaPre")).toString();
    m.preDatos = o.value(QStringLiteral("preDatos")).toString();
    m.prePaquetes = o.value(QStringLiteral("prePaquetes")).toString();
    if ((!m.preDatos.isEmpty() && !rutaAbsoluta(m.preDatos)) || (!m.prePaquetes.isEmpty() && !rutaAbsoluta(m.prePaquetes))) {
        return std::nullopt;
    }
    return m;
}

namespace marcador_restauracion {

QString rutaCarpeta(const QString& directorioDatos)
{
    return QDir(directorioDatos).filePath(QLatin1String(kCarpeta));
}

QString rutaArchivo(const QString& directorioDatos)
{
    return QDir(rutaCarpeta(directorioDatos)).filePath(QLatin1String(kArchivo));
}

bool asegurarCarpeta(const QString& directorioDatos)
{
    return archivos_restauracion::asegurarDirectorioPrivado(rutaCarpeta(directorioDatos));
}

Lectura leer(const QString& directorioDatos, MarcadorRestauracion& marcador)
{
    const QString ruta = rutaArchivo(directorioDatos);
    if (!archivos_restauracion::existe(ruta)) {
        return Lectura::Ausente;
    }
    QFile f(ruta);
    if (QFileInfo(ruta).isSymLink() || !f.open(QIODevice::ReadOnly) || f.size() > kMaxBytesMarcador) {
        return Lectura::Invalido;
    }
    const auto m = MarcadorRestauracion::desdeBytes(f.read(kMaxBytesMarcador));
    if (!m) {
        return Lectura::Invalido;
    }
    marcador = *m;
    return Lectura::Leido;
}

bool escribir(const QString& directorioDatos, const MarcadorRestauracion& marcador)
{
    if (!asegurarCarpeta(directorioDatos)) {
        return false;
    }
    return archivos_restauracion::escribirAtomico(rutaArchivo(directorioDatos), marcador.serializar());
}

bool borrar(const QString& directorioDatos)
{
    const QString ruta = rutaArchivo(directorioDatos);
    if (::unlink(QFile::encodeName(ruta).constData()) != 0 && errno != ENOENT) {
        return false;
    }
    ::unlink(QFile::encodeName(ruta + QStringLiteral(".tmp")).constData());
    archivos_restauracion::sincronizarDirectorio(rutaCarpeta(directorioDatos));
    return true;
}

} // namespace marcador_restauracion

} // namespace satcfdi
