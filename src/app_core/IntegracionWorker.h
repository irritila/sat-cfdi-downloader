#pragma once

#include "ExtensionCicloDeVida.h"
#include "presentation/viewmodels/AccionesSolicitud.h"
#include "presentation/viewmodels/ConsultaExistenciaPaquetes.h"
#include "application/notificaciones/Notificador.h"
#include "ports/OSIntegration.h"

#include <QPointer>

namespace satcfdi {

class OperacionExecutor;
class PaqueteSolicitudRepository;
class PersistenceDispatcher;
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
    void reintentarDescargaPaquete(const SolicitudId& id, const QString& idPaqueteSat) override;

private:
    WorkerLocal& m_worker;
};

// ConsultaExistenciaPaquetes (presentacion, T008 D11): lee ruta_local del
// paquete con PersistenceDispatcher (SQL fuera del hilo grafico) y consulta la
// existencia con OperacionExecutor::consultarExistencia (filesystem en el hilo
// del ejecutor). Solo lectura: no cambia estado ni escribe logs. Sin
// ruta_local, error de lectura, future cancelado o ejecutor detenido ->
// ErrorComprobacion.
class ConsultaExistenciaEjecutor final : public ConsultaExistenciaPaquetes {
public:
    ConsultaExistenciaEjecutor(PersistenceDispatcher& dispatcher, PaqueteSolicitudRepository& paquetes,
                               OperacionExecutor& ejecutor);

    QFuture<ExistenciaPaquete> consultar(const SolicitudId& solicitud, const QString& idPaqueteSat) override;

private:
    PersistenceDispatcher& m_dispatcher;
    PaqueteSolicitudRepository& m_paquetes;
    OperacionExecutor& m_ejecutor;
};

// Notificador (application, T009 D9) sobre OSIntegration::notificar. Sin
// OSIntegration (antes de iniciarCicloDeVida o en pruebas sin menu bar) no
// entrega nada. El resultado no vuelve a la aplicacion.
class NotificadorOS final : public Notificador {
public:
    void setOS(OSIntegration* os) { m_os = os; }
    void notificar(const Notificacion& n) override
    {
        if (m_os) {
            m_os->notificar(OSIntegration::NotificacionLocal{n.id, n.tipo, n.titulo, n.cuerpo});
        }
    }

private:
    QPointer<OSIntegration> m_os;
};

} // namespace satcfdi
