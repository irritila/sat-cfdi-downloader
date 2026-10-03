#pragma once

namespace satcfdi {

// Repositorio de logs saneados por solicitud (log_solicitud). Metodos
// sincronos desde T003.
//
// T002: frontera compilable sin metodos. Los metodos se agregan en la tarea
// que fija el contrato (ver docs/tasks/); no adelantar firmas aqui.
class LogSolicitudRepository {
public:
    virtual ~LogSolicitudRepository() = default;
};

} // namespace satcfdi
