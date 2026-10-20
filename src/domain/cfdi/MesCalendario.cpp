#include "domain/cfdi/MesCalendario.h"

#include <QTime>

namespace satcfdi {

namespace {

bool esDigito(QChar c)
{
    return c >= u'0' && c <= u'9';
}

} // namespace

std::optional<MesCalendario> MesCalendario::crear(int anio, int mes)
{
    if (anio < kAnioMinimo || anio > kAnioMaximo || mes < 1 || mes > 12) {
        return std::nullopt;
    }
    return MesCalendario(anio, mes);
}

std::optional<MesCalendario> MesCalendario::desdeTexto(QStringView texto)
{
    if (texto.size() != 7 || texto.at(4) != u'-') {
        return std::nullopt;
    }
    for (int i : {0, 1, 2, 3, 5, 6}) {
        if (!esDigito(texto.at(i))) {
            return std::nullopt;
        }
    }
    return crear(texto.first(4).toInt(), texto.sliced(5, 2).toInt());
}

std::optional<MesCalendario> MesCalendario::de(QDate fecha)
{
    if (!fecha.isValid()) {
        return std::nullopt;
    }
    return crear(fecha.year(), fecha.month());
}

QString MesCalendario::texto() const
{
    return QStringLiteral("%1-%2").arg(m_anio, 4, 10, QLatin1Char('0')).arg(m_mes, 2, 10, QLatin1Char('0'));
}

QDate MesCalendario::primerDia() const
{
    return QDate(m_anio, m_mes, 1);
}

QDate MesCalendario::ultimoDia() const
{
    const QDate primero = primerDia();
    return QDate(m_anio, m_mes, primero.daysInMonth());
}

std::optional<MesCalendario> MesCalendario::anterior() const
{
    return m_mes == 1 ? crear(m_anio - 1, 12) : crear(m_anio, m_mes - 1);
}

std::optional<MesCalendario> MesCalendario::siguiente() const
{
    return m_mes == 12 ? crear(m_anio + 1, 1) : crear(m_anio, m_mes + 1);
}

QString MesCalendario::inicioSat() const
{
    return primerDia().toString(u"yyyy-MM-dd") + QStringLiteral("T00:00:00");
}

QString MesCalendario::finSat() const
{
    return ultimoDia().toString(u"yyyy-MM-dd") + QStringLiteral("T23:59:59");
}

namespace centro {

QTimeZone zona()
{
    static const QTimeZone kZona = [] {
        QTimeZone z(QByteArrayLiteral("America/Mexico_City"));
        return z.isValid() ? z : QTimeZone::fromSecondsAheadOfUtc(-6 * 3600);
    }();
    return kZona;
}

QDateTime aParedCentro(const QDateTime& instante)
{
    Q_ASSERT(instante.isValid());
    const QDateTime local = instante.toTimeZone(zona());
    // Se descartan milisegundos: las fechas SAT tienen resolucion de segundos.
    return QDateTime(local.date(), QTime(local.time().hour(), local.time().minute(), local.time().second()),
                     QTimeZone(QTimeZone::UTC));
}

QDate hoy(const QDateTime& instante)
{
    return aParedCentro(instante).date();
}

std::optional<MesCalendario> mesEnCurso(const QDateTime& instante)
{
    return MesCalendario::de(hoy(instante));
}

std::optional<MesCalendario> mesInicialPorOmision(const QDateTime& instante)
{
    const auto actual = mesEnCurso(instante);
    return actual ? actual->anterior() : std::nullopt;
}

QString fechaSat(const QDateTime& pared)
{
    return pared.toString(u"yyyy-MM-ddTHH:mm:ss");
}

std::optional<QDateTime> desdeFechaSat(QStringView texto)
{
    if (texto.size() != 19 || texto.at(10) != u'T') {
        return std::nullopt;
    }
    // Fecha y hora por separado: QDateTime::fromString interpretaria el texto
    // en la zona del proceso (y ajustaria horas inexistentes por horario de
    // verano local).
    const QDate fecha = QDate::fromString(texto.first(10).toString(), u"yyyy-MM-dd");
    const QTime hora = QTime::fromString(texto.sliced(11).toString(), u"HH:mm:ss");
    if (!fecha.isValid() || !hora.isValid()) {
        return std::nullopt;
    }
    return QDateTime(fecha, hora, QTimeZone(QTimeZone::UTC));
}

} // namespace centro

} // namespace satcfdi
