#include "infrastructure/os/macos/IconoMenuBar.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace satcfdi::macos {

namespace {

// Centro y radio (rejilla de 18) de la insignia de estado.
// El recorte no toca la flecha de descarga (distancia minima ~4.4).
constexpr qreal kInsigniaX = 14.0;
constexpr qreal kInsigniaY = 14.0;
constexpr qreal kRecorte = 4.3;

QPen plumaBase(qreal e)
{
    QPen pluma(Qt::black, 1.4 * e);
    pluma.setJoinStyle(Qt::RoundJoin);
    pluma.setCapStyle(Qt::RoundCap);
    return pluma;
}

void dibujarDocumento(QPainter& p, qreal e)
{
    // Contorno del documento con esquina doblada.
    QPainterPath hoja;
    hoja.moveTo(4 * e, 2 * e);
    hoja.lineTo(11 * e, 2 * e);
    hoja.lineTo(14.5 * e, 5.5 * e);
    hoja.lineTo(14.5 * e, 16 * e);
    hoja.lineTo(4 * e, 16 * e);
    hoja.closeSubpath();
    p.drawPath(hoja);
    // Flecha de descarga.
    p.drawLine(QPointF(9.25 * e, 6 * e), QPointF(9.25 * e, 12.5 * e));
    p.drawLine(QPointF(6.75 * e, 10 * e), QPointF(9.25 * e, 12.5 * e));
    p.drawLine(QPointF(11.75 * e, 10 * e), QPointF(9.25 * e, 12.5 * e));
}

void recortarInsignia(QPainter& p, qreal e)
{
    p.save();
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawEllipse(QPointF(kInsigniaX * e, kInsigniaY * e), kRecorte * e, kRecorte * e);
    p.restore();
}

void dibujarPausa(QPainter& p, qreal e)
{
    p.drawLine(QPointF(12.6 * e, 11.8 * e), QPointF(12.6 * e, 16.2 * e));
    p.drawLine(QPointF(15.4 * e, 11.8 * e), QPointF(15.4 * e, 16.2 * e));
}

void dibujarTrabajando(QPainter& p, qreal e)
{
    // Arco de ~270 grados con punta de flecha (sincronizando).
    const qreal r = 2.5 * e;
    const QRectF caja(kInsigniaX * e - r, kInsigniaY * e - r, 2 * r, 2 * r);
    p.drawArc(caja, 90 * 16, 270 * 16);
    // Qt mide en sentido antihorario desde las 3: el arco va de arriba
    // (90) a la derecha (360) pasando por izquierda y abajo; en su extremo
    // final la direccion de avance es hacia arriba.
    const QPointF fin(kInsigniaX * e + r, kInsigniaY * e);
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    QPainterPath punta;
    punta.moveTo(fin + QPointF(0, -1.9 * e));
    punta.lineTo(fin + QPointF(-1.5 * e, 0.5 * e));
    punta.lineTo(fin + QPointF(1.5 * e, 0.5 * e));
    punta.closeSubpath();
    p.drawPath(punta);
    p.restore();
}

void dibujarAtencion(QPainter& p, qreal e)
{
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawEllipse(QPointF(kInsigniaX * e, kInsigniaY * e), 2.9 * e, 2.9 * e);
    p.restore();
}

} // namespace

QImage dibujarIconoMenuBar(OSIntegration::EstadoIcono estado, int lado)
{
    QImage imagen(lado, lado, QImage::Format_ARGB32_Premultiplied);
    imagen.fill(Qt::transparent);
    QPainter pintor(&imagen);
    pintor.setRenderHint(QPainter::Antialiasing);
    const qreal e = lado / 18.0;
    pintor.setPen(plumaBase(e));
    dibujarDocumento(pintor, e);

    using E = OSIntegration::EstadoIcono;
    switch (estado) {
    case E::Normal:
        break;
    case E::Pausado:
        recortarInsignia(pintor, e);
        dibujarPausa(pintor, e);
        break;
    case E::Trabajando:
        recortarInsignia(pintor, e);
        dibujarTrabajando(pintor, e);
        break;
    case E::Atencion:
        recortarInsignia(pintor, e);
        dibujarAtencion(pintor, e);
        break;
    }
    pintor.end();
    return imagen;
}

} // namespace satcfdi::macos
