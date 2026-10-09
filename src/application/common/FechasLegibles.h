#pragma once

#include <QDate>
#include <QString>

namespace satcfdi::fechaslegibles {

// Fechas legibles es_MX sin hora (T013 D5), equivalentes a FormatoFechas de
// presentacion para que la aplicacion (notificaciones) no dependa de ella.
// Meses abreviados fijos (ene..dic), sin locale ni conversion de zona.
//   fecha: "3 sep 2026"
//   rango: "3 sep 2026" (mismo dia), "1–30 sep 2026" (mismo mes),
//          "28 sep – 2 oct 2026" (mismo ano), "28 dic 2026 – 2 ene 2027"
// Fecha invalida -> "" (en rango, la otra fecha si es valida).
QString fecha(const QDate& dia);
QString rango(const QDate& inicial, const QDate& final_);

// Dia de un texto SAT "yyyy-MM-ddTHH:mm:ss" o "yyyy-MM-dd" (sin zona).
QDate diaDeTextoSat(const QString& texto);

} // namespace satcfdi::fechaslegibles
