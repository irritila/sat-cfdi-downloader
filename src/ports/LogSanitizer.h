#pragma once

namespace satcfdi {

// Puerto de sanitizacion de entradas de log antes de persistirlas, para no
// registrar secretos, tokens ni datos sensibles.
//
// T002: frontera compilable sin metodos. Los metodos se agregan en la tarea
// que fija el contrato (ver docs/tasks/); no adelantar firmas aqui.
class LogSanitizer {
public:
    virtual ~LogSanitizer() = default;
};

} // namespace satcfdi
