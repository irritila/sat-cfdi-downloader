#include "Trazos.h"

#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QRawFont>
#include <QTextLayout>

namespace branding {

namespace {

// Tamano de trabajo para dar forma al texto; luego se escala a `tamano`.
constexpr int kPixelesTrabajo = 1000;

QString numeroInterno(qreal valor)
{
    QString texto = QString::number(valor, 'f', 2);
    while (texto.contains(QLatin1Char('.')) && texto.endsWith(QLatin1Char('0'))) {
        texto.chop(1);
    }
    if (texto.endsWith(QLatin1Char('.'))) {
        texto.chop(1);
    }
    if (texto == QLatin1String("-0")) {
        texto = QStringLiteral("0");
    }
    return texto;
}

QString punto(const QPointF& p)
{
    return numeroInterno(p.x()) + QLatin1Char(' ') + numeroInterno(p.y());
}

} // namespace

QString numeroSvg(qreal valor)
{
    return numeroInterno(valor);
}

QString datosRuta(const QPainterPath& ruta)
{
    QStringList partes;
    bool abierta = false;
    for (int i = 0; i < ruta.elementCount(); ++i) {
        const QPainterPath::Element e = ruta.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            if (abierta) {
                partes << QStringLiteral("Z");
            }
            partes << QStringLiteral("M") + punto(e);
            abierta = true;
            break;
        case QPainterPath::LineToElement:
            partes << QStringLiteral("L") + punto(e);
            break;
        case QPainterPath::CurveToElement: {
            const QPainterPath::Element c2 = ruta.elementAt(i + 1);
            const QPainterPath::Element fin = ruta.elementAt(i + 2);
            partes << QStringLiteral("C") + punto(e) + QLatin1Char(' ') + punto(c2)
                          + QLatin1Char(' ') + punto(fin);
            i += 2;
            break;
        }
        case QPainterPath::CurveToDataElement:
            break;
        }
    }
    if (abierta) {
        partes << QStringLiteral("Z");
    }
    return partes.join(QString());
}

bool trazarTexto(const QString& rutaFuente, const QString& texto, qreal tamano,
                 qreal tracking, TextoATrazos& salida, QString& error)
{
    QFile archivo(rutaFuente);
    if (!archivo.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("no se pudo leer la fuente %1").arg(rutaFuente);
        return false;
    }
    const QByteArray datos = archivo.readAll();
    const QRawFont referencia(datos, kPixelesTrabajo, QFont::PreferNoHinting);
    if (!referencia.isValid()) {
        error = QStringLiteral("fuente invalida: %1").arg(rutaFuente);
        return false;
    }

    const int id = QFontDatabase::addApplicationFontFromData(datos);
    const QStringList familias = QFontDatabase::applicationFontFamilies(id);
    if (id < 0 || familias.isEmpty()) {
        error = QStringLiteral("Qt no registro la fuente %1").arg(rutaFuente);
        return false;
    }

    QFont fuente;
    fuente.setFamilies(familias);
    fuente.setStyleName(referencia.styleName());
    fuente.setWeight(static_cast<QFont::Weight>(referencia.weight()));
    fuente.setPixelSize(kPixelesTrabajo);
    fuente.setHintingPreference(QFont::PreferNoHinting);
    fuente.setStyleStrategy(QFont::NoFontMerging);
    fuente.setKerning(true);
    fuente.setLetterSpacing(QFont::AbsoluteSpacing, tracking * kPixelesTrabajo);

    QTextLayout layout(texto, fuente);
    layout.beginLayout();
    QTextLine linea = layout.createLine();
    linea.setLineWidth(1.0e7);
    layout.endLayout();

    const qreal escala = tamano / kPixelesTrabajo;
    const QByteArray cabeceraReferencia = referencia.fontTable("head");
    salida = {};
    qsizetype total = 0;
    for (const QGlyphRun& corrida : layout.glyphRuns()) {
        const QRawFont usada = corrida.rawFont();
        // La fuente resuelta debe ser exactamente el archivo versionado, no
        // una instalada en el sistema con el mismo nombre.
        if (usada.fontTable("head") != cabeceraReferencia) {
            error = QStringLiteral("Qt resolvio otra fuente (%1 %2)")
                        .arg(usada.familyName(), usada.styleName());
            return false;
        }
        const QList<quint32> indices = corrida.glyphIndexes();
        const QList<QPointF> posiciones = corrida.positions();
        for (qsizetype i = 0; i < indices.size(); ++i) {
            QPainterPath glifo = usada.pathForGlyph(indices.at(i));
            glifo.translate(posiciones.at(i).x(), posiciones.at(i).y() - linea.ascent());
            QTransform t;
            t.scale(escala, escala);
            glifo = t.map(glifo);
            if (!glifo.isEmpty()) {
                salida.caja = salida.caja.isNull() ? glifo.boundingRect()
                                                   : salida.caja.united(glifo.boundingRect());
                salida.glifos << glifo;
            }
            ++total;
        }
    }
    if (total != texto.size()) {
        error = QStringLiteral("se esperaban %1 glifos y Qt produjo %2")
                    .arg(texto.size())
                    .arg(total);
        return false;
    }
    salida.alturaMayusculas = referencia.capHeight() * escala;
    return true;
}

} // namespace branding
