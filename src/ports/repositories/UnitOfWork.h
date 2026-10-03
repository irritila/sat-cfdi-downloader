#pragma once

#include "domain/common/Resultado.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/persistence/Exito.h"

namespace satcfdi {

// Unidad de trabajo transaccional sobre la conexion SQLite del hilo actual
// (ADR 0016). Agrupa operaciones de los repositorios ejecutadas en el MISMO
// hilo entre begin() y commit()/rollback().
//
// Hilo: solo desde tareas de PersistenceDispatcher (nunca el hilo grafico).
// No reentrante: no hay transacciones anidadas ni savepoints en T003.
class UnitOfWork {
public:
    virtual ~UnitOfWork() = default;

    // `BEGIN IMMEDIATE` (toma el bloqueo de escritura antes de leer).
    // Errores: Transaccion si ya hay una activa; Ocupado si se agota
    // busy_timeout; Almacenamiento.
    virtual Resultado<Exito, ErrorPersistencia> begin() = 0;

    // `COMMIT`. Errores: Transaccion si no hay una activa; Ocupado;
    // Almacenamiento; Integridad si una FK diferida falla. Si falla, la
    // transaccion queda revertida o el llamador debe invocar rollback().
    virtual Resultado<Exito, ErrorPersistencia> commit() = 0;

    // `ROLLBACK`. Idempotente: sin transaccion activa devuelve exito.
    // Errores: Almacenamiento.
    virtual Resultado<Exito, ErrorPersistencia> rollback() = 0;
};

} // namespace satcfdi
