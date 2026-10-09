#include "FormatoFechas.h"

#include <QTimeZone>

#include <array>

namespace satcfdi {

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

QDateTime instanteDesde(const QVariant& valor)
{
    if (valor.metaType() == QMetaType::fromType<QDateTime>()) {
        return valor.toDateTime();
    }
    if (valor.metaType() == QMetaType::fromType<QString>()) {
        const QString texto = valor.toString().trimmed();
        if (texto.size() > 10) {
            return QDateTime::fromString(texto, Qt::ISODateWithMs);
        }
    }
    return {};
}

} // namespace

QString FormatoFechas::fecha(const QDate& dia)
{
    return dia.isValid() ? QStringLiteral("%1 %2").arg(diaMes(dia)).arg(dia.year()) : QString();
}

QString FormatoFechas::fechaHora(const QDateTime& instante)
{
    if (!instante.isValid()) {
        return {};
    }
    const QDateTime local = instante.toLocalTime();
    return QStringLiteral("%1, %2").arg(fecha(local.date()), local.time().toString(QStringLiteral("HH:mm")));
}

QString FormatoFechas::rango(const QDate& inicial, const QDate& final_)
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

QDate FormatoFechas::diaDesde(const QVariant& valor)
{
    if (valor.metaType() == QMetaType::fromType<QDate>()) {
        return valor.toDate();
    }
    if (valor.metaType() == QMetaType::fromType<QString>()) {
        const QString texto = valor.toString().trimmed();
        return QDate::fromString(texto.left(10), Qt::ISODate);
    }
    if (valor.metaType() == QMetaType::fromType<QDateTime>()) {
        // Un QDate que pasa por QML llega como Date a medianoche UTC; un dia
        // construido en local llega a medianoche local. Ninguno debe cambiar
        // de dia por la zona horaria.
        const QDateTime instante = valor.toDateTime();
        const QDateTime utc = instante.toUTC();
        if (utc.time() == QTime(0, 0)) {
            return utc.date();
        }
        return instante.toLocalTime().date();
    }
    return {};
}

QString FormatoFechas::fecha(const QVariant& valor) const
{
    return fecha(diaDesde(valor));
}

QString FormatoFechas::fechaHora(const QVariant& valor) const
{
    return fechaHora(instanteDesde(valor));
}

QString FormatoFechas::rango(const QVariant& inicial, const QVariant& final_) const
{
    return rango(diaDesde(inicial), diaDesde(final_));
}

} // namespace satcfdi
