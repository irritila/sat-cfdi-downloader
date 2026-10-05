#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteFilas.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QSqlQuery>

#include <utility>

namespace satcfdi {

namespace {

constexpr QStringView kTabla = u"solicitud_masiva";

} // namespace

SqliteSolicitudMasivaRepository::SqliteSolicitudMasivaRepository(SqliteConnectionProvider& proveedor)
    : m_proveedor(proveedor)
{
}

Resultado<QList<SolicitudPersistida>, ErrorPersistencia> SqliteSolicitudMasivaRepository::listarVisibles()
{
    using R = Resultado<QList<SolicitudPersistida>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"solicitud_masiva.listar";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = sqlite::ejecutarDirecto(
            q,
            QStringLiteral("SELECT ") + sqlite::columnasSolicitud()
                + QStringLiteral(" FROM solicitud_masiva WHERE eliminado_en IS NULL "
                                 "ORDER BY creada_en DESC, id DESC"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    QList<SolicitudPersistida> lista;
    while (q.next()) {
        auto fila = sqlite::leerSolicitud(q);
        if (!fila) {
            return R::fallo(std::move(fila).error());
        }
        lista.append(std::move(fila).valor());
    }
    return R::exito(std::move(lista));
}

Resultado<std::optional<SolicitudPersistida>, ErrorPersistencia>
SqliteSolicitudMasivaRepository::obtenerVisible(const SolicitudId& id)
{
    using R = Resultado<std::optional<SolicitudPersistida>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"solicitud_masiva.obtener";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = sqlite::preparar(q,
                                  QStringLiteral("SELECT ") + sqlite::columnasSolicitud()
                                      + QStringLiteral(" FROM solicitud_masiva WHERE id = :id "
                                                       "AND eliminado_en IS NULL"),
                                  kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":id"), sqlite::texto(id.texto()));
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    if (!q.next()) {
        return R::exito(std::nullopt);
    }
    auto fila = sqlite::leerSolicitud(q);
    if (!fila) {
        return R::fallo(std::move(fila).error());
    }
    return R::exito(std::optional<SolicitudPersistida>(std::move(fila).valor()));
}

Resultado<EvaluacionDuplicado, ErrorPersistencia>
SqliteSolicitudMasivaRepository::clasificarDuplicado(const DedupKey& clave)
{
    using R = Resultado<EvaluacionDuplicado, ErrorPersistencia>;
    constexpr QStringView kContexto = u"solicitud_masiva.clasificar_duplicado";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();

    // 1. Todas las coincidencias, incluidas eliminadas (ix_solicitud_masiva_dedup).
    QList<CoincidenciaDuplicado> coincidencias;
    {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (auto r = sqlite::preparar(
                q,
                QStringLiteral("SELECT id, estado_local, estado_solicitud_sat, eliminado_en "
                               "FROM solicitud_masiva WHERE dedup_key = :clave "
                               "ORDER BY creada_en DESC, id DESC"),
                kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        q.bindValue(QStringLiteral(":clave"), sqlite::texto(clave.texto()));
        if (auto r = sqlite::ejecutar(q, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        while (q.next()) {
            CoincidenciaDuplicado c;
            const auto id = SolicitudId::desdeTexto(q.value(0).toString());
            if (!id) {
                return R::fallo(sqlite::filaIlegible(kTabla, u"id"));
            }
            c.id = *id;
            const auto estadoLocal = estadoLocalDesdeClave(q.value(1).toString());
            if (!estadoLocal) {
                return R::fallo(sqlite::filaIlegible(kTabla, u"estado_local"));
            }
            c.estadoLocal = *estadoLocal;
            if (!q.value(2).isNull()) {
                const auto estadoSat = estadoSolicitudSatDesdeClave(q.value(2).toString());
                if (!estadoSat) {
                    return R::fallo(sqlite::filaIlegible(kTabla, u"estado_solicitud_sat"));
                }
                c.estadoSat = *estadoSat;
            }
            c.eliminada = !q.value(3).isNull();
            coincidencias.append(std::move(c));
        }
    }

    // 2. Paquetes no eliminados de las coincidencias visibles Terminada.
    for (CoincidenciaDuplicado& c : coincidencias) {
        if (c.eliminada || c.estadoSat != EstadoSolicitudSat::Terminada) {
            continue;
        }
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (auto r = sqlite::preparar(
                q,
                QStringLiteral("SELECT estado_descarga FROM paquete_solicitud "
                               "WHERE solicitud_masiva_id = :id AND eliminado_en IS NULL"),
                kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        q.bindValue(QStringLiteral(":id"), sqlite::texto(c.id.texto()));
        if (auto r = sqlite::ejecutar(q, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        while (q.next()) {
            const auto estado = estadoDescargaDesdeClave(q.value(0).toString());
            if (!estado) {
                return R::fallo(sqlite::filaIlegible(u"paquete_solicitud", u"estado_descarga"));
            }
            c.paquetesNoEliminados.append(*estado);
        }
    }

    // 3. La matriz vive en dominio.
    return R::exito(clasificarCoincidencias(clave, coincidencias));
}

Resultado<Exito, ErrorPersistencia>
SqliteSolicitudMasivaRepository::insertarCreada(const SolicitudNuevaPersistida& solicitud)
{
    using R = Resultado<Exito, ErrorPersistencia>;
    constexpr QStringView kContexto = u"solicitud_masiva.insertar";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    const SolicitudCanonica& c = solicitud.canonica;
    QSqlQuery q(db);
    // Solo columnas de la solicitud local; el resto queda NULL o con el
    // DEFAULT del esquema (estado SAT, id_solicitud_sat, codigos, mensajes,
    // contadores y banderas).
    if (auto r = sqlite::preparar(
            q,
            QStringLiteral(
                "INSERT INTO solicitud_masiva (id, perfil_sat_id, tipo_cfdi, operacion_sat, "
                "rfc_solicitante, rfc_emisor, rfc_receptor, rfc_receptores_json, "
                "tipo_solicitud_sat, estado_comprobante_sat, fecha_inicial_sat, fecha_final_sat, "
                "tipo_comprobante, complemento, dedup_key, estado_local, creada_en) VALUES "
                "(:id, :perfil_sat_id, :tipo_cfdi, :operacion_sat, :rfc_solicitante, "
                ":rfc_emisor, :rfc_receptor, :rfc_receptores_json, :tipo_solicitud_sat, "
                ":estado_comprobante_sat, :fecha_inicial_sat, :fecha_final_sat, "
                ":tipo_comprobante, :complemento, :dedup_key, 'Creada', :creada_en)"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":id"), sqlite::texto(solicitud.id.texto()));
    q.bindValue(QStringLiteral(":perfil_sat_id"), sqlite::texto(solicitud.perfilSatId.texto()));
    q.bindValue(QStringLiteral(":tipo_cfdi"), sqlite::texto(c.tipoCfdi()));
    q.bindValue(QStringLiteral(":operacion_sat"), sqlite::texto(claveEstable(c.operacionSat())));
    q.bindValue(QStringLiteral(":rfc_solicitante"), sqlite::texto(c.rfcSolicitante()));
    q.bindValue(QStringLiteral(":rfc_emisor"), sqlite::textoOpcional(c.rfcEmisor()));
    q.bindValue(QStringLiteral(":rfc_receptor"), sqlite::textoOpcional(c.rfcReceptor()));
    q.bindValue(QStringLiteral(":rfc_receptores_json"),
                sqlite::textoOpcional(c.rfcReceptoresJson()));
    q.bindValue(QStringLiteral(":tipo_solicitud_sat"), sqlite::texto(c.tipoSolicitudSat()));
    q.bindValue(QStringLiteral(":estado_comprobante_sat"),
                sqlite::texto(c.estadoComprobanteSat()));
    q.bindValue(QStringLiteral(":fecha_inicial_sat"), sqlite::texto(c.fechaInicialSat()));
    q.bindValue(QStringLiteral(":fecha_final_sat"), sqlite::texto(c.fechaFinalSat()));
    q.bindValue(QStringLiteral(":tipo_comprobante"), sqlite::textoOpcional(c.tipoComprobante()));
    q.bindValue(QStringLiteral(":complemento"), sqlite::textoOpcional(c.complemento()));
    q.bindValue(QStringLiteral(":dedup_key"), sqlite::texto(c.dedupKey().texto()));
    q.bindValue(QStringLiteral(":creada_en"), sqlite::instante(solicitud.creadaEn));
    return sqlite::ejecutar(q, kContexto);
}

Resultado<bool, ErrorPersistencia>
SqliteSolicitudMasivaRepository::marcarEliminadaVisible(const SolicitudId& id,
                                                        const QDateTime& eliminadoEn)
{
    using R = Resultado<bool, ErrorPersistencia>;
    constexpr QStringView kContexto = u"solicitud_masiva.marcar_eliminada";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (auto r = sqlite::preparar(q,
                                  QStringLiteral("UPDATE solicitud_masiva SET eliminado_en = "
                                                 ":eliminado_en WHERE id = :id "
                                                 "AND eliminado_en IS NULL"),
                                  kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":eliminado_en"), sqlite::instante(eliminadoEn));
    q.bindValue(QStringLiteral(":id"), sqlite::texto(id.texto()));
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    return R::exito(q.numRowsAffected() > 0);
}

} // namespace satcfdi
