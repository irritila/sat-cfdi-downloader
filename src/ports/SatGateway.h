#pragma once

namespace satcfdi {

// Puerto hacia el servicio web de descarga masiva del SAT: autenticacion,
// solicitud, verificacion y descarga de paquetes. La forma de sus operaciones
// y DTOs depende del spike SAT (T006); no declara SOAP, codigos ni firma.
//
// T002: frontera compilable sin metodos. Los metodos se agregan en la tarea
// que fija el contrato (ver docs/tasks/); no adelantar firmas aqui.
class SatGateway {
public:
    virtual ~SatGateway() = default;
};

} // namespace satcfdi
