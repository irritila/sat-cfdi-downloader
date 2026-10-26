#pragma once

#include "ExtensionCicloDeVida.h"
#include "presentation/viewmodels/AccionesSolicitud.h"
#include "presentation/viewmodels/ConsultaExistenciaPaquetes.h"
#include "application/estadoagregado/EstadoAgregado.h"
#include "application/notificaciones/Notificador.h"
#include "ports/OSIntegration.h"

#include <QFuture>
#include <QPointer>

#include <chrono>
#include <functional>

namespace satcfdi {

class OperacionExecutor;
class PaqueteSolicitudRepository;
class PersistenceDispatcher;
class WorkerLocal;
struct InstantaneaWorker;

// Traduce el estado del worker (application) al vocabulario del puerto
// OSIntegration (D2, D11).
OSIntegration::EstadoMonitoreo estadoMonitoreoDe(const InstantaneaWorker& instantanea);

// T026.2 (C5): future que se completa cuando `trabajo` termina (valor o
// cancelacion) o cuando pasa `plazo`, lo primero. Agotado el plazo registra
// un warning con solo `codigo` (ASCII estable, D20) y `trabajo` sigue en
// curso. Llamar desde un hilo con event loop (el temporizador vive en
// QCoreApplication); el future puede completarse en cualquier hilo.
QFuture<void> esperarConPlazo(QFuture<void> trabajo, std::chrono::milliseconds plazo, const char* codigo);

// T014.4 D2: EstadoAgregado (application) -> EstadoIcono (puerto), 1:1.
OSIntegration::EstadoIcono estadoIconoDe(EstadoAgregado estado);

// T014.4 D1: Notificacion (application) -> NotificacionLocal (puerto). El
// destino se copia tal cual (tipo + UUID interno); accionesExtra por tipo:
// [AbrirPerfiles] para "credencial" y "efirma_por_vencer" con destino Perfil;
// vacio en el resto. T024.1 (D4): ninguna notificacion nueva lleva
// [MostrarEnFinder]; la categoria sigue registrada en el adaptador y la
// activacion de una ya entregada sigue abriendo la carpeta (AppLifecycleController).
OSIntegration::NotificacionLocal notificacionLocalDe(const Notificacion& notificacion);

// ExtensionCicloDeVida sobre WorkerLocal + OperacionExecutor (T007 D1).
// - aplicarMonitoreoPausado: el worker ya observa la configuracion confirmada
//   (pausa); al REANUDAR se fuerza un ciclo para consumir intenciones.
// - detener: worker.detener() (timers, no encola mas) y despues
//   ejecutor.detener(plazo): espera la operacion actual hasta el plazo, pide
//   la cancelacion cooperativa y termina su hilo. No bloquea.
// - T026.2 (D19, C5): `detenerAdicional` (opcional) se llama en detener()
//   junto al ejecutor (la exportacion: cancela, descarta su `.partial` y une
//   su hilo) y se espera como maximo `plazoAdicional` (esperarConPlazo); el
//   future se completa cuando terminaron ambos o se agoto ese plazo. Agotado,
//   la salida sigue y el `.partial` lo borra la limpieza del siguiente
//   arranque (ADR 0023).
class ExtensionWorker final : public ExtensionCicloDeVida {
public:
    ExtensionWorker(WorkerLocal& worker, OperacionExecutor& ejecutor,
                    std::function<QFuture<void>()> detenerAdicional = {},
                    std::chrono::milliseconds plazoAdicional = std::chrono::seconds(10));

    void aplicarMonitoreoPausado(bool pausado) override;
    QFuture<void> detener() override;

private:
    WorkerLocal& m_worker;
    OperacionExecutor& m_ejecutor;
    std::function<QFuture<void>()> m_detenerAdicional;
    std::chrono::milliseconds m_plazoAdicional;
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
            m_os->notificar(notificacionLocalDe(n));
        }
    }

private:
    QPointer<OSIntegration> m_os;
};

} // namespace satcfdi
