#pragma once

// Reloj logico y Programador falsos (T007): las pruebas avanzan el tiempo sin
// esperar tiempo real (verificacion 3 de la tarea). El programador es solo del
// hilo grafico; el reloj es seguro entre hilos.
//   FakeReloj reloj(inicio);
//   FakeProgramador programador(reloj);
//   servicio(..., reloj.funcion(), programador);
//   programador.avanzar(std::chrono::minutes(10)); // ejecuta lo debido, en orden

#include "application/operaciones/Programador.h"
#include "application/persistence/PuertosPersistencia.h"

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <chrono>
#include <map>
#include <tuple>

namespace fakes {

// Thread-safe: el OperacionExecutor lo lee desde su hilo mientras la prueba
// lo avanza desde el hilo grafico.
class FakeReloj {
public:
    explicit FakeReloj(QDateTime inicioUtc) : m_ahora(std::move(inicioUtc)) {}
    QDateTime ahora() const
    {
        QMutexLocker l(&m_mutex);
        return m_ahora;
    }
    void fijar(QDateTime instante)
    {
        QMutexLocker l(&m_mutex);
        m_ahora = std::move(instante);
    }
    satcfdi::RelojUtc funcion()
    {
        return [this]() { return ahora(); };
    }

private:
    mutable QMutex m_mutex;
    QDateTime m_ahora;
};

class FakeProgramador final : public satcfdi::Programador {
public:
    explicit FakeProgramador(FakeReloj& reloj) : m_reloj(reloj) {}

    Id programar(const QDateTime& instanteUtc, std::function<void()> accion) override
    {
        const Id id = ++m_ultimoId;
        m_pendientes.emplace(std::make_tuple(instanteUtc, id), std::move(accion));
        return id;
    }

    void cancelar(Id id) override
    {
        for (auto it = m_pendientes.begin(); it != m_pendientes.end(); ++it) {
            if (std::get<1>(it->first) == id) {
                m_pendientes.erase(it);
                return;
            }
        }
    }

    void cancelarTodo() override { m_pendientes.clear(); }

    // Avanza el reloj hasta `instante` ejecutando, en orden (instante, id), las
    // acciones debidas; las que estas programen y ya esten debidas tambien.
    // Devuelve cuantas ejecuto.
    int avanzarHasta(const QDateTime& instante)
    {
        int ejecutadas = 0;
        while (!m_pendientes.empty() && std::get<0>(m_pendientes.begin()->first) <= instante) {
            auto nodo = m_pendientes.extract(m_pendientes.begin());
            m_reloj.fijar(std::max(m_reloj.ahora(), std::get<0>(nodo.key())));
            nodo.mapped()();
            ++ejecutadas;
        }
        m_reloj.fijar(std::max(m_reloj.ahora(), instante));
        return ejecutadas;
    }
    int avanzar(std::chrono::milliseconds delta) { return avanzarHasta(m_reloj.ahora().addMSecs(delta.count())); }

    int pendientes() const { return static_cast<int>(m_pendientes.size()); }
    std::optional<QDateTime> proximo() const
    {
        return m_pendientes.empty() ? std::nullopt : std::optional(std::get<0>(m_pendientes.begin()->first));
    }

private:
    FakeReloj& m_reloj;
    Id m_ultimoId = 0;
    std::map<std::tuple<QDateTime, Id>, std::function<void()>> m_pendientes;
};

} // namespace fakes
