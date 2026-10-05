#include "ProgramadorQt.h"

#include <algorithm>
#include <limits>

namespace satcfdi {

ProgramadorQt::ProgramadorQt(RelojUtc reloj)
    : m_reloj(reloj ? std::move(reloj) : relojSistema())
{
}

ProgramadorQt::~ProgramadorQt()
{
    cancelarTodo();
}

Programador::Id ProgramadorQt::programar(const QDateTime& instanteUtc, std::function<void()> accion)
{
    const Id id = ++m_siguiente;
    const qint64 ms = std::clamp<qint64>(m_reloj().msecsTo(instanteUtc), 0, std::numeric_limits<int>::max());
    auto* timer = new QTimer(); // vive en el hilo que llama
    timer->setSingleShot(true);
    timer->setTimerType(Qt::CoarseTimer);
    QObject::connect(timer, &QTimer::timeout, timer, [this, id, timer, accion = std::move(accion)] {
        m_timers.remove(id);
        timer->deleteLater();
        if (accion) {
            accion();
        }
    });
    m_timers.insert(id, timer);
    timer->start(static_cast<int>(ms));
    return id;
}

void ProgramadorQt::cancelar(Id id)
{
    if (QPointer<QTimer> t = m_timers.take(id); t) {
        t->stop();
        t->deleteLater();
    }
}

void ProgramadorQt::cancelarTodo()
{
    const auto timers = m_timers;
    m_timers.clear();
    for (const QPointer<QTimer>& t : timers) {
        if (t) {
            t->stop();
            t->deleteLater();
        }
    }
}

} // namespace satcfdi
