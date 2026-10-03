#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QSqlQuery>

#include <utility>

namespace satcfdi {

SqliteConfiguracionAppRepository::SqliteConfiguracionAppRepository(SqliteConnectionProvider& proveedor)
    : m_proveedor(proveedor)
{
}

Resultado<ConfiguracionApp, ErrorPersistencia> SqliteConfiguracionAppRepository::obtener()
{
    using R = Resultado<ConfiguracionApp, ErrorPersistencia>;
    constexpr QStringView kContexto = u"configuracion_app.obtener";
    constexpr QStringView kTablaConfig = u"configuracion_app";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = sqlite::ejecutarDirecto(
            q,
            QStringLiteral("SELECT inicio_automatico_habilitado, monitoreo_pausado, "
                           "ultimo_cierre_en, actualizada_en FROM configuracion_app WHERE id = 1"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    if (!q.next()) {
        return R::fallo(ErrorPersistencia::de(ErrorPersistencia::Tipo::NoEncontrado,
                                              QStringLiteral("configuracion_app sin fila id = 1")));
    }
    ConfiguracionApp c;
    c.inicioAutomaticoHabilitado = q.value(0).toInt() != 0;
    c.monitoreoPausado = q.value(1).toInt() != 0;
    if (!sqlite::leerInstanteOpcional(q.value(2), c.ultimoCierreEn)) {
        return R::fallo(sqlite::filaIlegible(kTablaConfig, u"ultimo_cierre_en"));
    }
    if (!sqlite::leerInstante(q.value(3), c.actualizadaEn)) {
        return R::fallo(sqlite::filaIlegible(kTablaConfig, u"actualizada_en"));
    }
    return R::exito(std::move(c));
}

} // namespace satcfdi
