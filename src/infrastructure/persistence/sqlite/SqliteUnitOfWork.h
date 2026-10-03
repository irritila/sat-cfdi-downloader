#pragma once

#include "ports/repositories/UnitOfWork.h"

namespace satcfdi {

class SqliteConnectionProvider;

// UnitOfWork sobre la conexion SQLite del hilo actual (ADR 0016).
// - begin(): `BEGIN IMMEDIATE` (no QSqlDatabase::transaction(), que emite
//   BEGIN DEFERRED). Marca la transaccion en el proveedor para que los
//   repositorios acepten escrituras.
// - commit(): `COMMIT`. Si falla, revierte (ROLLBACK) y deja la conexion sin
//   transaccion antes de devolver el error.
// - rollback(): idempotente.
// No retiene QSqlDatabase/QSqlQuery entre llamadas. No es thread-safe por si
// mismo: cada hilo opera sobre su propia conexion via el proveedor.
class SqliteUnitOfWork final : public UnitOfWork {
public:
    explicit SqliteUnitOfWork(SqliteConnectionProvider& proveedor);

    Resultado<Exito, ErrorPersistencia> begin() override;
    Resultado<Exito, ErrorPersistencia> commit() override;
    Resultado<Exito, ErrorPersistencia> rollback() override;

private:
    SqliteConnectionProvider& m_proveedor;
};

} // namespace satcfdi
