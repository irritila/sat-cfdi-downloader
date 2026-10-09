#pragma once

#include <QColor>
#include <QImage>
#include <QQuickPaintedItem>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

// Icono SVG monocromo tenido con un color exacto (T013 D4). Base de
// Icono.qml; las pantallas usan Icono, no este tipo.
//
// Renderiza `fuente` (qrc:/... o :/...) con QSvgRenderer al tamano del item y
// la densidad de la pantalla, y pinta `color` solo donde hay trazo
// (composicion SourceIn): el color final es el token, sin efectos de
// Qt Quick ni dependencia del estilo de Controls. Un SVG inexistente o
// invalido deja el item vacio y emite un warning (las pruebas fallan).
class IconoSvg : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl fuente READ fuente WRITE setFuente NOTIFY fuenteChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    // Verdadero si `fuente` se cargo como SVG valido.
    Q_PROPERTY(bool valido READ valido NOTIFY fuenteChanged)

public:
    explicit IconoSvg(QQuickItem* parent = nullptr);

    QUrl fuente() const { return m_fuente; }
    QColor color() const { return m_color; }
    bool valido() const { return m_valido; }

    void setFuente(const QUrl& fuente);
    void setColor(const QColor& color);

    void paint(QPainter* painter) override;

signals:
    void fuenteChanged();
    void colorChanged();

private:
    static QString rutaDe(const QUrl& url);

    QUrl m_fuente;
    QColor m_color = Qt::black;
    bool m_valido = false;
    QByteArray m_svg;
};

} // namespace satcfdi
