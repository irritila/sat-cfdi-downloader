#pragma once

// OperacionesSat falso que, como SatGatewayProductivo, espera girando un
// QEventLoop LOCAL en el hilo del OperacionExecutor (T009, guardia de
// reentrada). Mientras gira, ese loop despacha lo encolado al hilo del
// ejecutor (procesar(), finalizarEnHilo()...). Sin sleeps ni timers: el loop
// sale con liberar() o con la cancelacion de la operacion (alSolicitar).
//
// - Registra el orden de verificaciones ("verificar:<id>"), las operaciones
//   activas a la vez y su maximo.
// - bloquearSiguiente(): la siguiente verificacion gira su loop hasta liberar()
//   o la cancelacion (-> FallaOperacion{AntesDeEnvio, cancelada}).
// - marcarDespacho(): encola en el hilo del ejecutor un marcador; cuando
//   corre, todo lo encolado antes ya se despacho (orden FIFO del hilo).

#include "application/operaciones/OperacionesSat.h"

#include <QEventLoop>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QStringList>

#include <atomic>
#include <functional>

namespace fakes {

class FakeOperacionesSatConEventLoop final : public satcfdi::OperacionesSat {
public:
    template <typename T>
    using R = satcfdi::Resultado<T, satcfdi::FallaOperacion>;

    void bloquearSiguiente() { m_bloquear = true; }
    // Se invoca (sin locks del fake) dentro del callback de cancelacion, en el
    // hilo que la solicita; p. ej. para reentrar al ejecutor durante el cierre.
    std::function<void()> alCancelar;
    bool girando() const { return m_girando.load(); }
    void liberar()
    {
        QMutexLocker l(&m_mutex);
        m_liberado = true;
        if (m_loop) {
            QMetaObject::invokeMethod(m_loop.data(), &QEventLoop::quit, Qt::QueuedConnection);
        }
    }
    // Marcador FIFO en el hilo del ejecutor (solo mientras gira el loop).
    void marcarDespacho()
    {
        QMutexLocker l(&m_mutex);
        if (m_loop) {
            QMetaObject::invokeMethod(m_loop.data(), [this] { m_despachado = true; }, Qt::QueuedConnection);
        }
    }
    bool despachado() const { return m_despachado.load(); }
    int maximoActivas() const { return m_maximo.load(); }
    QStringList llamadas() const
    {
        QMutexLocker l(&m_mutex);
        return m_llamadas;
    }

    R<satcfdi::ResultadoVerificacion> verificar(const satcfdi::ContextoVerificacion& c) override
    {
        Activa a(*this);
        {
            QMutexLocker l(&m_mutex);
            m_llamadas.append(QStringLiteral("verificar:") + c.solicitudId.texto());
        }
        if (m_bloquear.exchange(false)) {
            QEventLoop loop;
            {
                QMutexLocker l(&m_mutex);
                m_loop = &loop;
                m_liberado = false;
            }
            QPointer<QEventLoop> destino(&loop);
            c.cancelacion.alSolicitar([this, destino] {
                if (alCancelar) {
                    alCancelar();
                }
                QMutexLocker l(&m_mutex);
                if (m_loop && m_loop == destino) {
                    QMetaObject::invokeMethod(m_loop.data(), &QEventLoop::quit, Qt::QueuedConnection);
                }
            });
            m_girando = true;
            if (!c.cancelacion.solicitada()) {
                loop.exec();
            }
            m_girando = false;
            {
                QMutexLocker l(&m_mutex);
                m_loop = nullptr;
            }
            if (c.cancelacion.solicitada()) {
                satcfdi::FallaOperacion f =
                    satcfdi::FallaOperacion::de(satcfdi::FaseOperacion::AntesDeEnvio, std::nullopt, QStringLiteral("cancelada"));
                f.cancelada = true;
                return R<satcfdi::ResultadoVerificacion>::fallo(f);
            }
        }
        satcfdi::ResultadoVerificacion v;
        v.estadoSolicitudSat = satcfdi::EstadoSolicitudSat::EnProceso;
        return R<satcfdi::ResultadoVerificacion>::exito(v);
    }

    R<satcfdi::ResultadoEnvio> enviar(const satcfdi::ContextoEnvio&) override
    {
        Activa a(*this);
        return R<satcfdi::ResultadoEnvio>::exito({QStringLiteral("5000"), {}, QStringLiteral("SAT-1")});
    }
    R<satcfdi::ResultadoDescarga> descargar(const satcfdi::ContextoDescarga&) override
    {
        Activa a(*this);
        return R<satcfdi::ResultadoDescarga>::fallo(
            satcfdi::FallaOperacion::de(satcfdi::FaseOperacion::Preparacion, QStringLiteral("no_aplica")));
    }
    R<satcfdi::ResultadoArchivoFinal> existeArchivoFinal(const satcfdi::ContextoArchivoFinal&) override
    {
        return R<satcfdi::ResultadoArchivoFinal>::exito({});
    }
    R<satcfdi::EstadoCredencial> obtenerEstadoCredencial(const satcfdi::PerfilId&) override
    {
        return R<satcfdi::EstadoCredencial>::exito(satcfdi::EstadoCredencial::Lista);
    }

private:
    struct Activa {
        explicit Activa(FakeOperacionesSatConEventLoop& f) : fake(f)
        {
            const int n = ++fake.m_activas;
            int previo = fake.m_maximo.load();
            while (n > previo && !fake.m_maximo.compare_exchange_weak(previo, n)) {
            }
        }
        ~Activa() { --fake.m_activas; }
        FakeOperacionesSatConEventLoop& fake;
    };

    mutable QMutex m_mutex;
    QPointer<QEventLoop> m_loop;
    bool m_liberado = false;
    QStringList m_llamadas;
    std::atomic<bool> m_bloquear{false};
    std::atomic<bool> m_girando{false};
    std::atomic<bool> m_despachado{false};
    std::atomic<int> m_activas{0};
    std::atomic<int> m_maximo{0};
};

} // namespace fakes
