#include "EntornoQml.h"

#include "Guion.h"

#include <QEvent>
#include <QGuiApplication>
#include <QQuickItem>
#include <QScreen>
#include <QUrl>

namespace video {

void RelojCuadros::avanzarCuadro()
{
    ++m_cuadros;
    advance();
}

qint64 RelojCuadros::elapsed() const
{
    return m_cuadros * 1000 / kFps;
}

QUrl InterceptorOffline::intercept(const QUrl& url, DataType)
{
    const QString esquema = url.scheme();
    if (esquema.isEmpty() || esquema == QLatin1String("file") || esquema == QLatin1String("qrc")
        || esquema == QLatin1String("data") || esquema == QLatin1String("image")) {
        return url;
    }
    bloqueadas.append(esquema + QStringLiteral("://") + url.host());
    return QUrl(QStringLiteral("qrc:/satcfdi-video/red-bloqueada"));
}

bool FiltroEntradaReal::eventFilter(QObject* objeto, QEvent* e)
{
    // T022 D7: si otra app toma el foco de macOS, Qt envia WindowDeactivate
    // (NO espontaneo) y Qt Quick suelta el agarre del raton: un clic en curso
    // pierde su release. La grabacion nunca lo envia y su foco no depende del
    // sistema (asegurarActiva), asi que se descarta siempre.
    if (e->type() == QEvent::WindowDeactivate && descartarDesactivacion) {
        return true;
    }
    if (e->spontaneous()) {
        switch (e->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::HoverEnter:
        case QEvent::HoverLeave:
        case QEvent::HoverMove:
        case QEvent::Enter:
        case QEvent::Leave:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ShortcutOverride:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::NativeGesture:
        case QEvent::FocusOut:
        case QEvent::WindowDeactivate:
            return true;
        default:
            break;
        }
    }
    return QObject::eventFilter(objeto, e);
}

QScreen* pantallaGrabacion()
{
    QScreen* mejor = QGuiApplication::primaryScreen();
    for (QScreen* s : QGuiApplication::screens()) {
        if (mejor == nullptr || s->devicePixelRatio() > mejor->devicePixelRatio()) {
            mejor = s;
        }
    }
    return mejor;
}

QQuickItem* buscar(QQuickItem* raiz, const QString& nombre)
{
    if (raiz == nullptr || !raiz->isVisible()) {
        return nullptr;
    }
    if (raiz->objectName() == nombre) {
        return raiz;
    }
    for (QQuickItem* hijo : raiz->childItems()) {
        if (QQuickItem* r = buscar(hijo, nombre)) {
            return r;
        }
    }
    return nullptr;
}

QQuickItem* buscarRuta(QQuickItem* raiz, const QString& ruta)
{
    QQuickItem* actual = raiz;
    for (const QString& nombre : ruta.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        actual = buscar(actual, nombre);
        if (actual == nullptr) {
            return nullptr;
        }
    }
    return actual;
}

} // namespace video
