#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QSqlQuery>

#include <utility>

namespace satcfdi {

namespace {

constexpr QStringView kTabla = u"log_solicitud";

Resultado<LogPersistido, ErrorPersistencia> leerLog(const QSqlQuery& q)
{
    using R = Resultado<LogPersistido, ErrorPersistencia>;
    auto ilegible = [](QStringView columna) { return R::fallo(sqlite::filaIlegible(kTabla, columna)); };

    LogPersistido l;
    l.id = q.value(0).toString();
    const auto solicitud = SolicitudId::desdeTexto(q.value(1).toString());
    if (!solicitud) {
        return ilegible(u"solicitud_masiva_id");
    }
    l.solicitudMasivaId = *solicitud;
    const auto tipo = tipoEventoLogDesdeClave(q.value(2).toString());
    if (!tipo) {
        return ilegible(u"tipo_evento");
    }
    l.tipoEvento = *tipo;
    const auto origen = origenLogDesdeClave(q.value(3).toString());
    if (!origen) {
        return ilegible(u"origen");
    }
    l.origen = *origen;
    if (const auto origenCodigo = sqlite::leerTextoOpcional(q.value(4))) {
        const auto o = origenCodigoSatDesdeClave(*origenCodigo);
        if (!o) {
            return ilegible(u"origen_codigo_sat");
        }
        l.origenCodigoSat = *o;
    }
    l.codigoSat = sqlite::leerTextoOpcional(q.value(5));
    l.mensajeSat = sqlite::leerTextoOpcional(q.value(6));
    l.payloadResumenJson = sqlite::leerTextoOpcional(q.value(7));
    if (!sqlite::leerInstante(q.value(8), l.creadoEn)) {
        return ilegible(u"creado_en");
    }
    if (!sqlite::leerInstanteOpcional(q.value(9), l.eliminadoEn)) {
        return ilegible(u"eliminado_en");
    }
    return R::exito(std::move(l));
}

} // namespace

SqliteLogSolicitudRepository::SqliteLogSolicitudRepository(SqliteConnectionProvider& proveedor)
    : m_proveedor(proveedor)
{
}

Resultado<QList<LogPersistido>, ErrorPersistencia>
SqliteLogSolicitudRepository::listarVisiblesPorSolicitud(const SolicitudId& solicitudId)
{
    using R = Resultado<QList<LogPersistido>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"log_solicitud.listar";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = sqlite::preparar(
            q,
            QStringLiteral("SELECT id, solicitud_masiva_id, tipo_evento, origen, "
                           "origen_codigo_sat, codigo_sat, mensaje_sat, payload_resumen_json, "
                           "creado_en, eliminado_en FROM log_solicitud "
                           "WHERE solicitud_masiva_id = :id AND eliminado_en IS NULL "
                           "ORDER BY creado_en ASC, id ASC"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":id"), sqlite::texto(solicitudId.texto()));
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    QList<LogPersistido> lista;
    while (q.next()) {
        auto fila = leerLog(q);
        if (!fila) {
            return R::fallo(std::move(fila).error());
        }
        lista.append(std::move(fila).valor());
    }
    return R::exito(std::move(lista));
}

Resultado<Exito, ErrorPersistencia> SqliteLogSolicitudRepository::agregar(const LogEntradaSaneada& entrada)
{
    using R = Resultado<Exito, ErrorPersistencia>;
    constexpr QStringView kContexto = u"log_solicitud.agregar";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (auto r = sqlite::preparar(
            q,
            QStringLiteral("INSERT INTO log_solicitud (id, solicitud_masiva_id, tipo_evento, "
                           "origen, origen_codigo_sat, codigo_sat, mensaje_sat, "
                           "payload_resumen_json, creado_en, eliminado_en) VALUES (:id, "
                           ":solicitud_masiva_id, :tipo_evento, :origen, :origen_codigo_sat, "
                           ":codigo_sat, :mensaje_sat, :payload_resumen_json, :creado_en, NULL)"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":id"), sqlite::texto(entrada.id));
    q.bindValue(QStringLiteral(":solicitud_masiva_id"),
                sqlite::texto(entrada.solicitudMasivaId.texto()));
    q.bindValue(QStringLiteral(":tipo_evento"), sqlite::texto(claveEstable(entrada.tipoEvento)));
    q.bindValue(QStringLiteral(":origen"), sqlite::texto(claveEstable(entrada.origen)));
    q.bindValue(QStringLiteral(":origen_codigo_sat"),
                sqlite::textoOpcional(entrada.origenCodigoSat
                                          ? std::optional<QString>(claveEstable(*entrada.origenCodigoSat))
                                          : std::nullopt));
    q.bindValue(QStringLiteral(":codigo_sat"), sqlite::textoOpcional(entrada.codigoSat));
    q.bindValue(QStringLiteral(":mensaje_sat"), sqlite::textoOpcional(entrada.mensajeSat));
    q.bindValue(QStringLiteral(":payload_resumen_json"),
                sqlite::textoOpcional(entrada.payloadResumenJson));
    q.bindValue(QStringLiteral(":creado_en"), sqlite::instante(entrada.creadoEn));
    return sqlite::ejecutar(q, kContexto);
}

Resultado<int, ErrorPersistencia>
SqliteLogSolicitudRepository::marcarEliminadosPorSolicitud(const SolicitudId& solicitudId,
                                                           const QDateTime& eliminadoEn)
{
    using R = Resultado<int, ErrorPersistencia>;
    constexpr QStringView kContexto = u"log_solicitud.marcar_eliminados";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (auto r = sqlite::preparar(q,
                                  QStringLiteral("UPDATE log_solicitud SET eliminado_en = "
                                                 ":eliminado_en WHERE solicitud_masiva_id = :id "
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
