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

// T025.6 B6: actividad que solo evita App Nap (NSActivityUserInitiated
// AllowingIdleSystemSleep: no impide el reposo del sistema ni crea aserciones
// de energia) mientras hay un borrado de portapapeles programado. Motivo fijo.
std::unique_ptr<OSIntegration::ActividadSistema> iniciarActividadSinBloquearReposoMacOS();
inline constexpr const char* kMotivoActividadPortapapeles = "Archivo Fiscal: borrado programado del portapapeles";

// Texto fijo del motivo, sin datos personales.
QString textoMotivoActividad(OSIntegration::MotivoActividad motivo);

} // namespace satcfdi::macos
