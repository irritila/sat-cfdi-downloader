#pragma once

// Cuadro de la ventana de la app dentro del lienzo (T017 D21): pinta la
// imagen capturada de la ventana real (960x540 logicos a 2x) a escala 1, con
// las esquinas inferiores redondeadas (las superiores las pone la barra de
// titulo simulada). Sin reescalado: cada pixel de la captura es un pixel del
// lienzo, asi que cursor, onda y controles quedan donde la app los dibujo.

#include <QImage>
#include <QQuickPaintedItem>

namespace video {

class CuadroApp : public QQuickPaintedItem { // no final: qmlRegisterType la extiende
    Q_OBJECT
    Q_PROPERTY(qreal radio READ radio WRITE setRadio NOTIFY radioChanged)

public:
    explicit CuadroApp(QQuickItem* parent = nullptr);

    void setImagen(const QImage& imagen);
    qreal radio() const { return m_radio; }
    void setRadio(qreal radio);
    void paint(QPainter* painter) override;

signals:
    void radioChanged();

private:
    QImage m_imagen;
    qreal m_radio = 12;
};

} // namespace video
