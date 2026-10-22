#pragma once

#include "ports/OSIntegration.h"

#include <memory>

namespace satcfdi::macos {

// T025.4 (ADR 0009): actividad de sistema con NSProcessInfo
// beginActivityWithOptions:NSActivityUserInitiated (evita App Nap y el reposo
// por inactividad; aparece en `pmset -g assertions` como
// PreventUserIdleSystemSleep con el motivo). El token la termina al
// destruirse. Cualquier hilo; sin estado compartido.
std::unique_ptr<OSIntegration::ActividadSistema> iniciarActividadSistemaMacOS(OSIntegration::MotivoActividad motivo);

// Texto fijo del motivo, sin datos personales.
QString textoMotivoActividad(OSIntegration::MotivoActividad motivo);

} // namespace satcfdi::macos
