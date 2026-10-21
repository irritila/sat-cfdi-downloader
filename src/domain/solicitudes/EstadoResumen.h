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

// T023.6 (T023 D22, docs/web-service.md 6.2): periodo sin facturas. La
// verificacion SAT termino con EstadoSolicitud 5 (Rechazada) y
// CodigoEstadoSolicitud 5004 ("no genero paquetes por falta de
// informacion"). No es error: equivale a Terminada con conjunto de paquetes
// vacio. Solo el par estado/codigo de la VERIFICACION (columnas
// estado_solicitud_sat y codigo_estado_solicitud); el CodEstatus 5004 de la
// operacion (solicitud no encontrada) es otra cosa y sigue siendo falla
// (ADR 0018). Se deduce de lo persistido: no requiere migracion. Pura.
bool esSinInformacionSat(std::optional<EstadoSolicitudSat> estadoSat,
                         const std::optional<QString>& codigoEstadoSolicitud);

// Como la anterior, pero un periodo sin facturas (esSinInformacionSat) se
// resume como Terminada (no Rechazada): listas, badges, estado agregado y
// resultado de lote no lo marcan como error. El estado SAT persistido no
// cambia (sigue Rechazada).
EstadoResumen derivarEstadoResumen(EstadoLocal estadoLocal,
                                   std::optional<EstadoSolicitudSat> estadoSat,
                                   const std::optional<QString>& codigoEstadoSolicitud);

// Claves QML exactas: Creada, Enviando, Enviada, EnvioFallido, EnvioIncierto,
// Aceptada, EnProceso, Terminada, ErrorSat, Rechazada, Vencida.
QString claveEstable(EstadoResumen estado);

} // namespace satcfdi
