#include "IconoSvg.h"

#include <QFile>
#include <QPainter>
#include <QQuickWindow>
#include <QSvgRenderer>
#include <QtMath>

namespace satcfdi {

IconoSvg::IconoSvg(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

QString IconoSvg::rutaDe(const QUrl& url)
{
    if (url.scheme() == QLatin1String("qrc")) {
        return QLatin1Char(':') + url.path();
    }
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    return url.toString();
}

void IconoSvg::setFuente(const QUrl& fuente)
{
    if (m_fuente == fuente) {
        return;
    }
    m_fuente = fuente;
    m_svg.clear();
    m_valido = false;
    if (!fuente.isEmpty()) {
        QFile archivo(rutaDe(fuente));
        if (archivo.open(QIODevice::ReadOnly)) {
            m_svg = archivo.readAll();
            m_valido = QSvgRenderer(m_svg).isValid();
        }
        if (!m_valido) {
            qWarning("IconoSvg: no se pudo cargar %s", qPrintable(fuente.toString()));
            m_svg.clear();
        }
    }
    emit fuenteChanged();
    update();
}

void IconoSvg::setColor(const QColor& color)
{
    if (m_color == color) {
        return;
    }
    m_color = color;
    emit colorChanged();
    update();
}

void IconoSvg::paint(QPainter* painter)
{
    if (!m_valido || width() <= 0 || height() <= 0) {
        return;
    }
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const QSize tamano(qCeil(width() * dpr), qCeil(height() * dpr));
    QImage imagen(tamano, QImage::Format_ARGB32_Premultiplied);
    imagen.fill(Qt::transparent);
    {
        QPainter p(&imagen);
        p.setRenderHint(QPainter::Antialiasing);
        QSvgRenderer renderer(m_svg);
        renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(tamano)));
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(imagen.rect(), m_color);
    }
    imagen.setDevicePixelRatio(dpr);
    painter->drawImage(QRectF(0, 0, width(), height()), imagen);
}

} // namespace satcfdi
