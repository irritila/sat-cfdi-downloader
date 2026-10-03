#pragma once

namespace satcfdi {

// Puerto de integracion con el sistema operativo: menu bar, inicio con la
// sesion y notificaciones (ADR 0009). Se implementa en T004; T002 no lo usa.
//
// T002: frontera compilable sin metodos. Los metodos se agregan en la tarea
// que fija el contrato (ver docs/tasks/); no adelantar firmas aqui.
class OSIntegration {
public:
    virtual ~OSIntegration() = default;
};

} // namespace satcfdi
