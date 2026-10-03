#pragma once

namespace satcfdi {

// Repositorio de solicitudes masivas locales (solicitud_masiva). Metodos
// sincronos desde T003.
//
// T002: frontera compilable sin metodos. Los metodos se agregan en la tarea
// que fija el contrato (ver docs/tasks/); no adelantar firmas aqui.
class SolicitudMasivaRepository {
public:
    virtual ~SolicitudMasivaRepository() = default;
};

} // namespace satcfdi
