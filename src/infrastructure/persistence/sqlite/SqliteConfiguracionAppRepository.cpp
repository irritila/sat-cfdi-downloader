#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QSqlQuery>

#include <utility>

namespace satcfdi {

namespace {

using ResultadoConfig = Resultado<ConfiguracionApp, ErrorPersistencia>;

constexpr QStringView kTablaConfig = u"configuracion_app";

ErrorPersistencia sinFila()
{
    return ErrorPersistencia::de(ErrorPersistencia::Tipo::NoEncontrado,
                                 QStringLiteral("configuracion_app sin fila id = 1"));
}

// Lee la fila id = 1 con la conexion indicada (dentro o fuera de transaccion).
ResultadoConfig leerFila(QSqlDatabase db, QStringView contexto)
{
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = sqlite::ejecutarDirecto(
            q,
            QStringLiteral("SELECT inicio_automatico_habilitado, monitoreo_pausado, "
                           "ultimo_cierre_en, actualizada_en FROM configuracion_app WHERE id = 1"),
            contexto);
        !r) {
        return ResultadoConfig::fallo(std::move(r).error());
    }
    if (!q.next()) {
        return ResultadoConfig::fallo(sinFila());
    }
    ConfiguracionApp c;
    c.inicioAutomaticoHabilitado = q.value(0).toInt() != 0;
    c.monitoreoPausado = q.value(1).toInt() != 0;
    if (!sqlite::leerInstanteOpcional(q.value(2), c.ultimoCierreEn)) {
        return ResultadoConfig::fallo(sqlite::filaIlegible(kTablaConfig, u"ultimo_cierre_en"));
    }
    if (!sqlite::leerInstante(q.value(3), c.actualizadaEn)) {
        return ResultadoConfig::fallo(sqlite::filaIlegible(kTablaConfig, u"actualizada_en"));
    }
    return ResultadoConfig::exito(std::move(c));
}

// UPDATE de la fila id = 1 con `asignaciones` (ya incluye actualizada_en) y
// relectura en la misma transaccion. `valor` se enlaza a :valor si es valido
// (QVariant() = sin :valor).
ResultadoConfig actualizarFila(SqliteConnectionProvider& proveedor, const QString& asignaciones,
                               const QVariant& valor, const QDateTime& en, QStringView contexto)
{
    if (!en.isValid()) {
        return ResultadoConfig::fallo(ErrorPersistencia::de(
            ErrorPersistencia::Tipo::Interno,
            QStringLiteral("instante invalido en ") + contexto.toString()));
    }
    auto conexion = sqlite::conexionEscritura(proveedor, contexto);
    if (!conexion) {
        return ResultadoConfig::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (auto r = sqlite::preparar(q,
                                  QStringLiteral("UPDATE configuracion_app SET ") + asignaciones
                                      + QStringLiteral(", actualizada_en = :en WHERE id = 1"),
                                  contexto);
        !r) {
        return ResultadoConfig::fallo(std::move(r).error());
    }
    if (valor.isValid()) {
        q.bindValue(QStringLiteral(":valor"), valor);
    }
    q.bindValue(QStringLiteral(":en"), sqlite::instante(en));
    if (auto r = sqlite::ejecutar(q, contexto); !r) {
        return ResultadoConfig::fallo(std::move(r).error());
    }
    if (q.numRowsAffected() <= 0) {
        return ResultadoConfig::fallo(sinFila());
    }
    return leerFila(db, contexto);
}

} // namespace

SqliteConfiguracionAppRepository::SqliteConfiguracionAppRepository(SqliteConnectionProvider& proveedor)
    : m_proveedor(proveedor)
{
}

Resultado<ConfiguracionApp, ErrorPersistencia> SqliteConfiguracionAppRepository::obtener()
{
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return ResultadoConfig::fallo(std::move(conexion).error());
    }
    return leerFila(conexion.valor(), u"configuracion_app.obtener");
}

Resultado<ConfiguracionApp, ErrorPersistencia>
SqliteConfiguracionAppRepository::actualizarInicioAutomatico(bool habilitado, const QDateTime& en)
{
    return actualizarFila(m_proveedor, QStringLiteral("inicio_automatico_habilitado = :valor"),
                          sqlite::booleano(habilitado), en,
                          u"configuracion_app.actualizar_inicio_automatico");
}

Resultado<ConfiguracionApp, ErrorPersistencia>
SqliteConfiguracionAppRepository::actualizarMonitoreoPausado(bool pausado, const QDateTime& en)
{
    return actualizarFila(m_proveedor, QStringLiteral("monitoreo_pausado = :valor"),
                          sqlite::booleano(pausado), en, u"configuracion_app.actualizar_monitoreo_pausado");
}

Resultado<ConfiguracionApp, ErrorPersistencia>
SqliteConfiguracionAppRepository::registrarUltimoCierre(const QDateTime& en)
{
    // ultimo_cierre_en y actualizada_en reciben el mismo instante.
    return actualizarFila(m_proveedor, QStringLiteral("ultimo_cierre_en = :en"), QVariant(), en,
                          u"configuracion_app.registrar_ultimo_cierre");
}

} // namespace satcfdi
