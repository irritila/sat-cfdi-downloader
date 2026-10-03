#pragma once

#include "application/common/Errores.h"
#include "application/requests/SolicitudDtos.h"
#include "domain/common/Resultado.h"
#include "domain/solicitudes/SolicitudCanonica.h"

#include <QString>

#include <optional>

namespace satcfdi {

// Validacion y normalizacion de NuevaSolicitudRequest compartida por los
// servicios (demo y persistido). Funcion pura; corre fuera de transaccion.
//
// `rfcPerfilActivo`: RFC del perfil si existe, no esta eliminado y esta
// activo; nullopt en otro caso.
//
// Errores (se reportan todos):
// - ErrorCrear::Validacion: PerfilRequerido / PerfilInexistente y errores de
//   fecha (FechaInicialRequerida, FechaFinalRequerida, RangoFechasInvalido);
//   si ademas hay errores de filtros, tambien se adjuntan en `filtros`.
// - ErrorCrear::FiltroInvalido: solo errores de filtros SAT.
Resultado<SolicitudCanonica, ErrorCrear>
prepararSolicitud(const NuevaSolicitudRequest& request,
                  const std::optional<QString>& rfcPerfilActivo);

} // namespace satcfdi
