#pragma once

namespace satcfdi {

// Unidad de trabajo transaccional que agrupa operaciones de repositorios.
// begin/commit/rollback se definen en T003.
//
// T002: frontera compilable sin metodos. Los metodos se agregan en la tarea
// que fija el contrato (ver docs/tasks/); no adelantar firmas aqui.
class UnitOfWork {
public:
    virtual ~UnitOfWork() = default;
};

} // namespace satcfdi
