#include "application/common/FechasLegibles.h"

#include <array>

namespace satcfdi::fechaslegibles {

namespace {

QString mes(int numero)
{
    static const std::array<const char*, 12> meses = {"ene", "feb", "mar", "abr", "may", "jun",
                                                      "jul", "ago", "sep", "oct", "nov", "dic"};
    return (numero >= 1 && numero <= 12) ? QString::fromLatin1(meses[std::size_t(numero - 1)]) : QString();
}

QString diaMes(const QDate& d)
{
    return QStringLiteral("%1 %2").arg(d.day()).arg(mes(d.month()));
}

const QString kGuionLargo = QStringLiteral("–"); // en dash

} // namespace

QString fecha(const QDate& dia)
{
    return dia.isValid() ? QStringLiteral("%1 %2").arg(diaMes(dia)).arg(dia.year()) : QString();
}

QString rango(const QDate& inicial, const QDate& final_)
{
    if (!inicial.isValid() || !final_.isValid()) {
        return inicial.isValid() ? fecha(inicial) : fecha(final_);
    }
    if (inicial == final_) {
        return fecha(inicial);
    }
    if (inicial.year() == final_.year()) {
        if (inicial.month() == final_.month()) {
            return QStringLiteral("%1%2%3 %4 %5")
                .arg(inicial.day())
                .arg(kGuionLargo)
                .arg(final_.day())
                .arg(mes(final_.month()))
                .arg(final_.year());
        }
        return QStringLiteral("%1 %2 %3").arg(diaMes(inicial), kGuionLargo, fecha(final_));
    }
    return QStringLiteral("%1 %2 %3").arg(fecha(inicial), kGuionLargo, fecha(final_));
}

QDate diaDeTextoSat(const QString& texto)
{
    return QDate::fromString(texto.trimmed().left(10), Qt::ISODate);
}

} // namespace satcfdi::fechaslegibles
