#include "AppCompositionRoot.h"

namespace satcfdi {

AppCompositionRoot::AppCompositionRoot()
    : m_perfiles(DemoPerfilesSatService::perfilesDemo())
    , m_perfilesService(m_perfiles)
    , m_solicitudesService(m_perfiles)
    , m_viewModels(&m_solicitudesService, &m_perfilesService)
{
}

bool AppCompositionRoot::cargar()
{
    m_engine.setInitialProperties(m_viewModels.initialProperties());
    m_engine.loadFromModule("SatCfdiDownloader", "Main");
    return !m_engine.rootObjects().isEmpty();
}

} // namespace satcfdi
