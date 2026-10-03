#include "AppCompositionRoot.h"

#include "application/logging/RegexLogSanitizer.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/PerfilesSatServicePersistido.h"
#include "application/requests/SolicitudesServicePersistido.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include <QQmlApplicationEngine>

#include <functional>

namespace satcfdi {

AppCompositionRoot::AppCompositionRoot(const QString& rutaBase)
    : m_persistencia(std::make_unique<SqlitePersistencia>(rutaBase))
    , m_dispatcher(std::make_unique<PersistenceDispatcher>())
    , m_sanitizer(std::make_unique<RegexLogSanitizer>())
{
    SqlitePersistencia& p = *m_persistencia;
    const PuertosPersistencia puertos{p.perfiles(), p.solicitudes(), p.paquetes(),
                                      p.logs(),     p.unidadDeTrabajo(), *m_sanitizer};

    m_perfilesService = std::make_unique<PerfilesSatServicePersistido>(
        *m_dispatcher, p.perfiles(), p.unidadDeTrabajo());
    m_solicitudesService = std::make_unique<SolicitudesServicePersistido>(*m_dispatcher, puertos);
    m_viewModels = std::make_unique<PresentacionViewModels>(m_solicitudesService.get(),
                                                            m_perfilesService.get());
}

AppCompositionRoot::~AppCompositionRoot()
{
    // 1. Consumidores, del mas externo al mas interno.
    m_engine.reset();
    m_viewModels.reset();
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

SolicitudesService& AppCompositionRoot::solicitudes() const
{
    return *m_solicitudesService;
}

PerfilesSatService& AppCompositionRoot::perfiles() const
{
    return *m_perfilesService;
}

} // namespace satcfdi
