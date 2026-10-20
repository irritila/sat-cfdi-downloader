#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringView>
#include <QTimeZone>

#include <compare>
#include <optional>

namespace satcfdi {

// Mes natural del flujo guiado (T023.2, ADR 0019). Valor inmutable y valido:
// solo se obtiene con crear(), desdeTexto() o de(). Persistido como "YYYY-MM".
// Dominio: 0001-01 a 9999-12 (kAnioMinimo..kAnioMaximo). Las operaciones que
// podrian salir de el (de(), anterior(), siguiente()) devuelven nullopt en
// lugar de un mes invalido.
class MesCalendario {
public:
    static constexpr int kAnioMinimo = 1;
    static constexpr int kAnioMaximo = 9999;

    // anio kAnioMinimo..kAnioMaximo, mes 1..12; nullopt fuera de rango.
    static std::optional<MesCalendario> crear(int anio, int mes);
    // Exactamente "YYYY-MM".
    static std::optional<MesCalendario> desdeTexto(QStringView texto);
    // Mes de una fecha civil; nullopt si es invalida o fuera del dominio.
    static std::optional<MesCalendario> de(QDate fecha);

    int anio() const noexcept { return m_anio; }
    int mes() const noexcept { return m_mes; }

    QString texto() const;           // "YYYY-MM"
    QDate primerDia() const;
    QDate ultimoDia() const;
    // enero -> diciembre del anio previo; nullopt en 0001-01.
    std::optional<MesCalendario> anterior() const;
    // diciembre -> enero del anio siguiente; nullopt en 9999-12.
    std::optional<MesCalendario> siguiente() const;

    // Primer y ultimo segundo del mes como fecha SAT (hora Centro sin offset):
    // "YYYY-MM-01T00:00:00" y "YYYY-MM-<ultimo>T23:59:59".
    QString inicioSat() const;
    QString finSat() const;

    friend bool operator==(const MesCalendario&, const MesCalendario&) = default;
    friend auto operator<=>(const MesCalendario&, const MesCalendario&) = default;

private:
    MesCalendario(int anio, int mes) : m_anio(anio), m_mes(mes) {}

    int m_anio = 1;
    int m_mes = 1;
};

// Hora Centro de Mexico (D19). Conversiones PURAS: reciben el instante ya
// capturado por un reloj inyectable; ninguna funcion lee el reloj del sistema
// ni depende de la zona horaria del proceso.
//
// "Reloj de pared": QDateTime en QTimeZone::UTC usado como fecha y hora civil
// SIN zona (la misma convencion que las fechas SAT `YYYY-MM-DDThh:mm:ss`).
// Asi las comparaciones y el "segundo siguiente" son exactos y no dependen de
// reglas de horario de verano.
namespace centro {

// QTimeZone("America/Mexico_City"). Si la base IANA del sistema no la tiene,
// UTC-6 fijo (regla vigente desde octubre de 2022).
QTimeZone zona();

// Instante (cualquier zona; normalmente UTC del reloj) -> reloj de pared Centro.
QDateTime aParedCentro(const QDateTime& instante);

// Fecha civil de "hoy" en Centro para el instante dado.
QDate hoy(const QDateTime& instante);

// Mes en curso en Centro; nullopt si el instante cae fuera del dominio de
// MesCalendario.
std::optional<MesCalendario> mesEnCurso(const QDateTime& instante);

// Mes inicial por omision del flujo guiado: el mes anterior al mes en curso
// (en enero, diciembre del anio previo); nullopt fuera del dominio.
std::optional<MesCalendario> mesInicialPorOmision(const QDateTime& instante);

// Reloj de pared -> fecha SAT "YYYY-MM-DDThh:mm:ss" (sin milisegundos).
QString fechaSat(const QDateTime& pared);

// Fecha SAT exacta "YYYY-MM-DDThh:mm:ss" -> reloj de pared; nullopt si no
// tiene ese formato o no es una fecha valida.
std::optional<QDateTime> desdeFechaSat(QStringView texto);

} // namespace centro

} // namespace satcfdi
