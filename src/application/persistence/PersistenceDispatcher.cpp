#include "application/persistence/PersistenceDispatcher.h"

#include <QMetaObject>

namespace satcfdi {

PersistenceDispatcher::PersistenceDispatcher(QObject* parent)
    : QObject(parent)
    , m_contexto(new QObject)
{
    m_hilo.setObjectName(QStringLiteral("satcfdi-persistencia"));
    m_contexto->moveToThread(&m_hilo);
    // Patron documentado de Qt: el contexto se destruye en su hilo al terminar.
    connect(&m_hilo, &QThread::finished, m_contexto, &QObject::deleteLater);
    m_hilo.start();
}

PersistenceDispatcher::~PersistenceDispatcher()
{
    cerrar();
}

void PersistenceDispatcher::encolar(std::function<void()> trabajo)
{
    Q_ASSERT(QThread::currentThread() == thread());
    QMetaObject::invokeMethod(m_contexto, std::move(trabajo), Qt::QueuedConnection);
}

void PersistenceDispatcher::cerrar()
{
    if (m_cerrado) {
        return;
    }
    m_cerrado = true;
    // Encolado detras de las tareas pendientes: el hilo sale despues de
    // ejecutarlas todas.
    QThread* hilo = &m_hilo;
    encolar([hilo]() { hilo->quit(); });
    m_hilo.wait();
}

} // namespace satcfdi
