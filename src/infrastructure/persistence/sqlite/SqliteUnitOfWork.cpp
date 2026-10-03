#include "infrastructure/persistence/sqlite/SqliteUnitOfWork.h"

#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QSqlDatabase>
#include <QSqlQuery>

#include <utility>

namespace satcfdi {

namespace {

using ResultadoExito = Resultado<Exito, ErrorPersistencia>;

ResultadoExito ejecutarSentencia(SqliteConnectionProvider& proveedor, const QString& sql,
                                 QStringView contexto)
{
    auto conexion = proveedor.conexion();
    if (!conexion) {
        return ResultadoExito::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    return sqlite::ejecutarDirecto(q, sql, contexto);
}

} // namespace

SqliteUnitOfWork::SqliteUnitOfWork(SqliteConnectionProvider& proveedor)
    : m_proveedor(proveedor)
{
}

Resultado<Exito, ErrorPersistencia> SqliteUnitOfWork::begin()
{
    if (m_proveedor.transaccionActiva()) {
        return ResultadoExito::fallo(ErrorPersistencia::de(
            ErrorPersistencia::Tipo::Transaccion,
            QStringLiteral("unidad_de_trabajo.begin: ya hay una transaccion activa")));
    }
    auto r = ejecutarSentencia(m_proveedor, QStringLiteral("BEGIN IMMEDIATE"),
                               u"unidad_de_trabajo.begin");
    if (r) {
        m_proveedor.marcarTransaccion(true);
    }
    return r;
}

Resultado<Exito, ErrorPersistencia> SqliteUnitOfWork::commit()
{
    if (!m_proveedor.transaccionActiva()) {
        return ResultadoExito::fallo(ErrorPersistencia::de(
            ErrorPersistencia::Tipo::Transaccion,
            QStringLiteral("unidad_de_trabajo.commit: no hay transaccion activa")));
    }
    auto r = ejecutarSentencia(m_proveedor, QStringLiteral("COMMIT"), u"unidad_de_trabajo.commit");
    if (r) {
        m_proveedor.marcarTransaccion(false);
        return r;
    }
    // COMMIT fallido (p. ej. SQLITE_BUSY): la transaccion sigue abierta.
    // Se revierte para no dejar la conexion bloqueando escrituras.
    (void)rollback();
    return r;
}

Resultado<Exito, ErrorPersistencia> SqliteUnitOfWork::rollback()
{
    if (!m_proveedor.transaccionActiva()) {
        return ResultadoExito::exito(Exito{});
    }
    auto r = ejecutarSentencia(m_proveedor, QStringLiteral("ROLLBACK"),
                               u"unidad_de_trabajo.rollback");
    // Tras un ROLLBACK (exitoso o no) la conexion queda en autocommit: SQLite
    // revierte automaticamente ante errores graves y un segundo ROLLBACK
    // fallaria con "no transaction is active".
    m_proveedor.marcarTransaccion(false);
    if (!r && r.error().tipo == ErrorPersistencia::Tipo::Interno) {
        return ResultadoExito::exito(Exito{});
    }
    return r;
}

} // namespace satcfdi
