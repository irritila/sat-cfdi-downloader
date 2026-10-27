#pragma once

// T020 (D8, D11): conversion de texto a trazos con la fuente versionada.
// Sustituye a fontTools (no disponible en el equipo): Qt da forma al texto
// (QTextLayout, kerning de la fuente) y QRawFont::pathForGlyph entrega el
// contorno de cada glifo. La salida no conserva ninguna referencia a la fuente.

#include <QList>
#include <QPainterPath>
#include <QRectF>
#include <QString>

namespace branding {

struct TextoATrazos {
    QList<QPainterPath> glifos; // un contorno por glifo, linea base en y = 0
    QRectF caja;                // caja de tinta de todos los glifos
    qreal alturaMayusculas = 0; // cap height en las mismas unidades
};

// Traza `texto` en una linea a `tamano` unidades por em, con `tracking` en
// fracciones de em (p. ej. -0.01). Falla (mensaje en `error`) si la fuente no
// carga o si Qt resolvio otra fuente distinta del archivo indicado.
bool trazarTexto(const QString& rutaFuente, const QString& texto, qreal tamano,
                 qreal tracking, TextoATrazos& salida, QString& error);

// Serializa un QPainterPath como atributo `d` de SVG (M, L, C, Z), con dos
// decimales como maximo.
QString datosRuta(const QPainterPath& ruta);

// Numero SVG con dos decimales como maximo (sin ceros finales ni "-0").
QString numeroSvg(qreal valor);

} // namespace branding
