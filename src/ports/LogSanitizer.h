#pragma once

#include "domain/logs/LogSolicitud.h"

namespace satcfdi {

// Puerto de sanitizacion de logs antes de persistirlos (T003 "LogSanitizer",
// DA3). Implementacion de referencia: application/logging/RegexLogSanitizer.
//
// Contrato:
// - Funcion pura y determinista: no genera ids ni lee el reloj; copia la
//   metadata (id, solicitud, tipo, origen, creadoEn).
// - Idempotente sobre el texto: sanear un texto ya saneado no lo cambia.
// - mensajeSat se sanea (max 500); mensaje (max 500) y detalle (max 8192) se
//   sanean y van a payload_resumen_json junto con idSolicitudSat,
//   idPaqueteSat, operacionSat, dedupKey y filtros. Sin datos -> payload nulo.
// - Garantiza ck_log_codigo_sat: sin codigoSat descarta origenCodigoSat y
//   mueve mensajeSat al payload; con codigoSat sin origen mueve ambos al payload.
// - Marcadores cerrados: [REDACTED:token|password|clave|secret|firma|
//   certificado|pem|paquete], [REDACTED:base64:<len>], [TRUNCATED:<n>].
//
// Hilo: cualquiera; la implementacion debe ser reentrante y const-thread-safe.
class LogSanitizer {
public:
    virtual ~LogSanitizer() = default;

    virtual LogEntradaSaneada sanitizar(const LogEntradaCruda& entrada) const = 0;
};

} // namespace satcfdi
