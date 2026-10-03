#pragma once

namespace satcfdi {

// Repositorio de perfiles SAT (perfil_sat). Metodos sincronos desde T003.
//
// T002: frontera compilable sin metodos. Los metodos se agregan en la tarea
// que fija el contrato (ver docs/tasks/); no adelantar firmas aqui.
class PerfilSatRepository {
public:
    virtual ~PerfilSatRepository() = default;
};

} // namespace satcfdi
