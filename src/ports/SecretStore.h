#pragma once

namespace satcfdi {

// Puerto de almacenamiento seguro de secretos de la e.firma por perfil SAT
// (ADR 0006, ADR 0010). La implementacion productiva vive en infrastructure.
//
// T002: frontera compilable sin metodos. Los metodos se agregan en la tarea
// que fija el contrato (ver docs/tasks/); no adelantar firmas aqui.
class SecretStore {
public:
    virtual ~SecretStore() = default;
};

} // namespace satcfdi
