#include "AppCompositionRoot.h"

#include "AppLifecycleController.h"
#include "SingleInstanceCoordinator.h"
#include "application/configuration/ConfiguracionAppServicePersistido.h"
#include "application/logging/RegexLogSanitizer.h"
#include "application/profiles/CredencialesSatServicePersistido.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/PerfilesSatServicePersistido.h"
#include "application/requests/SolicitudesServicePersistido.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include <QQmlApplicationEngine>
#include <QWindow>

#include <functional>

namespace satcfdi {

AppCompositionRoot::AppCompositionRoot(const QString& rutaBase, SecretStore& secretStore)
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
    m_viewModels = std::make_unique<PresentacionViewModels>(
        m_solicitudesService.get(), m_perfilesService.get(), m_credencialesService.get());

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
    m_controlador->iniciar();
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
