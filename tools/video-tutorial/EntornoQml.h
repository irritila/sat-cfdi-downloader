#pragma once

// Piezas del entorno de grabacion (T017 D4, D15): reloj por cuadro, bloqueo
// de URL no locales en QML, descarte de la entrada real y busqueda de
// controles por objectName.

#include <QAnimationDriver>
#include <QObject>
#include <QQmlAbstractUrlInterceptor>
#include <QStringList>

class QQuickItem;
class QScreen;

namespace video {

// Reloj de animaciones fijo: cada cuadro avanza exactamente 1/30 s.
class RelojCuadros final : public QAnimationDriver {
public:
    void avanzarCuadro();
    qint64 elapsed() const override;

private:
    qint64 m_cuadros = 0;
};

// D15: QML solo carga file:, qrc:, data: e image:; cualquier otro esquema se
// bloquea antes de llegar a la red. Es defensa adicional: el aislamiento de
// red lo da el sandbox de proceso (sin-red.sb).
class InterceptorOffline final : public QQmlAbstractUrlInterceptor {
public:
    QStringList bloqueadas;
    QUrl intercept(const QUrl& url, DataType tipo) override;
};

// Descarta la entrada REAL del usuario durante la grabacion: solo cuentan
// los eventos sinteticos del guion. Tambien WindowDeactivate, aunque Qt lo
// envie como no espontaneo (T022 D7).
class FiltroEntradaReal final : public QObject {
public:
    // Solo pruebas del grabador (T022): false deja pasar WindowDeactivate.
    bool descartarDesactivacion = true;
    bool eventFilter(QObject* objeto, QEvent* e) override;
};

QScreen* pantallaGrabacion();

// Primer control VISIBLE con ese objectName (busqueda en el arbol visual).
QQuickItem* buscar(QQuickItem* raiz, const QString& nombre);
// "padre/hijo": busca cada objectName dentro del anterior.
QQuickItem* buscarRuta(QQuickItem* raiz, const QString& ruta);

} // namespace video
