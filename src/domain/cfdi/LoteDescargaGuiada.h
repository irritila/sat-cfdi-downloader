#pragma once

#include "domain/solicitudes/EstadoResumen.h"

#include <QLatin1StringView>
#include <QList>

namespace satcfdi {

// Resultado de un lote de descarga guiada (T023.2, ADR 0019), derivado de
// las solicitudes del lote que siguen visibles. Nunca se persiste: si se
// elimina una solicitud del lote se recalcula sin ella.
enum class ResultadoLote {
    SinSolicitudesVigentes, // todas las solicitudes del lote fueron eliminadas
    ConAtencion,            // alguna EnvioFallido, EnvioIncierto, ErrorSat, Rechazada o Vencida
    EnCurso,                // alguna Creada, Enviando, Enviada, Aceptada o EnProceso
    Terminado,              // todas Terminada
};

QLatin1StringView claveEstable(ResultadoLote resultado);

// Precedencia: ConAtencion > EnCurso > Terminado. Funcion pura.
ResultadoLote resultadoLote(const QList<EstadoResumen>& vigentes);

} // namespace satcfdi
