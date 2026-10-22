#include "ports/respaldo/FuenteRespaldo.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

namespace satcfdi::respaldo {

QByteArray MetadatosCaptura::serializar() const
{
    QJsonObject o;
    o.insert(QStringLiteral("formato"), kFormato);
    o.insert(QStringLiteral("esquema"), versionEsquema);
    o.insert(QStringLiteral("app"), versionApp);
    o.insert(QStringLiteral("capturada"),
             capturadaEnUtc.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ")));
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::optional<MetadatosCaptura> MetadatosCaptura::desdeBytes(const QByteArray& json)
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("formato")).toInt(-1) != kFormato || !o.value(QStringLiteral("esquema")).isDouble()
        || !o.value(QStringLiteral("app")).isString() || !o.value(QStringLiteral("capturada")).isString()) {
        return std::nullopt;
    }
    MetadatosCaptura m;
    m.versionEsquema = o.value(QStringLiteral("esquema")).toInt();
    m.versionApp = o.value(QStringLiteral("app")).toString();
    m.capturadaEnUtc = QDateTime::fromString(o.value(QStringLiteral("capturada")).toString(),
                                             QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"));
    m.capturadaEnUtc.setTimeZone(QTimeZone::UTC);
    if (!m.capturadaEnUtc.isValid()) {
        return std::nullopt;
    }
    return m;
}

} // namespace satcfdi::respaldo
