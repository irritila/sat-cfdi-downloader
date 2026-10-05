#include "application/operaciones/SenalCancelacion.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace satcfdi {

struct SenalCancelacion::Estado {
    std::atomic<bool> solicitada{false};
    std::mutex mutex;
    std::condition_variable condicion;
    std::vector<std::function<void()>> notificaciones;
};

SenalCancelacion::SenalCancelacion()
    : m_estado(std::make_shared<Estado>())
{
}

void SenalCancelacion::solicitar() const
{
    std::vector<std::function<void()>> pendientes;
    {
        std::lock_guard<std::mutex> l(m_estado->mutex);
        if (m_estado->solicitada.exchange(true)) {
            return;
        }
        pendientes.swap(m_estado->notificaciones);
    }
    m_estado->condicion.notify_all();
    for (auto& n : pendientes) {
        n();
    }
}

bool SenalCancelacion::solicitada() const noexcept
{
    return m_estado->solicitada.load();
}

bool SenalCancelacion::esperar(std::chrono::milliseconds maximo) const
{
    std::unique_lock<std::mutex> l(m_estado->mutex);
    return m_estado->condicion.wait_for(l, maximo, [this] { return m_estado->solicitada.load(); });
}

void SenalCancelacion::alSolicitar(std::function<void()> notificar) const
{
    {
        std::lock_guard<std::mutex> l(m_estado->mutex);
        if (!m_estado->solicitada.load()) {
            m_estado->notificaciones.push_back(std::move(notificar));
            return;
        }
    }
    notificar();
}

} // namespace satcfdi
