#pragma once

#include "ExtensionCicloDeVida.h"
#include "presentation/viewmodels/AccionesSolicitud.h"
#include "ports/OSIntegration.h"

namespace satcfdi {

class OperacionExecutor;
class WorkerLocal;
struct InstantaneaWorker;

// Traduce el estado del worker (application) al vocabulario del puerto
// OSIntegration (D2, D11).
OSIntegration::EstadoMonitoreo estadoMonitoreoDe(const InstantaneaWorker& instantanea);

// ExtensionCicloDeVida sobre WorkerLocal + OperacionExecutor (T007 D1).
// - aplicarMonitoreoPausado: el worker ya observa la configuracion confirmada
//   (pausa); al REANUDAR se fuerza un ciclo para consumir intenciones.
// - detener: worker.detener() (timers, no encola mas) y despues
//   ejecutor.detener(plazo): espera la operacion actual hasta el plazo, pide
//   la cancelacion cooperativa y termina su hilo. No bloquea.
class ExtensionWorker final : public ExtensionCicloDeVida {
public:
    ExtensionWorker(WorkerLocal& worker, OperacionExecutor& ejecutor);

    void aplicarMonitoreoPausado(bool pausado) override;
    QFuture<void> detener() override;

private:
    WorkerLocal& m_worker;
    OperacionExecutor& m_ejecutor;
    bool m_conocido = false;
    bool m_pausado = false;
};

// AccionesSolicitud (presentacion) sobre WorkerLocal: solo encola.
class AccionesWorker final : public AccionesSolicitud {
public:
    explicit AccionesWorker(WorkerLocal& worker);

    void enviar(const SolicitudId& id) override;
    void verificarAhora(const SolicitudId& id) override;
    void reintentarDescarga(const SolicitudId& id) override;

private:
    WorkerLocal& m_worker;
};

} // namespace satcfdi
