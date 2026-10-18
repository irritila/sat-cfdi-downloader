#include "CuadroApp.h"

#include <QPainter>
#include <QPainterPath>

namespace video {

CuadroApp::CuadroApp(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setOpaquePainting(false);
}

void CuadroApp::setImagen(const QImage& imagen)
{
    m_imagen = imagen;
    update();
}

void CuadroApp::setRadio(qreal radio)
{
    if (!qFuzzyCompare(m_radio, radio)) {
        m_radio = radio;
        emit radioChanged();
        update();
    }
}

void CuadroApp::paint(QPainter* painter)
{
    if (m_imagen.isNull()) {
        return;
    }
    const QRectF r = boundingRect();
    // La captura a escala exacta: 1920x1080 px en 960x540 logicos (el
    // painter ya trae la escala del devicePixelRatio).
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->drawImage(r, m_imagen);
    // Esquinas inferiores redondeadas: se borran (transparentes, con
    // antialiasing) las areas fuera del contorno; el fondo del lienzo se ve.
    const qreal d = 2 * m_radio;
    QPainterPath esquinas;
    esquinas.addRect(QRectF(r.left(), r.bottom() - m_radio, m_radio, m_radio));
    esquinas.addRect(QRectF(r.right() - m_radio, r.bottom() - m_radio, m_radio, m_radio));
    QPainterPath curvas;
    curvas.addEllipse(QRectF(r.left(), r.bottom() - d, d, d));
    curvas.addEllipse(QRectF(r.right() - d, r.bottom() - d, d, d));
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setCompositionMode(QPainter::CompositionMode_DestinationOut);
    painter->fillPath(esquinas.subtracted(curvas), Qt::black);
}

} // namespace video
