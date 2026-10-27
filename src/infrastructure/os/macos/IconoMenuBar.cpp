#include "infrastructure/os/macos/IconoMenuBar.h"

#include <QPainter>
#include <QSvgRenderer>

namespace satcfdi::macos {

QString rutaSvgIconoMenuBar(OSIntegration::EstadoIcono estado, int lado)
{
    using E = OSIntegration::EstadoIcono;
    QString sufijo;
    switch (estado) {
    case E::Normal:
        break;
    case E::Trabajando:
        sufijo = QStringLiteral("-trabajando");
        break;
    case E::Pausado:
        sufijo = QStringLiteral("-pausa");
        break;
    case E::Atencion:
        sufijo = QStringLiteral("-atencion");
        break;
    }
    const int base = lado > 18 ? 36 : 18;
    return QStringLiteral(":/menubar/menu-%1%2.svg").arg(base).arg(sufijo);
}

QImage dibujarIconoMenuBar(OSIntegration::EstadoIcono estado, int lado)
{
    QSvgRenderer renderer(rutaSvgIconoMenuBar(estado, lado));
    if (!renderer.isValid() || lado <= 0) {
        return {};
    }
    QImage imagen(lado, lado, QImage::Format_ARGB32_Premultiplied);
    imagen.fill(Qt::transparent);
    QPainter pintor(&imagen);
    pintor.setRenderHint(QPainter::Antialiasing);
    renderer.render(&pintor, QRectF(0, 0, lado, lado));
    pintor.end();
    return imagen;
}

} // namespace satcfdi::macos
