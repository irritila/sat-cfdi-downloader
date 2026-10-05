#include "IntegracionWorker.h"

#include "application/operaciones/OperacionExecutor.h"
#include "domain/paquetes/PaquetePersistido.h"
#include "application/operaciones/WorkerLocal.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"

#include <chrono>

namespace satcfdi {

OSIntegration::EstadoMonitoreo estadoMonitoreoDe(const InstantaneaWorker& instantanea)
{
    using E = OSIntegration::EstadoMonitoreo;
    E e;
    e.pendientes = instantanea.pendientes;
    switch (instantanea.estado) {
    case EstadoWorker::Pausado:
        e.fase = E::Fase::Pausado;
        break;
    case EstadoWorker::ActivoEnEspera:
        e.fase = E::Fase::ActivoEnEspera;
        break;
    case EstadoWorker::Ejecutando:
        e.fase = E::Fase::Ejecutando;
        e.actividad = E::Actividad::Otra;
        if (instantanea.tipo) {
            switch (*instantanea.tipo) {
            case TipoOperacion::Envio:
                e.actividad = E::Actividad::Enviando;
                break;
            case TipoOperacion::Verificacion:
                e.actividad = E::Actividad::Verificando;
                break;
            case TipoOperacion::Descarga:
                e.actividad = E::Actividad::Descargando;
                break;
            case TipoOperacion::VencimientoEstimado:
            case TipoOperacion::Recuperacion:
            case TipoOperacion::RegistroIntencion:
                break;
            }
        }
        break;
    case EstadoWorker::Deteniendo:
        e.fase = E::Fase::Deteniendo;
        break;
    case EstadoWorker::Detenido:
        e.fase = E::Fase::Detenido;
        break;
    }
    return e;
}

ExtensionWorker::ExtensionWorker(WorkerLocal& worker, OperacionExecutor& ejecutor)
    : m_worker(worker)
    , m_ejecutor(ejecutor)
{
}

void ExtensionWorker::aplicarMonitoreoPausado(bool pausado)
{
    const bool reanuda = m_conocido && m_pausado && !pausado;
    m_conocido = true;
    m_pausado = pausado;
    if (reanuda) {
        m_worker.ejecutarCiclo();
    }
}

QFuture<void> ExtensionWorker::detener()
{
    m_worker.detener();
    return m_ejecutor.detener(std::chrono::seconds(10));
}

ConsultaExistenciaEjecutor::ConsultaExistenciaEjecutor(PersistenceDispatcher& dispatcher,
                                                       PaqueteSolicitudRepository& paquetes,
                                                       OperacionExecutor& ejecutor)
    : m_dispatcher(dispatcher)
    , m_paquetes(paquetes)
    , m_ejecutor(ejecutor)
{
}

QFuture<ExistenciaPaquete> ConsultaExistenciaEjecutor::consultar(const SolicitudId& solicitud,
                                                                 const QString& idPaqueteSat)
{
    PaqueteSolicitudRepository* paquetes = &m_paquetes;
    OperacionExecutor* ejecutor = &m_ejecutor;
    QFuture<std::optional<QString>> ruta = m_dispatcher.despachar<std::optional<QString>>(
        [paquetes, solicitud, idPaqueteSat]() -> std::optional<QString> {
            auto lista = paquetes->listarVisiblesPorSolicitud(solicitud);
            if (!lista) {
                return std::nullopt;
            }
            for (const PaquetePersistido& p : lista.valor()) {
                if (p.idPaqueteSat == idPaqueteSat && p.rutaLocal && !p.rutaLocal->isEmpty()) {
                    return p.rutaLocal;
                }
            }
            return std::nullopt;
        });
    // La consulta al ejecutor se encola desde el hilo grafico (contexto).
    return ruta
        .then(ejecutor,
              [ejecutor](const std::optional<QString>& r) -> QFuture<ExistenciaPaquete> {
                  if (!r) {
                      return QtFuture::makeReadyValueFuture(ExistenciaPaquete::ErrorComprobacion);
                  }
                  return ejecutor->consultarExistencia(*r).then([](ExistenciaArchivo e) {
                      switch (e) {
                      case ExistenciaArchivo::Presente:
                          return ExistenciaPaquete::Presente;
                      case ExistenciaArchivo::NoEncontrado:
                          return ExistenciaPaquete::NoEncontrado;
                      case ExistenciaArchivo::ErrorComprobacion:
                          break;
                      }
                      return ExistenciaPaquete::ErrorComprobacion;
                  });
              })
        .unwrap();
}

AccionesWorker::AccionesWorker(WorkerLocal& worker)
    : m_worker(worker)
{
}

void AccionesWorker::enviar(const SolicitudId& id)
{
    m_worker.enviar(id);
}

void AccionesWorker::verificarAhora(const SolicitudId& id)
{
    m_worker.verificarAhora(id);
}

void AccionesWorker::reintentarDescarga(const SolicitudId& id)
{
    m_worker.reintentarDescarga(id);
}

} // namespace satcfdi
