#include "DiagnosticoClic.h"

#include "EntornoQml.h"

#include <QEvent>
#include <QGuiApplication>
#include <QMetaEnum>
#include <QMetaProperty>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>

#include <algorithm>

namespace video {

namespace {

QString nombreEvento(QEvent::Type t)
{
    const char* k = QMetaEnum::fromType<QEvent::Type>().valueToKey(t);
    return k != nullptr ? QString::fromLatin1(k) : QString::number(int(t));
}

QString punto(QPointF p)
{
    return QStringLiteral("(%1,%2)").arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 1);
}

QString rect(const QRectF& r)
{
    return QStringLiteral("[%1,%2 %3x%4]")
        .arg(r.x(), 0, 'f', 1)
        .arg(r.y(), 0, 'f', 1)
        .arg(r.width(), 0, 'f', 1)
        .arg(r.height(), 0, 'f', 1);
}

// Valor legible de una propiedad: el nombre del enum si lo es.
QString valor(const QObject* o, const char* propiedad)
{
    if (o == nullptr) {
        return QStringLiteral("(sin objeto)");
    }
    const int i = o->metaObject()->indexOfProperty(propiedad);
    if (i < 0) {
        return QStringLiteral("(sin propiedad)");
    }
    const QMetaProperty mp = o->metaObject()->property(i);
    const QVariant v = mp.read(o);
    if (mp.isEnumType()) {
        const char* k = mp.enumerator().valueToKey(v.toULongLong());
        return k != nullptr ? QString::fromLatin1(k) : QString::number(v.toInt());
    }
    return v.toString();
}

// pressed/down/hovered/activeFocus del item, solo las que existen.
QString banderas(const QQuickItem* item)
{
    QStringList partes;
    for (const char* p : {"pressed", "down", "hovered", "containsMouse", "checked"}) {
        if (item->metaObject()->indexOfProperty(p) >= 0) {
            partes.append(QStringLiteral("%1=%2").arg(QLatin1String(p), item->property(p).toBool() ? QStringLiteral("1")
                                                                                                    : QStringLiteral("0")));
        }
    }
    partes.append(QStringLiteral("activeFocus=%1").arg(item->hasActiveFocus() ? 1 : 0));
    return partes.join(QLatin1Char(' '));
}

qreal opacidadEfectiva(const QQuickItem* item)
{
    qreal o = 1;
    for (const QQuickItem* i = item; i != nullptr; i = i->parentItem()) {
        o *= i->opacity();
    }
    return o;
}

bool esDescendiente(const QQuickItem* item, const QQuickItem* ancestro)
{
    for (const QQuickItem* i = item; i != nullptr; i = i->parentItem()) {
        if (i == ancestro) {
            return true;
        }
    }
    return false;
}

// Busqueda en profundidad del item mas alto bajo `p` (escena): hijos en orden
// de pintado inverso (z, luego orden de declaracion), sin exigir que el padre
// contenga el punto salvo que recorte (clip), como la entrega de Qt Quick.
bool buscarAlto(QQuickItem* item, QPointF p, QQuickItem* excluir, QList<QQuickItem*>& camino)
{
    if (item == excluir || !item->isVisible()) {
        return false;
    }
    const bool contiene = item->contains(item->mapFromScene(p));
    if (item->clip() && !contiene) {
        return false;
    }
    camino.append(item);
    QList<QQuickItem*> hijos = item->childItems();
    std::stable_sort(hijos.begin(), hijos.end(), [](QQuickItem* a, QQuickItem* b) { return a->z() < b->z(); });
    for (auto it = hijos.crbegin(); it != hijos.crend(); ++it) {
        if (buscarAlto(*it, p, excluir, camino)) {
            return true;
        }
    }
    if (contiene) {
        return true;
    }
    camino.removeLast();
    return false;
}

} // namespace

DiagnosticoClic::DiagnosticoClic(const QString& ruta, QQuickWindow* ventana, QQuickItem* excluir)
    : m_ventana(ventana)
    , m_excluir(excluir)
{
    if (ruta.isEmpty()) {
        return;
    }
    m_archivo.setFileName(ruta);
    if (m_archivo.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        m_reloj.start();
        escribir(QStringLiteral("# diagnostico de clics T022: ms reales desde el inicio, cuadro, momento, datos"));
    }
}

QList<QQuickItem*> DiagnosticoClic::cadenaBajo(QQuickItem* raiz, QPointF punto, QQuickItem* excluir)
{
    QList<QQuickItem*> camino;
    if (raiz != nullptr) {
        buscarAlto(raiz, punto, excluir, camino);
    }
    return camino;
}

QString DiagnosticoClic::describir(const QQuickItem* item)
{
    if (item == nullptr) {
        return QStringLiteral("(ninguno)");
    }
    const QString clase = QString::fromLatin1(item->metaObject()->className());
    return item->objectName().isEmpty() ? clase : item->objectName() + QLatin1Char(':') + clase;
}

void DiagnosticoClic::escribir(const QString& linea)
{
    m_archivo.write(QStringLiteral("%1 c%2 ").arg(m_reloj.elapsed(), 7).arg(m_cuadro, 5).toUtf8());
    m_archivo.write(linea.toUtf8());
    m_archivo.write("\n");
    m_archivo.flush();
}

QString DiagnosticoClic::semantico() const
{
    QStringList partes;
    for (const Semantico& s : m_semantico) {
        const QObject* o = s.objectName.isEmpty() ? s.objeto.data()
                                                   : buscar(m_ventana->contentItem(), s.objectName);
        partes.append(s.etiqueta + QLatin1Char('=') + valor(o, s.propiedad));
    }
    return partes.join(QLatin1Char(' '));
}

void DiagnosticoClic::setContexto(const QString& escena, const QString& nombreObjetivo, QQuickItem* objetivo)
{
    soltarVigilancia();
    m_escena = escena;
    m_nombreObjetivo = nombreObjetivo;
    m_objetivo = objetivo;
}

void DiagnosticoClic::nota(int cuadro, const QString& texto)
{
    if (!activo()) {
        return;
    }
    m_cuadro = cuadro;
    escribir(QStringLiteral("[%1] %2").arg(m_escena, texto));
}

void DiagnosticoClic::instantanea(int cuadro, const QString& momento, QPointF p)
{
    if (!activo()) {
        return;
    }
    m_cuadro = cuadro;
    for (const QString& linea : estado(momento, p)) {
        escribir(linea);
    }
}

QStringList DiagnosticoClic::estado(const QString& momento, QPointF p) const
{
    QStringList l;
    l.append(QStringLiteral("[%1] %2 cursor=%3 objetivo='%4'").arg(m_escena, momento, punto(p), m_nombreObjetivo));
    QQuickItem* o = m_objetivo.data();
    if (o == nullptr) {
        l.append(QStringLiteral("  objetivo: (destruido o ausente)"));
    } else {
        const QRectF r = o->mapRectToScene(QRectF(0, 0, o->width(), o->height()));
        l.append(QStringLiteral("  objetivo: %1 rect=%2 contieneCursor=%3 visible=%4 enabled=%5 opacity=%6 opacidadEfectiva=%7 %8")
                     .arg(describir(o), rect(r))
                     .arg(o->contains(o->mapFromScene(p)) ? 1 : 0)
                     .arg(o->isVisible() ? 1 : 0)
                     .arg(o->isEnabled() ? 1 : 0)
                     .arg(o->opacity(), 0, 'f', 3)
                     .arg(opacidadEfectiva(o), 0, 'f', 3)
                     .arg(banderas(o)));
    }
    const QList<QQuickItem*> cadena = cadenaBajo(m_ventana->contentItem(), p, m_excluir);
    QStringList nombres;
    for (const QQuickItem* i : cadena) {
        nombres.append(describir(i));
    }
    const QQuickItem* alto = cadena.isEmpty() ? nullptr : cadena.constLast();
    l.append(QStringLiteral("  bajoCursor(z): %1").arg(nombres.join(QStringLiteral(" > "))));
    l.append(QStringLiteral("  itemMasAlto=%1 objetivoEnCadena=%2")
                 .arg(describir(alto))
                 .arg(o != nullptr && cadena.contains(o) ? 1 : 0));
    QQuickItem* grabber = m_ventana->mouseGrabberItem();
    l.append(QStringLiteral("  mouseGrabberItem=%1 grabberEsObjetivoODescendiente=%2 activeFocusItem=%3")
                 .arg(describir(grabber))
                 .arg(grabber != nullptr && o != nullptr && esDescendiente(grabber, o) ? 1 : 0)
                 .arg(describir(m_ventana->activeFocusItem())));
    l.append(QStringLiteral("  ventana: isActive=%1 focusWindowEsApp=%2 exposed=%3")
                 .arg(m_ventana->isActive() ? 1 : 0)
                 .arg(QGuiApplication::focusWindow() == m_ventana ? 1 : 0)
                 .arg(m_ventana->isExposed() ? 1 : 0));
    l.append(QStringLiteral("  semantico: %1").arg(semantico()));
    return l;
}

void DiagnosticoClic::vigilar()
{
    if (!activo()) {
        return;
    }
    soltarVigilancia();
    // Ultimo filtro instalado = primero en ejecutarse: ve tambien la entrada
    // real que FiltroEntradaReal descarta despues.
    m_ventana->installEventFilter(this);
    m_vigilandoVentana = true;
    if (QQuickItem* o = m_objetivo.data()) {
        o->installEventFilter(this);
        m_vigilado = o;
    }
}

void DiagnosticoClic::soltarVigilancia()
{
    if (m_vigilandoVentana) {
        m_ventana->removeEventFilter(this);
        m_vigilandoVentana = false;
    }
    if (QQuickItem* v = m_vigilado.data()) {
        v->removeEventFilter(this);
    }
    m_vigilado.clear();
}

bool DiagnosticoClic::eventFilter(QObject* objeto, QEvent* e)
{
    switch (e->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::UngrabMouse:
    case QEvent::HoverEnter:
    case QEvent::HoverLeave:
    case QEvent::HoverMove:
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate: {
        QString detalle;
        if (e->isSinglePointEvent()) {
            const auto* m = static_cast<QSinglePointEvent*>(e);
            detalle = QStringLiteral(" pos=%1 boton=%2 botones=%3 ts=%4")
                          .arg(punto(m->position()))
                          .arg(int(m->button()))
                          .arg(int(m->buttons()))
                          .arg(m->timestamp());
        }
        const QString destino = objeto == m_ventana ? QStringLiteral("ventana")
                                                     : describir(qobject_cast<QQuickItem*>(objeto));
        escribir(QStringLiteral("[%1]   evento %2 -> %3%4%5")
                     .arg(m_escena, nombreEvento(e->type()), destino, detalle,
                          e->spontaneous() ? QStringLiteral(" ESPONTANEO") : QString()));
        break;
    }
    default:
        break;
    }
    return false;
}

} // namespace video
