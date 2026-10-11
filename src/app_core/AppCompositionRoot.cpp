#include "AppCompositionRoot.h"

#include "AccesoFinder.h"
#include "AppLifecycleController.h"
#include "IntegracionWorker.h"
#include "ProgramadorQt.h"
#include "SingleInstanceCoordinator.h"
#include "application/configuration/ConfiguracionAppServicePersistido.h"
#include "application/logging/RegexLogSanitizer.h"
#include "application/operaciones/OperacionExecutor.h"
#include "application/notificaciones/ServicioNotificaciones.h"
#include "application/estadoagregado/MonitorEstadoAgregado.h"
#include "application/operaciones/OperacionesSatProductivo.h"
#include "application/paquetes/AccesoPaquetesService.h"
#include "application/primeruso/ConsultaPrimerUsoPersistida.h"
#include "application/profiles/ConsultaPreparacionPerfiles.h"
#include "application/vencimiento/AvisoVencimientoEFirma.h"
#include "application/operaciones/WorkerLocal.h"
#include "application/profiles/CredencialesSatServicePersistido.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/PerfilesSatServicePersistido.h"
#include "application/requests/SolicitudesServicePersistido.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"
#include "infrastructure/storage/FilesystemPackageStorage.h"
#include "infrastructure/sat/SatGatewayProductivo.h"
#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/CatalogoMensajes.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include <QDir>
#include <QFileInfo>
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
    // hilo en SqlitePersistencia), puerto SAT (productivo desde T009) y worker.
    const RelojUtc reloj = monitoreo.reloj ? monitoreo.reloj : relojSistema();
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
    // T008 D8: almacenamiento de ZIP. La raiz la crea AppBootstrapper al
    // arrancar, en su hilo de E/S (nunca en el hilo grafico); el adaptador
    // tambien la crea al primer guardado.
    m_packageStorage = monitoreo.packageStorage;
    if (!m_packageStorage) {
        m_raizPaquetes = monitoreo.raizPaquetes.isEmpty()
                             ? QDir(QFileInfo(rutaBase).absolutePath()).filePath(QStringLiteral("paquetes"))
                             : monitoreo.raizPaquetes;
        m_packageStoragePropio = std::make_unique<FilesystemPackageStorage>(m_raizPaquetes);
        m_packageStorage = m_packageStoragePropio.get();
    }

    // T009: SatGateway productivo (o inyectado). Se crea en el hilo grafico
    // pero SOLO se invoca desde el hilo del ejecutor (adaptador OperacionesSat).
    m_satGateway = monitoreo.satGateway;
    if (!m_satGateway) {
        SatGatewayOptions opciones;
        opciones.reloj = reloj;
        m_satGatewayPropio = std::make_unique<SatGatewayProductivo>(std::move(opciones));
        m_satGateway = m_satGatewayPropio.get();
    }

    // T009: OperacionesSat productivo (o inyectado). Se crea antes del
    // ejecutor y se destruye despues (su destructor cierra las sesiones). Un
    // cambio de credencial invalida la sesion de token de ese perfil.
    OperacionesSat* sat = monitoreo.operacionesSat;
    if (!sat) {
        auto productivo = std::make_unique<OperacionesSatProductivo>(*m_satGateway, *m_credencialesService,
                                                                     *m_packageStorage, reloj);
        OperacionesSatProductivo* operaciones = productivo.get();
        m_conexionInvalidarSesion = QObject::connect(m_credencialesService.get(), &CredencialesSatService::credencialCambio,
                         m_credencialesService.get(), [operaciones](const QString& id) {
                             if (const auto perfil = PerfilId::desdeTexto(id)) {
                                 operaciones->invalidarSesion(*perfil);
                             }
                         });
        m_operacionesSatPropio = std::move(productivo);
        sat = m_operacionesSatPropio.get();
    }

    SqlitePersistencia* persistencia = m_persistencia.get();
    PuertosEjecutor puertosEjecutor{p.solicitudes(), p.logs(), p.operaciones(), p.unidadDeTrabajo(), *m_sanitizer,
                                    *sat, [persistencia] { persistencia->cerrarConexionDelHiloActual(); },
                                    &p.paquetes(), m_packageStorage};
    m_ejecutor = std::make_unique<OperacionExecutor>(std::move(puertosEjecutor), reloj, *programadorEjecutor);
    m_worker = std::make_unique<WorkerLocal>(*m_ejecutor, *m_configuracionService, *programadorWorker, reloj);
    m_extensionWorker = std::make_unique<ExtensionWorker>(*m_worker, *m_ejecutor);
    m_accionesWorker = std::make_unique<AccionesWorker>(*m_worker);
    m_consultaExistencia = std::make_unique<ConsultaExistenciaEjecutor>(*m_dispatcher, p.paquetes(), *m_ejecutor);
    m_accesoPaquetes = std::make_unique<AccesoPaquetesEjecutor>(*m_ejecutor);
    m_accesoFinder = std::make_unique<AccesoFinder>(*m_accesoPaquetes, *m_dispatcher, p.paquetes());

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
    CredencialesSatService* credenciales = m_credencialesService.get();
    QObject::connect(m_ejecutor.get(), &OperacionExecutor::estadoCredencialCambiado, credenciales,
                     [credenciales](const PerfilId& perfil, EstadoCredencial estado) {
                         qCInfo(lcMonitoreo, "estado de credencial de un perfil cambio a %s",
                                qUtf8Printable(claveEstable(estado)));
                         emit credenciales->credencialCambio(perfil.texto());
                     });

    // T009 D1/D9: notificaciones decididas por la aplicacion (una sola fuente,
    // tambien las de credencial) solo tras commit, entregadas por OSIntegration
    // cuando exista (iniciarCicloDeVida). El resultado no vuelve a la app.
    m_notificador = std::make_unique<NotificadorOS>();
    m_servicioNotificaciones = std::make_unique<ServicioNotificaciones>(*m_notificador);
    QObject::connect(m_ejecutor.get(), &OperacionExecutor::transicionConfirmada, m_servicioNotificaciones.get(),
                     &ServicioNotificaciones::alConfirmarTransicion);
    QObject::connect(m_ejecutor.get(), &OperacionExecutor::estadoCredencialCambiado,
                     m_servicioNotificaciones.get(), &ServicioNotificaciones::alCambiarEstadoCredencial);

    // T014.3 D1: primer uso (lectura en el dispatcher). D2/D3: aviso de
    // vencimiento de e.firma; arranca con iniciarMonitoreo() y reevalua al
    // cambiar una credencial (un reemplazo reinicia el dedupe).
    m_consultaPrimerUso = std::make_unique<ConsultaPrimerUsoPersistida>(*m_dispatcher, p.perfiles(), p.solicitudes(),
                                                                        p.credenciales(), secretStore, reloj);
    m_consultaVencimiento = std::make_unique<ConsultaPreparacionPerfiles>(*m_perfilesService, *m_credencialesService,
                                                                          nullptr, reloj);
    Programador* programadorVencimiento = monitoreo.programadorVencimiento;
    if (!programadorVencimiento) {
        m_programadorVencimientoPropio = std::make_unique<ProgramadorQt>(reloj);
        programadorVencimiento = m_programadorVencimientoPropio.get();
    }
    ConsultaPreparacionPerfiles* consultaVencimiento = m_consultaVencimiento.get();
    m_avisoVencimiento = std::make_unique<AvisoVencimientoEFirma>(
        [consultaVencimiento] { return consultaVencimiento->listarVerificados(); }, *m_dispatcher,
        p.avisosVencimiento(), p.unidadDeTrabajo(), *m_servicioNotificaciones, *programadorVencimiento, reloj);
    QObject::connect(m_credencialesService.get(), &CredencialesSatService::credencialCambio, m_avisoVencimiento.get(),
                     &AvisoVencimientoEFirma::alCambiarCredencial);

    // T014.4 D2: estado agregado del icono. Consultas de solo lectura (una
    // tarea del dispatcher para solicitudes/paquetes; preparacion de perfiles
    // por metadata). Recalcula al cambiar el worker, la lista de solicitudes
    // (creacion, transiciones confirmadas del ejecutor, eliminacion), los
    // perfiles o una credencial. Se inicia en iniciarCicloDeVida().
    {
        PersistenceDispatcher* dispatcher = m_dispatcher.get();
        SolicitudMasivaRepository* repoSolicitudes = &p.solicitudes();
        PaqueteSolicitudRepository* repoPaquetes = &p.paquetes();
        m_estadoAgregado = std::make_unique<MonitorEstadoAgregado>(
            [dispatcher, repoSolicitudes, repoPaquetes] {
                return consultarAtencionSolicitudes(*dispatcher, *repoSolicitudes, *repoPaquetes);
            },
            [consultaVencimiento] { return consultarAtencionPerfiles(*consultaVencimiento); });
        MonitorEstadoAgregado* monitor = m_estadoAgregado.get();
        QObject::connect(m_worker.get(), &WorkerLocal::instantaneaCambiada, monitor,
                         &MonitorEstadoAgregado::alCambiarWorker);
        QObject::connect(m_solicitudesService.get(), &SolicitudesService::listaCambiada, monitor,
                         &MonitorEstadoAgregado::refrescarSolicitudes);
        QObject::connect(m_solicitudesService.get(), &SolicitudesService::solicitudEliminada, monitor,
                         &MonitorEstadoAgregado::refrescarSolicitudes);
        QObject::connect(m_perfilesService.get(), &PerfilesSatService::perfilesCambiaron, monitor,
                         &MonitorEstadoAgregado::refrescarPerfiles);
        QObject::connect(m_credencialesService.get(), &CredencialesSatService::credencialCambio, monitor,
                         &MonitorEstadoAgregado::refrescarPerfiles);
    }

    m_viewModels = std::make_unique<PresentacionViewModels>(
        m_solicitudesService.get(), m_perfilesService.get(), m_credencialesService.get(), nullptr,
        reloj); // T014.3: mismo reloj que AvisoVencimientoEFirma (badge = aviso)
    m_viewModels->setAccionesSolicitud(m_accionesWorker.get());
    m_viewModels->setConsultaExistencia(m_consultaExistencia.get());
    m_viewModels->setAccionesFinder(m_accesoFinder.get());
    m_viewModels->setConsultaPrimerUso(m_consultaPrimerUso.get()); // T014.3 D1

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
    m_estadoAgregado.reset(); // T014.4: antes que sus consultas y servicios
    m_avisoVencimiento.reset(); // cancela su programacion; antes que notificaciones
    m_programadorVencimientoPropio.reset();
    m_consultaVencimiento.reset();
    m_consultaPrimerUso.reset();
    m_servicioNotificaciones.reset();
    m_accesoFinder.reset();
    m_accesoPaquetes.reset();
    m_accionesWorker.reset();
    m_consultaExistencia.reset();
    m_extensionWorker.reset();
    if (m_worker) {
        m_worker->detener();
    }
    m_worker.reset();
    m_ejecutor.reset(); // ~OperacionExecutor: cancela, espera y cierra su conexion en su hilo
    m_programadorWorkerPropio.reset();
    m_programadorEjecutorPropio.reset();
    QObject::disconnect(m_conexionInvalidarSesion);
    m_operacionesSatPropio.reset(); // ~OperacionesSatProductivo: cerrarSesiones()
    m_packageStoragePropio.reset();
    m_satGatewayPropio.reset();
    m_notificador.reset();

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
    m_avisoVencimiento->iniciar(); // T014.3: al arrancar y cada 24 h
}

ConsultaPrimerUso& AppCompositionRoot::consultaPrimerUso() const
{
    return *m_consultaPrimerUso;
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
    // T009: notificaciones (credencial aqui; las de solicitud las decide el
    // servicio de aplicacion) y aviso de notificaciones deshabilitadas.
    m_os = &os;
    m_notificador->setOS(&os);
    m_accesoFinder->setOS(&os);
    AppViewModel* app = m_viewModels->app();
    const auto reflejarPermiso = [app](OSIntegration::NotificationStatus s) {
        app->setNotificacionesDeshabilitadas(s == OSIntegration::NotificationStatus::Denied
                                             || s == OSIntegration::NotificationStatus::Unavailable);
    };
    reflejarPermiso(os.notificationStatus());
    QObject::connect(&os, &OSIntegration::notificationStatusChanged, app, reflejarPermiso);

    // T007: pausa/reanudacion confirmadas y salida (D1) pasan por el worker.
    m_controlador->setExtension(m_extensionWorker.get());

    // Estado del worker -> menu bar (D2, D11): estado inicial y cada cambio.
    QObject::connect(m_worker.get(), &WorkerLocal::instantaneaCambiada, &os,
                     [&os](const InstantaneaWorker& i) { os.reflejarEstadoMonitoreo(estadoMonitoreoDe(i)); });

    // T009.1 D1: "Abrir carpeta de paquetes" del menu bar. Si no se puede
    // abrir (carpeta inexistente, error o Finder fallido) el aviso D7 se
    // muestra en la lista de la ventana, que se trae al frente; nunca se crea
    // la carpeta.
    AppLifecycleController* controlador = m_controlador.get();
    QObject::connect(&os, &OSIntegration::abrirCarpetaPaquetesSolicitada, controlador, [this, app, controlador] {
        app->setMensajeFinder(QString());
        m_accesoFinder->abrirCarpetaPaquetes().then(controlador, [app, controlador](const ResultadoAccionFinder& r) {
            if (r.estado != ResultadoAccionFinder::Estado::Mostrado) {
                app->setMensajeFinder(r.mensaje);
                app->mostrarLista();
                controlador->mostrarVentana();
            }
        });
    });

    // T014.4 D2: estado agregado -> icono, solo cuando cambia (la primera
    // publicacion fija el estado inicial). En la salida deja de publicar.
    MonitorEstadoAgregado* monitor = m_estadoAgregado.get();
    QObject::connect(monitor, &MonitorEstadoAgregado::estadoCambiado, &os,
                     [&os](EstadoAgregado estado) { os.reflejarEstadoIcono(estadoIconoDe(estado)); });
    QObject::connect(controlador, &AppLifecycleController::salidaIniciada, monitor,
                     &MonitorEstadoAgregado::detener);
    monitor->alCambiarWorker(m_worker->instantanea());
    monitor->iniciar();

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
