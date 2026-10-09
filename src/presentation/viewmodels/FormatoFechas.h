#pragma once

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

// Fechas visibles en formato legible es_MX (T013 D5). La captura sigue en
// AAAA-MM-DD; esto es solo presentacion.
//
//   fecha:     "3 sep 2026"
//   fechaHora: "1 oct 2026, 10:03"  (hora local del equipo, 24 h)
//   rango:     "3 sep 2026" (mismo dia), "1–30 sep 2026" (mismo mes),
//              "28 sep – 2 oct 2026" (mismo ano), "28 dic 2026 – 2 ene 2027"
//
// Las fechas SIN hora (QDate o texto "AAAA-MM-DD", como las exponen los view
// models) se formatean tal cual, sin conversion de zona: un dia nunca se
// desplaza. Las fechas CON hora (QDateTime, normalmente UTC) se muestran en la
// hora local. Los meses van abreviados en espanol fijo (ene..dic), sin
// depender del locale del sistema. Valor nulo o invalido -> "".
//
// En QML: singleton `FormatoFechas` del modulo (FormatoFechas.fecha(...)).
// En C++: las funciones estaticas.
class FormatoFechas : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    static QString fecha(const QDate& dia);
    static QString fechaHora(const QDateTime& instante);
    static QString rango(const QDate& inicial, const QDate& final_);

    // Acepta QDate, QDateTime o texto ISO ("AAAA-MM-DD" se trata como dia sin
    // hora; un texto con hora se trata como instante).
    Q_INVOKABLE QString fecha(const QVariant& valor) const;
    Q_INVOKABLE QString fechaHora(const QVariant& valor) const;
    Q_INVOKABLE QString rango(const QVariant& inicial, const QVariant& final_) const;

    // Dia sin hora a partir de un QVariant (invalido si no aplica).
    static QDate diaDesde(const QVariant& valor);
};

} // namespace satcfdi
