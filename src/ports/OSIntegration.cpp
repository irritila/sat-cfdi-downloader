#include "ports/OSIntegration.h"

namespace satcfdi {

// Destructor fuera de linea: ancla la vtable y el codigo MOC en satcfdi_ports.
OSIntegration::~OSIntegration() = default;

std::unique_ptr<OSIntegration::ActividadSistema> OSIntegration::iniciarActividadSistema(MotivoActividad motivo)
{
    Q_UNUSED(motivo);
    return std::make_unique<ActividadSistema>();
}

} // namespace satcfdi
