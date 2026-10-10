#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteFilas.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QSqlQuery>

#include <utility>

namespace satcfdi {

namespace {

constexpr QStringView kTabla = u"paquete_solicitud";

} // namespace

SqlitePaqueteSolicitudRepository::SqlitePaqueteSolicitudRepository(SqliteConnectionProvider& proveedor)
    : m_proveedor(proveedor)
{
}

Resultado<QList<PaquetePersistido>, ErrorPersistencia>
SqlitePaqueteSolicitudRepository::listarVisiblesPorSolicitud(const SolicitudId& solicitudId)
{
    using R = Resultado<QList<PaquetePersistido>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"paquete_solicitud.listar";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = sqlite::preparar(
            q,
            QStringLiteral("SELECT ") + sqlite::columnasPaquete()
                + QStringLiteral(" FROM paquete_solicitud WHERE solicitud_masiva_id = :id "
                                 "AND eliminado_en IS NULL "
                                 "ORDER BY disponible_en ASC, id_paquete_sat ASC"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":id"), sqlite::texto(solicitudId.texto()));
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    QList<PaquetePersistido> lista;
    while (q.next()) {
        auto fila = sqlite::leerPaquete(q);
        if (!fila) {
            return R::fallo(std::move(fila).error());
        }
        lista.append(std::move(fila).valor());
    }
    return R::exito(std::move(lista));
}

Resultado<QHash<SolicitudId, int>, ErrorPersistencia>
SqlitePaqueteSolicitudRepository::contarVisiblesPorSolicitud()
{
    using R = Resultado<QHash<SolicitudId, int>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"paquete_solicitud.contar";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = sqlite::ejecutarDirecto(
            q,
            QStringLiteral("SELECT solicitud_masiva_id, count(*) FROM paquete_solicitud "
                           "WHERE eliminado_en IS NULL GROUP BY solicitud_masiva_id"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    QHash<SolicitudId, int> conteo;
    while (q.next()) {
        const auto id = SolicitudId::desdeTexto(q.value(0).toString());
        if (!id) {
            return R::fallo(sqlite::filaIlegible(kTabla, u"solicitud_masiva_id"));
        }
        conteo.insert(*id, q.value(1).toInt());
    }
    return R::exito(std::move(conteo));
}

Resultado<QHash<SolicitudId, ConteoPaquetes>, ErrorPersistencia>
SqlitePaqueteSolicitudRepository::contarVisiblesPorSolicitudYEstado()
{
    using R = Resultado<QHash<SolicitudId, ConteoPaquetes>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"paquete_solicitud.contar_estado";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    // estado_descarga se guarda con la clave estable del enum (claveEstable).
    if (auto r = sqlite::ejecutarDirecto(
            q,
            QStringLiteral("SELECT solicitud_masiva_id, count(*), "
                           "sum(CASE WHEN estado_descarga = 'Descargado' THEN 1 ELSE 0 END), "
                           "sum(CASE WHEN estado_descarga IN ('Disponible', 'Error') THEN 1 ELSE 0 END) "
                           "FROM paquete_solicitud WHERE eliminado_en IS NULL GROUP BY solicitud_masiva_id"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    QHash<SolicitudId, ConteoPaquetes> conteo;
    while (q.next()) {
        const auto id = SolicitudId::desdeTexto(q.value(0).toString());
        if (!id) {
            return R::fallo(sqlite::filaIlegible(kTabla, u"solicitud_masiva_id"));
        }
        conteo.insert(*id, ConteoPaquetes{q.value(1).toInt(), q.value(2).toInt(), q.value(3).toInt()});
    }
    return R::exito(std::move(conteo));
}

Resultado<int, ErrorPersistencia>
SqlitePaqueteSolicitudRepository::marcarEliminadosPorSolicitud(const SolicitudId& solicitudId,
                                                               const QDateTime& eliminadoEn)
{
    using R = Resultado<int, ErrorPersistencia>;
    constexpr QStringView kContexto = u"paquete_solicitud.marcar_eliminados";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (auto r = sqlite::preparar(q,
                                  QStringLiteral("UPDATE paquete_solicitud SET eliminado_en = "
                                                 ":eliminado_en, reintento_pendiente_en = NULL "
                                                 "WHERE solicitud_masiva_id = :id "
                                                 "AND eliminado_en IS NULL"),
                                  kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":eliminado_en"), sqlite::instante(eliminadoEn));
    q.bindValue(QStringLiteral(":id"), sqlite::texto(solicitudId.texto()));
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    return R::exito(q.numRowsAffected());
}

} // namespace satcfdi
