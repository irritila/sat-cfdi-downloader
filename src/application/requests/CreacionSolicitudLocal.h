#pragma once

#include "application/common/Errores.h"
#include "application/persistence/PuertosPersistencia.h"
#include "application/requests/SolicitudDtos.h"
#include "domain/common/Resultado.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/Duplicados.h"
#include "domain/solicitudes/SolicitudCanonica.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QDateTime>

namespace satcfdi {

// Nucleo de crearLocal DENTRO de una transaccion ya abierta (BEGIN IMMEDIATE)
// por el llamador, compartido por SolicitudesServicePersistido::crearLocal y
// la descarga guiada (T023.3). No abre, confirma ni revierte la transaccion.
//
// Pasos: clasificarDuplicado (autoritativo); Bloqueado -> DedupBloqueado;
// RequiereConfirmacion sin Confirmada -> RequiereConfirmacion; insertarCreada
// (DedupBloqueado del indice parcial -> DedupBloqueado); logs
// solicitud_creada y, si se confirmo un duplicado, duplicado_confirmado, por
// el LogSanitizer. Devuelve la evaluacion aplicada.
//
// Hilo: tarea del PersistenceDispatcher (la del llamador).
Resultado<EvaluacionDuplicado, ErrorCrear> crearSolicitudLocalEnTransaccion(const PuertosPersistencia& puertos,
                                                                           const SolicitudId& id,
                                                                           const PerfilId& perfil,
                                                                           const SolicitudCanonica& canonica,
                                                                           ConfirmacionDuplicado confirmacion,
                                                                           const QDateTime& ahora);

} // namespace satcfdi
