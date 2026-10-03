#pragma once

#include "ports/OSIntegration.h"

#include <memory>

namespace satcfdi {

// Punto de inyeccion del adaptador de SO de produccion (T004). Unico lugar de
// satcfdi_app que elige la implementacion concreta de OSIntegration.
std::unique_ptr<OSIntegration> crearOSIntegracion();

} // namespace satcfdi
