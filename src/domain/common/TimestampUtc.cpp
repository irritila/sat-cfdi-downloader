#include "domain/common/TimestampUtc.h"

#include <QTimeZone>

namespace satcfdi::timestamp {

namespace {

constexpr auto kFormato = u"yyyy-MM-ddTHH:mm:ss.zzz'Z'";

} // namespace

QString aTexto(const QDateTime& instante)
{
    Q_ASSERT(instante.isValid());
    return instante.toUTC().toString(QStringView(kFormato));
}

std::optional<QDateTime> desdeTexto(QStringView texto)
{
    if (texto.size() != 24) {
        return std::nullopt;
    }
    QDateTime dt = QDateTime::fromString(texto.toString(), QStringView(kFormato));
    if (!dt.isValid()) {
        return std::nullopt;
    }
    dt.setTimeZone(QTimeZone(QTimeZone::UTC));
    return dt;
}

QDateTime ahoraUtc()
{
    return QDateTime::fromMSecsSinceEpoch(QDateTime::currentMSecsSinceEpoch(),
                                          QTimeZone(QTimeZone::UTC));
}

} // namespace satcfdi::timestamp
