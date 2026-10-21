#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteFilas.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include "domain/cfdi/CoberturaMensual.h"
#include "domain/common/UuidCanonico.h"

#include <QSqlQuery>

#include <algorithm>
#include <utility>

// T023.2: lote_descarga_guiada y lote_descarga_guiada_solicitud (migracion
// 007, ADR 0019). Ningun valor de fila sale en errores ni logs.

namespace satcfdi {

namespace {

ErrorPersistencia integridad(QStringView contexto, const char* detalle)
{
    return ErrorPersistencia::de(ErrorPersistencia::Tipo::Integridad,
                                 contexto.toString() + QStringLiteral(": ") + QString::fromLatin1(detalle));
}

// Ejecuta `sql` con `valores` (marcador -> valor) y deja `q` posicionada antes
// de la primera fila.
Resultado<Exito, ErrorPersistencia> correr(QSqlQuery& q, const QString& sql,
                                           std::initializer_list<std::pair<const char*, QVariant>> valores,
                                           QStringView contexto)
{
    if (auto r = sqlite::preparar(q, sql, contexto); !r) {
        return r;
    }
    for (const auto& [marcador, valor] : valores) {
        q.bindValue(QString::fromLatin1(marcador), valor);
    }
    return sqlite::ejecutar(q, contexto);
}

} // namespace

SqliteLoteDescargaGuiadaRepository::SqliteLoteDescargaGuiadaRepository(SqliteConnectionProvider& proveedor)
    : m_proveedor(proveedor)
{
}

Resultado<Exito, ErrorPersistencia> SqliteLoteDescargaGuiadaRepository::insertar(const NuevoLoteGuiado& lote)
{
    using R = Resultado<Exito, ErrorPersistencia>;
    constexpr QStringView kContexto = u"lote_descarga_guiada.insertar";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    if (!uuid::esCanonico(lote.id) || lote.perfil.esNulo() || !lote.creadoEn.isValid() || lote.tramos.isEmpty()) {
        return R::fallo(integridad(kContexto, "lote incoherente"));
    }
    // ADR 0020: varias solicitudes por direccion (una por hueco), sin
    // traslape entre los tramos de una misma direccion. Las fechas SAT tienen
    // formato fijo: comparan como texto.
    for (TipoDescarga direccion : {TipoDescarga::Emitidos, TipoDescarga::Recibidos}) {
        QList<TramoLoteGuiado> tramos;
        for (const TramoLoteGuiado& t : lote.tramos) {
            if (t.direccion == direccion) {
                tramos.append(t);
            }
        }
        std::sort(tramos.begin(), tramos.end(), [](const TramoLoteGuiado& a, const TramoLoteGuiado& b) {
            return a.fechaInicialSat < b.fechaInicialSat;
        });
        for (qsizetype i = 1; i < tramos.size(); ++i) {
            if (tramos.at(i).fechaInicialSat <= tramos.at(i - 1).fechaFinalSat) {
                return R::fallo(integridad(kContexto, "tramos traslapados en una direccion"));
            }
        }
    }
    QSqlDatabase db = conexion.valor();
    const QVariant perfil = sqlite::texto(lote.perfil.texto());

    // 1. Perfil visible y cada solicitud visible, canonica del mes del lote
    //    (esSolicitudCanonicaDelMes: mismo perfil, CFDI/Vigente, sin filtros
    //    SAT, tramo dentro del mes) y con la direccion y el tramo indicados
    //    (revalidado bajo BEGIN IMMEDIATE).
    {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (auto r = correr(q, QStringLiteral("SELECT 1 FROM perfil_sat WHERE id = :perfil AND eliminado_en IS NULL"),
                            {{":perfil", perfil}}, kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        if (!q.next()) {
            return R::fallo(integridad(kContexto, "perfil no visible"));
        }
    }
    for (const TramoLoteGuiado& t : lote.tramos) {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (auto r = correr(q,
                            QStringLiteral("SELECT ") + sqlite::columnasSolicitud()
                                + QStringLiteral(" FROM solicitud_masiva WHERE id = :id AND eliminado_en IS NULL"),
                            {{":id", sqlite::texto(t.solicitud.texto())}}, kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        if (!q.next()) {
            return R::fallo(integridad(kContexto, "solicitud del lote no visible"));
        }
        auto s = sqlite::leerSolicitud(q);
        if (!s) {
            return R::fallo(std::move(s).error());
        }
        const SolicitudPersistida& sol = s.valor();
        if (!esSolicitudCanonicaDelMes(sol, lote.perfil, lote.mes) || sol.tipoCfdi != t.direccion
            || sol.fechaInicialSat != t.fechaInicialSat || sol.fechaFinalSat != t.fechaFinalSat) {
            return R::fallo(integridad(kContexto, "solicitud del lote no canonica o incoherente"));
        }
    }

    // 2. Lote y tramos.
    {
        QSqlQuery q(db);
        if (auto r = correr(q,
                            QStringLiteral("INSERT INTO lote_descarga_guiada (id, perfil_sat_id, mes, creado_en) "
                                           "VALUES (:id, :perfil, :mes, :creado)"),
                            {{":id", sqlite::texto(lote.id)},
                             {":perfil", perfil},
                             {":mes", sqlite::texto(lote.mes.texto())},
                             {":creado", sqlite::instante(lote.creadoEn)}},
                            kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
    }
    for (const TramoLoteGuiado& t : lote.tramos) {
        QSqlQuery q(db);
        if (auto r = correr(q,
                            QStringLiteral("INSERT INTO lote_descarga_guiada_solicitud (lote_id, solicitud_masiva_id, "
                                           "tipo_cfdi, fecha_inicial_sat, fecha_final_sat) "
                                           "VALUES (:lote, :solicitud, :tipo, :inicial, :final)"),
                            {{":lote", sqlite::texto(lote.id)},
                             {":solicitud", sqlite::texto(t.solicitud.texto())},
                             {":tipo", sqlite::texto(valorTipoCfdi(t.direccion))},
                             {":inicial", sqlite::texto(t.fechaInicialSat)},
                             {":final", sqlite::texto(t.fechaFinalSat)}},
                            kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
    }
    return R::exito({});
}

Resultado<QList<LoteGuiado>, ErrorPersistencia>
SqliteLoteDescargaGuiadaRepository::listar(const PerfilId& perfil, const std::optional<MesCalendario>& mes)
{
    using R = Resultado<QList<LoteGuiado>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"lote_descarga_guiada.listar";
    constexpr QStringView kTabla = u"lote_descarga_guiada";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    QString sql = QStringLiteral(
        "SELECT l.id, l.mes, l.creado_en, t.tipo_cfdi, t.solicitud_masiva_id, t.fecha_inicial_sat, "
        "t.fecha_final_sat, s.eliminado_en, s.estado_local, s.estado_solicitud_sat "
        "FROM lote_descarga_guiada l "
        "JOIN perfil_sat ps ON ps.id = l.perfil_sat_id AND ps.eliminado_en IS NULL "
        "LEFT JOIN lote_descarga_guiada_solicitud t ON t.lote_id = l.id "
        "LEFT JOIN solicitud_masiva s ON s.id = t.solicitud_masiva_id "
        "WHERE l.perfil_sat_id = :perfil");
    if (mes) {
        sql += QStringLiteral(" AND l.mes = :mes");
    }
    sql += QStringLiteral(" ORDER BY l.creado_en DESC, l.id DESC, t.tipo_cfdi, t.fecha_inicial_sat");
    if (auto r = sqlite::preparar(q, sql, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":perfil"), sqlite::texto(perfil.texto()));
    if (mes) {
        q.bindValue(QStringLiteral(":mes"), sqlite::texto(mes->texto()));
    }
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }

    QList<LoteGuiado> lotes;
    QList<QList<EstadoResumen>> vigentes;
    while (q.next()) {
        const QString id = q.value(0).toString();
        if (lotes.isEmpty() || lotes.constLast().id != id) {
            LoteGuiado lote;
            lote.id = id;
            lote.perfil = perfil;
            const auto m = MesCalendario::desdeTexto(q.value(1).toString());
            if (!m) {
                return R::fallo(sqlite::filaIlegible(kTabla, u"mes"));
            }
            lote.mes = *m;
            if (!sqlite::leerInstante(q.value(2), lote.creadoEn)) {
                return R::fallo(sqlite::filaIlegible(kTabla, u"creado_en"));
            }
            lotes.append(std::move(lote));
            vigentes.append(QList<EstadoResumen>());
        }
        if (q.value(4).isNull()) {
            continue; // lote sin tramos (no lo produce insertar)
        }
        TramoLoteConsultado t;
        const auto direccion = tipoDescargaDesdeTipoCfdi(q.value(3).toString());
        const auto solicitud = SolicitudId::desdeTexto(q.value(4).toString());
        if (!direccion || !solicitud) {
            return R::fallo(sqlite::filaIlegible(u"lote_descarga_guiada_solicitud", u"tipo_cfdi"));
        }
        t.tramo = TramoLoteGuiado{*direccion, *solicitud, q.value(5).toString(), q.value(6).toString()};
        t.eliminada = !q.value(7).isNull();
        if (!t.eliminada) {
            const auto local = estadoLocalDesdeClave(q.value(8).toString());
            if (!local) {
                return R::fallo(sqlite::filaIlegible(u"solicitud_masiva", u"estado_local"));
            }
            std::optional<EstadoSolicitudSat> sat;
            if (!q.value(9).isNull()) {
                sat = estadoSolicitudSatDesdeClave(q.value(9).toString());
                if (!sat) {
                    return R::fallo(sqlite::filaIlegible(u"solicitud_masiva", u"estado_solicitud_sat"));
                }
            }
            t.estado = derivarEstadoResumen(*local, sat);
            vigentes.last().append(*t.estado);
        }
        LoteGuiado& lote = lotes.last();
        lote.eliminacionParcial = lote.eliminacionParcial || t.eliminada;
        lote.tramos.append(std::move(t));
    }
    for (qsizetype i = 0; i < lotes.size(); ++i) {
        lotes[i].resultado = resultadoLote(vigentes.at(i));
    }
    return R::exito(std::move(lotes));
}

Resultado<QList<SolicitudSinLote>, ErrorPersistencia>
SqliteLoteDescargaGuiadaRepository::listarSinLote(const PerfilId& perfil)
{
    using R = Resultado<QList<SolicitudSinLote>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"lote_descarga_guiada.listar_sin_lote";
    constexpr QStringView kTabla = u"solicitud_masiva";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlQuery q(conexion.valor());
    q.setForwardOnly(true);
    const QString sql = QStringLiteral(
        "SELECT s.id, s.tipo_cfdi, s.fecha_inicial_sat, s.fecha_final_sat, s.creada_en, "
        "(s.tipo_comprobante IS NOT NULL OR s.complemento IS NOT NULL OR s.rfc_receptores_json IS NOT NULL "
        "OR (s.tipo_cfdi = 'emitidos' AND s.rfc_receptor IS NOT NULL) "
        "OR (s.tipo_cfdi = 'recibidos' AND s.rfc_emisor IS NOT NULL)) AS con_filtros "
        "FROM solicitud_masiva s "
        "JOIN perfil_sat ps ON ps.id = s.perfil_sat_id AND ps.eliminado_en IS NULL "
        "WHERE s.perfil_sat_id = :perfil AND s.eliminado_en IS NULL "
        "AND NOT EXISTS (SELECT 1 FROM lote_descarga_guiada_solicitud g WHERE g.solicitud_masiva_id = s.id) "
        "ORDER BY s.creada_en DESC, s.id DESC");
    if (auto r = correr(q, sql, {{":perfil", sqlite::texto(perfil.texto())}}, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    QList<SolicitudSinLote> lista;
    while (q.next()) {
        SolicitudSinLote s;
        const auto id = SolicitudId::desdeTexto(q.value(0).toString());
        const auto direccion = tipoDescargaDesdeTipoCfdi(q.value(1).toString());
        if (!id || !direccion) {
            return R::fallo(sqlite::filaIlegible(kTabla, u"tipo_cfdi"));
        }
        s.id = *id;
        s.direccion = *direccion;
        s.fechaInicialSat = q.value(2).toString();
        s.fechaFinalSat = q.value(3).toString();
        if (!sqlite::leerInstante(q.value(4), s.creadaEn)) {
            return R::fallo(sqlite::filaIlegible(kTabla, u"creada_en"));
        }
        s.conFiltros = q.value(5).toInt() != 0;
        lista.append(std::move(s));
    }
    return R::exito(std::move(lista));
}

Resultado<std::optional<UbicacionSolicitud>, ErrorPersistencia>
SqliteLoteDescargaGuiadaRepository::ubicar(const SolicitudId& solicitud)
{
    using R = Resultado<std::optional<UbicacionSolicitud>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"lote_descarga_guiada.ubicar";
    if (solicitud.esNulo()) {
        return R::exito(std::nullopt);
    }
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlQuery q(conexion.valor());
    q.setForwardOnly(true);
    // El lote solo cuenta si es del mismo perfil que la solicitud.
    const QString sql = QStringLiteral(
        "SELECT s.perfil_sat_id, l.mes FROM solicitud_masiva s "
        "JOIN perfil_sat ps ON ps.id = s.perfil_sat_id AND ps.eliminado_en IS NULL "
        "LEFT JOIN lote_descarga_guiada_solicitud g ON g.solicitud_masiva_id = s.id "
        "LEFT JOIN lote_descarga_guiada l ON l.id = g.lote_id AND l.perfil_sat_id = s.perfil_sat_id "
        "WHERE s.id = :id AND s.eliminado_en IS NULL");
    if (auto r = correr(q, sql, {{":id", sqlite::texto(solicitud.texto())}}, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    if (!q.next()) {
        return R::exito(std::nullopt);
    }
    UbicacionSolicitud u;
    u.solicitud = solicitud;
    const auto perfil = PerfilId::desdeTexto(q.value(0).toString());
    if (!perfil) {
        return R::fallo(sqlite::filaIlegible(u"solicitud_masiva", u"perfil_sat_id"));
    }
    u.perfil = *perfil;
    if (!q.value(1).isNull()) {
        const auto mes = MesCalendario::desdeTexto(q.value(1).toString());
        if (!mes) {
            return R::fallo(sqlite::filaIlegible(u"lote_descarga_guiada", u"mes"));
        }
        u.mesLote = *mes;
    }
    return R::exito(std::move(u));
}

} // namespace satcfdi
