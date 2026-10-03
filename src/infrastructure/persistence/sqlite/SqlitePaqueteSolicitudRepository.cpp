#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QSqlQuery>

#include <utility>

namespace satcfdi {

namespace {

constexpr QStringView kTabla = u"paquete_solicitud";

const QString kColumnas = QStringLiteral(
    "id, solicitud_masiva_id, id_paquete_sat, estado_descarga, ruta_local, disponible_en, "
    "descarga_iniciada_en, descargado_en, vencimiento_estimado_en, vencido_en, "
    "motivo_vencimiento, origen_vencimiento, reconciliado_en, codigo_descarga_sat, "
    "mensaje_descarga_sat, ultimo_error, eliminado_en");

Resultado<PaquetePersistido, ErrorPersistencia> leerPaquete(const QSqlQuery& q)
{
    using R = Resultado<PaquetePersistido, ErrorPersistencia>;
    auto ilegible = [](QStringView columna) { return R::fallo(sqlite::filaIlegible(kTabla, columna)); };

    PaquetePersistido p;
    p.id = q.value(0).toString();
    const auto solicitud = SolicitudId::desdeTexto(q.value(1).toString());
    if (!solicitud) {
        return ilegible(u"solicitud_masiva_id");
    }
    p.solicitudMasivaId = *solicitud;
    p.idPaqueteSat = q.value(2).toString();
    const auto estado = estadoDescargaDesdeClave(q.value(3).toString());
    if (!estado) {
        return ilegible(u"estado_descarga");
    }
    p.estadoDescarga = *estado;
    p.rutaLocal = sqlite::leerTextoOpcional(q.value(4));
    if (!sqlite::leerInstante(q.value(5), p.disponibleEn)) {
        return ilegible(u"disponible_en");
    }
    if (!sqlite::leerInstanteOpcional(q.value(6), p.descargaIniciadaEn)) {
        return ilegible(u"descarga_iniciada_en");
    }
    if (!sqlite::leerInstanteOpcional(q.value(7), p.descargadoEn)) {
        return ilegible(u"descargado_en");
    }
    if (!sqlite::leerInstanteOpcional(q.value(8), p.vencimientoEstimadoEn)) {
        return ilegible(u"vencimiento_estimado_en");
    }
    if (!sqlite::leerInstanteOpcional(q.value(9), p.vencidoEn)) {
        return ilegible(u"vencido_en");
    }
    if (const auto motivo = sqlite::leerTextoOpcional(q.value(10))) {
        const auto m = motivoVencimientoDesdeClave(*motivo);
        if (!m) {
            return ilegible(u"motivo_vencimiento");
        }
        p.motivoVencimiento = *m;
    }
    if (const auto origen = sqlite::leerTextoOpcional(q.value(11))) {
        const auto o = origenVencimientoDesdeClave(*origen);
        if (!o) {
            return ilegible(u"origen_vencimiento");
        }
        p.origenVencimiento = *o;
    }
    if (!sqlite::leerInstanteOpcional(q.value(12), p.reconciliadoEn)) {
        return ilegible(u"reconciliado_en");
    }
    p.codigoDescargaSat = sqlite::leerTextoOpcional(q.value(13));
    p.mensajeDescargaSat = sqlite::leerTextoOpcional(q.value(14));
    p.ultimoError = sqlite::leerTextoOpcional(q.value(15));
    if (!sqlite::leerInstanteOpcional(q.value(16), p.eliminadoEn)) {
        return ilegible(u"eliminado_en");
    }
    return R::exito(std::move(p));
}

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
            QStringLiteral("SELECT ") + kColumnas
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
        auto fila = leerPaquete(q);
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
