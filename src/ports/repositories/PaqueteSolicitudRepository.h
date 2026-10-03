#pragma once

namespace satcfdi {

// Repositorio de paquetes por solicitud (paquete_solicitud). Metodos
// sincronos desde T003.
//
// T002: frontera compilable sin metodos. Los metodos se agregan en la tarea
// que fija el contrato (ver docs/tasks/); no adelantar firmas aqui.
class PaqueteSolicitudRepository {
public:
    virtual ~PaqueteSolicitudRepository() = default;
};

} // namespace satcfdi
