#pragma once

namespace satcfdi {

// Repositorio de la configuracion unica de la aplicacion. Metodos sincronos
// desde T003.
//
// T002: frontera compilable sin metodos. Los metodos se agregan en la tarea
// que fija el contrato (ver docs/tasks/); no adelantar firmas aqui.
class ConfiguracionAppRepository {
public:
    virtual ~ConfiguracionAppRepository() = default;
};

} // namespace satcfdi
