#include "AppCompositionRoot.h"

#include "AppLifecycleController.h"
#include "IntegracionWorker.h"
#include "ProgramadorQt.h"
#include "SingleInstanceCoordinator.h"
#include "application/configuration/ConfiguracionAppServicePersistido.h"
#include "application/logging/RegexLogSanitizer.h"
#include "application/operaciones/OperacionExecutor.h"
#include "application/operaciones/OperacionesSatNulo.h"
#include "application/operaciones/WorkerLocal.h"
#include "application/profiles/CredencialesSatServicePersistido.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/PerfilesSatServicePersistido.h"
#include "application/requests/SolicitudesServicePersistido.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QWindow>

#include <functional>

namespace satcfdi {

namespace {
Q_LOGGING_CATEGORY(lcMonitoreo, "satcfdi.monitoreo")
} // namespace

AppCompositionRoot::AppCompositionRoot(const QString& rutaBase, SecretStore& secretStore,
                                       OpcionesMonitoreo monitoreo)
    : m_persistencia(std::make_unique<SqlitePersistencia>(rutaBase))
    , m_dispatcher(std::make_unique<PersistenceDispatcher>())
    , m_sanitizer(std::make_unique<RegexLogSanitizer>())
{
    SqlitePersistencia& p = *m_persistencia;
    const PuertosPersistencia puertos{p.perfiles(), p.solicitudes(), p.paquetes(),
                                      p.logs(),     p.unidadDeTrabajo(), *m_sanitizer,
                                      &p.configuracion()};

    m_perfilesService = std::make_unique<PerfilesSatServicePersistido>(
        *m_dispatcher, p.perfiles(), p.unidadDeTrabajo());
    m_solicitudesService = std::make_unique<SolicitudesServicePersistido>(*m_dispatcher, puertos);
    m_configuracionService = std::make_unique<ConfiguracionAppServicePersistido>(
        *m_dispatcher, p.configuracion(), p.unidadDeTrabajo());
    m_credencialesService = std::make_unique<CredencialesSatServicePersistido>(
        *m_dispatcher, p.perfiles(), p.credenciales(), p.unidadDeTrabajo(), secretStore);
    // T005.1: la presentacion de perfiles y e.firma consume el mismo servicio
    // de credenciales persistido.
    // T007: ejecutor serial con su propio hilo y conexion SQLite (la de su
    // hilo en SqlitePersistencia), puerto SAT (nulo hasta T009) y worker.
    const RelojUtc reloj = monitoreo.reloj ? monitoreo.reloj : relojSistema();
    OperacionesSat* sat = monitoreo.operacionesSat;
    if (!sat) {
        m_operacionesSatPropio = std::make_unique<OperacionesSatNulo>();
        sat = m_operacionesSatPropio.get();
    }
    Programador* programadorEjecutor = monitoreo.programadorEjecutor;
    if (!programadorEjecutor) {
        m_programadorEjecutorPropio = std::make_unique<ProgramadorQt>(reloj);
        programadorEjecutor = m_programadorEjecutorPropio.get();
    }
    Programador* programadorWorker = monitoreo.programadorWorker;
    if (!programadorWorker) {
        m_programadorWorkerPropio = std::make_unique<ProgramadorQt>(reloj);
        programadorWorker = m_programadorWorkerPropio.get();
    }
    SqlitePersistencia* persistencia = m_persistencia.get();
    PuertosEjecutor puertosEjecutor{p.solicitudes(), p.logs(), p.operaciones(), p.unidadDeTrabajo(), *m_sanitizer,
                                    *sat, [persistencia] { persistencia->cerrarConexionDelHiloActual(); }};
    m_ejecutor = std::make_unique<OperacionExecutor>(std::move(puertosEjecutor), reloj, *programadorEjecutor);
    m_worker = std::make_unique<WorkerLocal>(*m_ejecutor, *m_configuracionService, *programadorWorker, reloj);
    m_extensionWorker = std::make_unique<ExtensionWorker>(*m_worker, *m_ejecutor);
    m_accionesWorker = std::make_unique<AccionesWorker>(*m_worker);

    // Los cambios aplicados por el ejecutor refrescan la UI por las senales
    // del servicio de solicitudes (lista y detalle).
    SolicitudesService* solicitudes = m_solicitudesService.get();
    QObject::connect(m_ejecutor.get(), &OperacionExecutor::solicitudActualizada, solicitudes,
                     [solicitudes](const SolicitudId& id) {
                         emit solicitudes->solicitudActualizada(id);
                         emit solicitudes->listaCambiada();
                     });

    // D9: cambio del estado de credencial observado por el gate del worker.
    // Diagnostico de app (solo la clave del estado; sin RFC ni perfil) y
    // evento para la UI: credencialCambio hace que PerfilesSatViewModel
    // reverifique ese perfil y que el selector de nueva solicitud se recargue.
    // La notificacion nativa la agrega T009.
    CredencialesSatService* credenciales = m_credencialesService.get();
    QObject::connect(m_ejecutor.get(), &OperacionExecutor::estadoCredencialCambiado, credenciales,
                     [credenciales](const PerfilId& perfil, EstadoCredencial estado) {
                         qCInfo(lcMonitoreo, "estado de credencial de un perfil cambio a %s",
                                qUtf8Printable(claveEstable(estado)));
                         emit credenciales->credencialCambio(perfil.texto());
                     });

    m_viewModels = std::make_unique<PresentacionViewModels>(
        m_solicitudesService.get(), m_perfilesService.get(), m_credencialesService.get());
    m_viewModels->setAccionesSolicitud(m_accionesWorker.get());

    // Primera tarea del dispatcher serial: limpia generaciones huerfanas del
    // SecretStore (residuos de fallos previos). No se espera aqui; las
    // operaciones de credencial posteriores se encolan detras.
    m_reconciliacionInicial = m_credencialesService->reconciliar();
}

AppCompositionRoot::~AppCompositionRoot()
{
    // 1. Consumidores, del mas externo al mas interno.
    m_engine.reset();
    m_controlador.reset();
    m_viewModels.reset();

    // T007: el ejecutor termina antes que el dispatcher (y su hilo cierra su
    // propia conexion). Si la salida explicita ya lo detuvo, es inmediato.
    m_accionesWorker.reset();
    m_extensionWorker.reset();
    if (m_worker) {
        m_worker->detener();
    }
    m_worker.reset();
    m_ejecutor.reset(); // ~OperacionExecutor: cancela, espera y cierra su conexion en su hilo
    m_programadorWorkerPropio.reset();
    m_programadorEjecutorPropio.reset();
    m_operacionesSatPropio.reset();

    m_credencialesService.reset();
    m_configuracionService.reset();
    m_solicitudesService.reset();
    m_perfilesService.reset();

    // 2. Cerrar la conexion de trabajo en su hilo propietario; cerrar() espera
    //    a que terminen las tareas encoladas (incluida esta) y une el hilo.
    SqlitePersistencia* persistencia = m_persistencia.get();
    m_dispatcher->despachar<void>(std::function<void()>(
        [persistencia] { persistencia->cerrarConexionDelHiloActual(); }));
    m_dispatcher->cerrar();
    m_dispatcher.reset();

    // 3. Repositorios y proveedor.
    m_sanitizer.reset();
    m_persistencia.reset();
}

bool AppCompositionRoot::cargar()
{
    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->setInitialProperties(m_viewModels->initialProperties());
    m_engine->loadFromModule("SatCfdiDownloader", "Main");
    return !m_engine->rootObjects().isEmpty();
}

void AppCompositionRoot::iniciarMonitoreo()
{
    if (m_monitoreoIniciado) {
        return;
    }
    m_monitoreoIniciado = true;
    m_worker->iniciar(); // recuperacion (D9) y primer ciclo
}

AppLifecycleController& AppCompositionRoot::iniciarCicloDeVida(OSIntegration& os,
                                                              SingleInstanceCoordinator* instancia,
                                                              std::function<void()> salida)
{
    Q_ASSERT(!m_controlador);
    QWindow* ventana = nullptr;
    if (m_engine && !m_engine->rootObjects().isEmpty()) {
        ventana = qobject_cast<QWindow*>(m_engine->rootObjects().constFirst());
    }
    m_controlador = std::make_unique<AppLifecycleController>(
        os, *m_configuracionService, *m_viewModels->app(), ventana);
    if (salida) {
        m_controlador->setSalida(std::move(salida));
    }
    // T007: pausa/reanudacion confirmadas y salida (D1) pasan por el worker.
    m_controlador->setExtension(m_extensionWorker.get());

    // Estado del worker -> menu bar (D2, D11): estado inicial y cada cambio.
    QObject::connect(m_worker.get(), &WorkerLocal::instantaneaCambiada, &os,
                     [&os](const InstantaneaWorker& i) { os.reflejarEstadoMonitoreo(estadoMonitoreoDe(i)); });

    m_controlador->iniciar();
    os.reflejarEstadoMonitoreo(estadoMonitoreoDe(m_worker->instantanea()));
    iniciarMonitoreo();
    if (instancia) {
        // Segunda apertura -> mostrar/enfocar; entrega lo encolado antes.
        QObject::connect(instancia, &SingleInstanceCoordinator::activacionSolicitada,
                         m_controlador.get(), &AppLifecycleController::mostrarVentana);
        instancia->habilitarEntrega();
    }
    return *m_controlador;
}

CredencialesSatService& AppCompositionRoot::credenciales() const
{
    return *m_credencialesService;
}

ConfiguracionAppService& AppCompositionRoot::configuracion() const
{
    return *m_configuracionService;
}

SolicitudesService& AppCompositionRoot::solicitudes() const
{
    return *m_solicitudesService;
}

PerfilesSatService& AppCompositionRoot::perfiles() const
{
    return *m_perfilesService;
}

} // namespace satcfdi
