#pragma once

#include "domain/solicitudes/EstadosSolicitud.h"

#include <QString>

#include <optional>

namespace satcfdi {

// Estado resumido para mostrar en UI. Es un valor derivado: nunca se persiste
// (operational-rules: "no persistir una sola columna que mezcle ambas
// dimensiones").
enum class EstadoResumen {
    Creada,
    Enviando,
    Enviada,
    EnvioFallido,
    EnvioIncierto,
    Aceptada,
    EnProceso,
    Terminada,
    ErrorSat,
    Rechazada,
    Vencida,
};

// Regla DA5: si existe estado SAT se usa ese (Error -> ErrorSat); si no, se usa
// el estado local. Funcion pura.
EstadoResumen derivarEstadoResumen(EstadoLocal estadoLocal,
                                   std::optional<EstadoSolicitudSat> estadoSat);

// Claves QML exactas: Creada, Enviando, Enviada, EnvioFallido, EnvioIncierto,
// Aceptada, EnProceso, Terminada, ErrorSat, Rechazada, Vencida.
QString claveEstable(EstadoResumen estado);

} // namespace satcfdi
