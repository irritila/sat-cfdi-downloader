#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteFilas.h"
#include "infrastructure/persistence/sqlite/SqliteOperacionesConsultas.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>

#include <initializer_list>
#include <utility>

// OperacionesSolicitudRepository sobre SQLite (T007, corte B2).
//
// Selecciones (D9): acotadas con LIMIT y apoyadas en los indices parciales de
// 001 (verificado con EXPLAIN QUERY PLAN en TestSqliteOperaciones):
// - ix_solicitud_masiva_monitoreo: verificaciones debidas, interrumpidos.
// - ix_solicitud_masiva_accion_pendiente: intenciones.
// - ix_paquete_solicitud_vencimiento_estimado: vencimientos estimados.
// - ix_paquete_solicitud_descarga: descargas automaticas, Descargando.
// - ux_paquete_solicitud_id_paquete_sat: paquetes de una solicitud y registro
//   de paquetes nuevos sin duplicar.
// Los perfiles se filtran con json_each(:perfiles) (un solo parametro).
//
// Escrituras: exigen UnitOfWork (BEGIN IMMEDIATE). Cada UPDATE revalida en su
// WHERE eliminado_en IS NULL (solicitud y, en paquetes, tambien la solicitud
// padre via EXISTS) y el estado de origen; 0 filas afectadas -> false sin
// cambios. Los CHECK de 001-003 son la ultima defensa: una violacion se
// traduce a Integridad y el llamador revierte la transaccion.
namespace satcfdi {

namespace {

using Binds = std::initializer_list<std::pair<const char*, QVariant>>;

ErrorPersistencia argumentoInvalido(QStringView contexto, const char* detalle)
{
    return ErrorPersistencia::de(ErrorPersistencia::Tipo::Interno,
                                 contexto.toString() + QStringLiteral(": argumento invalido (")
                                     + QString::fromLatin1(detalle) + QLatin1Char(')'));
}

Resultado<Exito, ErrorPersistencia> correr(QSqlQuery& q, const QString& sql, Binds binds, QStringView contexto)
{
    if (auto r = sqlite::preparar(q, sql, contexto); !r) {
        return r;
    }
    for (const auto& [nombre, valor] : binds) {
        q.bindValue(QString::fromLatin1(nombre), valor);
    }
    return sqlite::ejecutar(q, contexto);
}

QVariant perfilesJson(const QList<PerfilId>& perfiles)
{
    QJsonArray arreglo;
    for (const PerfilId& p : perfiles) {
        arreglo.append(p.texto());
    }
    return sqlite::texto(QString::fromUtf8(QJsonDocument(arreglo).toJson(QJsonDocument::Compact)));
}

// Seleccion de paquete con los datos de su solicitud (ambos visibles).
// Columnas: paquete (posicional, kNumColumnasPaquete) + perfil_sat_id,
// rfc_solicitante, id_solicitud_sat.
QString selectDescargable()
{
    return QStringLiteral("SELECT ") + sqlite::columnasPaquete(u"p")
           + QStringLiteral(", s.perfil_sat_id, s.rfc_solicitante, s.id_solicitud_sat "
                            "FROM paquete_solicitud p "
                            "JOIN solicitud_masiva s ON s.id = p.solicitud_masiva_id "
                            "WHERE p.eliminado_en IS NULL AND s.eliminado_en IS NULL ");
}

Resultado<PaqueteDescargable, ErrorPersistencia> leerDescargable(const QSqlQuery& q)
{
    using R = Resultado<PaqueteDescargable, ErrorPersistencia>;
    auto paquete = sqlite::leerPaquete(q, 0);
    if (!paquete) {
        return R::fallo(std::move(paquete).error());
    }
    PaqueteDescargable d;
    d.paquete = std::move(paquete).valor();
    constexpr int base = sqlite::kNumColumnasPaquete;
    const auto perfil = PerfilId::desdeTexto(q.value(base).toString());
    if (!perfil) {
        return R::fallo(sqlite::filaIlegible(u"solicitud_masiva", u"perfil_sat_id"));
    }
    d.perfilSatId = *perfil;
    d.rfcSolicitante = q.value(base + 1).toString();
    d.idSolicitudSat = sqlite::leerTextoOpcional(q.value(base + 2)).value_or(QString());
    return R::exito(std::move(d));
}

Resultado<QList<PaqueteDescargable>, ErrorPersistencia> leerDescargables(QSqlQuery& q)
{
    using R = Resultado<QList<PaqueteDescargable>, ErrorPersistencia>;
    QList<PaqueteDescargable> lista;
    while (q.next()) {
        auto fila = leerDescargable(q);
        if (!fila) {
            return R::fallo(std::move(fila).error());
        }
        lista.append(std::move(fila).valor());
    }
    return R::exito(std::move(lista));
}

Resultado<QList<PaqueteDescargable>, ErrorPersistencia>
consultarDescargables(SqliteConnectionProvider& proveedor, const QString& sql, Binds binds, QStringView contexto)
{
    using R = Resultado<QList<PaqueteDescargable>, ErrorPersistencia>;
    auto conexion = sqlite::conexionLectura(proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = correr(q, sql, binds, contexto); !r) {
        return R::fallo(std::move(r).error());
    }
    return leerDescargables(q);
}

// UPDATE de escritura (exige tx) que devuelve si afecto alguna fila.
Resultado<bool, ErrorPersistencia> actualizar(SqliteConnectionProvider& proveedor, const QString& sql, Binds binds,
                                              QStringView contexto)
{
    using R = Resultado<bool, ErrorPersistencia>;
    auto conexion = sqlite::conexionEscritura(proveedor, contexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (auto r = correr(q, sql, binds, contexto); !r) {
        return R::fallo(std::move(r).error());
    }
    return R::exito(q.numRowsAffected() > 0);
}

// Condicion de solicitud padre visible para escrituras sobre paquete_solicitud.
const QString kSolicitudPadreVisible = QStringLiteral(
    "EXISTS (SELECT 1 FROM solicitud_masiva s WHERE s.id = paquete_solicitud.solicitud_masiva_id "
    "AND s.eliminado_en IS NULL)");

// Verificable por el worker: Enviada con estado SAT nulo, Aceptada o EnProceso.
const QString kVerificable = QStringLiteral(
    "estado_local = 'Enviada' "
    "AND (estado_solicitud_sat IS NULL OR estado_solicitud_sat IN ('Aceptada', 'EnProceso')) ");

} // namespace

namespace sqlite::operaciones {

QString sqlPerfilesConTrabajo()
{
    // UNION ALL en subconsulta + DISTINCT: cada rama usa su indice parcial
    // (un UNION directo hace MERGE ordenado y el planificador recorre
    // ix_solicitud_masiva_perfil completo para la rama de intenciones).
    return QStringLiteral("SELECT DISTINCT perfil_sat_id FROM ("
                          "SELECT perfil_sat_id FROM solicitud_masiva WHERE eliminado_en IS NULL AND ")
           + kVerificable
           + QStringLiteral("AND siguiente_verificacion_en IS NOT NULL AND siguiente_verificacion_en <= :ahora "
                            "UNION ALL "
                            "SELECT perfil_sat_id FROM solicitud_masiva "
                            "WHERE eliminado_en IS NULL AND accion_pendiente_en IS NOT NULL "
                            "UNION ALL "
                            "SELECT s.perfil_sat_id FROM paquete_solicitud p "
                            "JOIN solicitud_masiva s ON s.id = p.solicitud_masiva_id "
                            "WHERE p.eliminado_en IS NULL AND p.estado_descarga = 'Disponible' "
                            "AND s.eliminado_en IS NULL AND s.estado_solicitud_sat = 'Terminada'"
                            ") ORDER BY perfil_sat_id");
}

QString sqlVencimientosEstimados()
{
    // Terminos identicos al WHERE de ix_paquete_solicitud_vencimiento_estimado.
    return selectDescargable()
           + QStringLiteral("AND p.vencimiento_estimado_en IS NOT NULL "
                            "AND p.estado_descarga IN ('Disponible', 'Descargando', 'Error') "
                            "AND p.vencimiento_estimado_en <= :ahora "
                            "ORDER BY p.vencimiento_estimado_en ASC, p.id ASC LIMIT :limite");
}

QString sqlIntenciones()
{
    return QStringLiteral("SELECT id, perfil_sat_id, verificacion_pendiente, descarga_pendiente, "
                          "accion_pendiente_en FROM solicitud_masiva "
                          "WHERE eliminado_en IS NULL AND accion_pendiente_en IS NOT NULL "
                          "AND perfil_sat_id IN (SELECT value FROM json_each(:perfiles)) "
                          "ORDER BY accion_pendiente_en ASC, id ASC LIMIT :limite");
}

QString sqlVerificacionesDebidas()
{
    return QStringLiteral("SELECT ") + columnasSolicitud()
           + QStringLiteral(" FROM solicitud_masiva WHERE eliminado_en IS NULL AND ") + kVerificable
           + QStringLiteral("AND siguiente_verificacion_en IS NOT NULL "
                            "AND siguiente_verificacion_en <= :ahora "
                            "AND perfil_sat_id IN (SELECT value FROM json_each(:perfiles)) "
                            "ORDER BY siguiente_verificacion_en ASC, id ASC LIMIT :limite");
}

QString sqlDescargasAutomaticas()
{
    return selectDescargable()
           + QStringLiteral("AND p.estado_descarga = 'Disponible' AND s.estado_solicitud_sat = 'Terminada' "
                            "AND s.perfil_sat_id IN (SELECT value FROM json_each(:perfiles)) "
                            "ORDER BY p.disponible_en ASC, p.id ASC LIMIT :limite");
}

QString sqlObtenerPaquete()
{
    return selectDescargable() + QStringLiteral("AND p.id = :id");
}

QString sqlPaquetesReintentables()
{
    return selectDescargable()
           + QStringLiteral("AND p.solicitud_masiva_id = :id AND p.estado_descarga IN ('Disponible', 'Error') "
                            "ORDER BY p.disponible_en ASC, p.id_paquete_sat ASC");
}

QString sqlSolicitudesEnviando()
{
    return QStringLiteral("SELECT id FROM solicitud_masiva WHERE eliminado_en IS NULL "
                          "AND estado_local = 'Enviando' ORDER BY envio_iniciado_en ASC, id ASC");
}

QString sqlPaquetesDescargando()
{
    return selectDescargable()
           + QStringLiteral("AND p.estado_descarga = 'Descargando' ORDER BY p.descarga_iniciada_en ASC, p.id ASC");
}

} // namespace sqlite::operaciones

using namespace sqlite::operaciones;

SqliteOperacionesSolicitudRepository::SqliteOperacionesSolicitudRepository(SqliteConnectionProvider& proveedor)
    : m_proveedor(proveedor)
{
}

// --- Seleccion -----------------------------------------------------------------

Resultado<QList<PerfilId>, ErrorPersistencia>
SqliteOperacionesSolicitudRepository::listarPerfilesConTrabajo(const QDateTime& ahoraUtc)
{
    using R = Resultado<QList<PerfilId>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"operaciones.perfiles_con_trabajo";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    const QString sql = sqlPerfilesConTrabajo();
    if (auto r = correr(q, sql, {{":ahora", sqlite::instante(ahoraUtc)}}, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    QList<PerfilId> perfiles;
    while (q.next()) {
        const auto perfil = PerfilId::desdeTexto(q.value(0).toString());
        if (!perfil) {
            return R::fallo(sqlite::filaIlegible(u"solicitud_masiva", u"perfil_sat_id"));
        }
        perfiles.append(*perfil);
    }
    return R::exito(std::move(perfiles));
}

Resultado<QList<PaqueteDescargable>, ErrorPersistencia>
SqliteOperacionesSolicitudRepository::listarVencimientosEstimados(const QDateTime& ahoraUtc, int limite)
{
    constexpr QStringView kContexto = u"operaciones.vencimientos_estimados";
    if (limite <= 0) {
        return Resultado<QList<PaqueteDescargable>, ErrorPersistencia>::fallo(argumentoInvalido(kContexto, "limite"));
    }
    return consultarDescargables(
        m_proveedor, sqlVencimientosEstimados(),
        {{":ahora", sqlite::instante(ahoraUtc)}, {":limite", sqlite::entero(limite)}}, kContexto);
}

Resultado<QList<IntencionPendiente>, ErrorPersistencia>
SqliteOperacionesSolicitudRepository::listarIntencionesPendientes(const QList<PerfilId>& perfiles, int limite)
{
    using R = Resultado<QList<IntencionPendiente>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"operaciones.intenciones";
    if (limite <= 0) {
        return R::fallo(argumentoInvalido(kContexto, "limite"));
    }
    if (perfiles.isEmpty()) {
        return R::exito({});
    }
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = correr(q,
                        sqlIntenciones(),
                        {{":perfiles", perfilesJson(perfiles)}, {":limite", sqlite::entero(limite)}}, kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    QList<IntencionPendiente> lista;
    while (q.next()) {
        IntencionPendiente i;
        const auto id = SolicitudId::desdeTexto(q.value(0).toString());
        const auto perfil = PerfilId::desdeTexto(q.value(1).toString());
        if (!id || !perfil) {
            return R::fallo(sqlite::filaIlegible(u"solicitud_masiva", !id ? u"id" : u"perfil_sat_id"));
        }
        i.solicitudId = *id;
        i.perfilSatId = *perfil;
        i.verificacionPendiente = q.value(2).toInt() != 0;
        i.descargaPendiente = q.value(3).toInt() != 0;
        if (!sqlite::leerInstante(q.value(4), i.accionPendienteEn)) {
            return R::fallo(sqlite::filaIlegible(u"solicitud_masiva", u"accion_pendiente_en"));
        }
        lista.append(std::move(i));
    }
    return R::exito(std::move(lista));
}

Resultado<QList<SolicitudPersistida>, ErrorPersistencia>
SqliteOperacionesSolicitudRepository::listarVerificacionesDebidas(const QList<PerfilId>& perfiles,
                                                                  const QDateTime& ahoraUtc, int limite)
{
    using R = Resultado<QList<SolicitudPersistida>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"operaciones.verificaciones_debidas";
    if (limite <= 0) {
        return R::fallo(argumentoInvalido(kContexto, "limite"));
    }
    if (perfiles.isEmpty()) {
        return R::exito({});
    }
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = correr(q,
                        sqlVerificacionesDebidas(),
                        {{":ahora", sqlite::instante(ahoraUtc)},
                         {":perfiles", perfilesJson(perfiles)},
                         {":limite", sqlite::entero(limite)}},
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

Resultado<QList<PaqueteDescargable>, ErrorPersistencia>
SqliteOperacionesSolicitudRepository::listarDescargasAutomaticas(const QList<PerfilId>& perfiles, int limite)
{
    using R = Resultado<QList<PaqueteDescargable>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"operaciones.descargas_automaticas";
    if (limite <= 0) {
        return R::fallo(argumentoInvalido(kContexto, "limite"));
    }
    if (perfiles.isEmpty()) {
        return R::exito({});
    }
    return consultarDescargables(
        m_proveedor,
        sqlDescargasAutomaticas(),
        {{":perfiles", perfilesJson(perfiles)}, {":limite", sqlite::entero(limite)}}, kContexto);
}

Resultado<std::optional<PaqueteDescargable>, ErrorPersistencia>
SqliteOperacionesSolicitudRepository::obtenerPaquete(const QString& paqueteId)
{
    using R = Resultado<std::optional<PaqueteDescargable>, ErrorPersistencia>;
    auto lista = consultarDescargables(m_proveedor, sqlObtenerPaquete(),
                                       {{":id", sqlite::texto(paqueteId)}}, u"operaciones.obtener_paquete");
    if (!lista) {
        return R::fallo(std::move(lista).error());
    }
    if (lista.valor().isEmpty()) {
        return R::exito(std::nullopt);
    }
    return R::exito(std::optional<PaqueteDescargable>(lista.valor().first()));
}

Resultado<QList<PaqueteDescargable>, ErrorPersistencia>
SqliteOperacionesSolicitudRepository::listarPaquetesReintentables(const SolicitudId& solicitudId)
{
    return consultarDescargables(
        m_proveedor,
        sqlPaquetesReintentables(),
        {{":id", sqlite::texto(solicitudId.texto())}}, u"operaciones.paquetes_reintentables");
}

Resultado<TrabajoInterrumpido, ErrorPersistencia> SqliteOperacionesSolicitudRepository::listarInterrumpidos()
{
    using R = Resultado<TrabajoInterrumpido, ErrorPersistencia>;
    constexpr QStringView kContexto = u"operaciones.interrumpidos";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    TrabajoInterrumpido t;
    {
        QSqlDatabase db = conexion.valor();
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (auto r = correr(q,
                            sqlSolicitudesEnviando(),
                            {}, kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        while (q.next()) {
            const auto id = SolicitudId::desdeTexto(q.value(0).toString());
            if (!id) {
                return R::fallo(sqlite::filaIlegible(u"solicitud_masiva", u"id"));
            }
            t.solicitudesEnviando.append(*id);
        }
    }
    auto paquetes = consultarDescargables(
        m_proveedor,
        sqlPaquetesDescargando(),
        {}, kContexto);
    if (!paquetes) {
        return R::fallo(std::move(paquetes).error());
    }
    t.paquetesDescargando = std::move(paquetes).valor();
    return R::exito(std::move(t));
}

Resultado<std::optional<RachaVerificacion>, ErrorPersistencia>
SqliteOperacionesSolicitudRepository::leerRachaVerificacion(const SolicitudId& solicitudId)
{
    using R = Resultado<std::optional<RachaVerificacion>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"operaciones.racha";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = correr(q,
                        QStringLiteral("SELECT verificaciones_sin_cambio, ultima_clave_falla_verificacion, "
                                       "fallas_verificacion_iguales FROM solicitud_masiva "
                                       "WHERE id = :id AND eliminado_en IS NULL"),
                        {{":id", sqlite::texto(solicitudId.texto())}}, kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    if (!q.next()) {
        return R::exito(std::nullopt);
    }
    RachaVerificacion racha;
    racha.verificacionesSinCambio = q.value(0).toInt();
    racha.ultimaClaveFalla = sqlite::leerTextoOpcional(q.value(1));
    racha.fallasIguales = q.value(2).toInt();
    return R::exito(std::optional<RachaVerificacion>(std::move(racha)));
}

// --- Envio ---------------------------------------------------------------------

Resultado<bool, ErrorPersistencia> SqliteOperacionesSolicitudRepository::marcarEnviando(const SolicitudId& solicitudId,
                                                                                        const QDateTime& ahoraUtc)
{
    return actualizar(m_proveedor,
                      QStringLiteral("UPDATE solicitud_masiva SET estado_local = 'Enviando', "
                                     "envio_iniciado_en = :ahora "
                                     "WHERE id = :id AND eliminado_en IS NULL AND estado_local = 'Creada'"),
                      {{":ahora", sqlite::instante(ahoraUtc)}, {":id", sqlite::texto(solicitudId.texto())}},
                      u"operaciones.marcar_enviando");
}

Resultado<bool, ErrorPersistencia> SqliteOperacionesSolicitudRepository::aplicarEnvio(const AplicacionEnvio& a)
{
    using R = Resultado<bool, ErrorPersistencia>;
    constexpr QStringView kContexto = u"operaciones.aplicar_envio";
    const QString kWhere =
        QStringLiteral(" WHERE id = :id AND eliminado_en IS NULL AND estado_local = 'Enviando'");
    const QVariant id = sqlite::texto(a.solicitudId.texto());
    switch (a.destino) {
    case EstadoLocal::Enviada:
        if (!a.idSolicitudSat || !a.codEstatus || !a.enviadaEn || !a.siguienteVerificacionEn) {
            return R::fallo(argumentoInvalido(kContexto, "Enviada incompleta"));
        }
        return actualizar(m_proveedor,
                          QStringLiteral("UPDATE solicitud_masiva SET estado_local = 'Enviada', "
                                         "id_solicitud_sat = :id_sat, cod_estatus_solicitud = :cod, "
                                         "mensaje_solicitud_sat = :mensaje, enviada_en = :enviada, "
                                         "siguiente_verificacion_en = :siguiente, ultimo_error = :error")
                              + kWhere,
                          {{":id_sat", sqlite::texto(*a.idSolicitudSat)},
                           {":cod", sqlite::texto(*a.codEstatus)},
                           {":mensaje", sqlite::textoOpcional(a.mensaje)},
                           {":enviada", sqlite::instante(*a.enviadaEn)},
                           {":siguiente", sqlite::instante(*a.siguienteVerificacionEn)},
                           {":error", sqlite::textoOpcional(a.ultimoError)},
                           {":id", id}},
                          kContexto);
    case EstadoLocal::Creada:
        // D7 / ADR 0017: regreso atomico y sin rastro de respuesta de creacion
        // (ck_solicitud_intento_envio, ck_solicitud_codigo_creacion).
        return actualizar(m_proveedor,
                          QStringLiteral("UPDATE solicitud_masiva SET estado_local = 'Creada', "
                                         "envio_iniciado_en = NULL, cod_estatus_solicitud = NULL, "
                                         "mensaje_solicitud_sat = NULL, ultimo_error = :error")
                              + kWhere,
                          {{":error", sqlite::textoOpcional(a.ultimoError)}, {":id", id}}, kContexto);
    case EstadoLocal::EnvioFallido:
    case EstadoLocal::EnvioIncierto:
        // Conserva envio_iniciado_en.
        return actualizar(m_proveedor,
                          QStringLiteral("UPDATE solicitud_masiva SET estado_local = :destino, "
                                         "cod_estatus_solicitud = :cod, mensaje_solicitud_sat = :mensaje, "
                                         "ultimo_error = :error")
                              + kWhere,
                          {{":destino", sqlite::texto(claveEstable(a.destino))},
                           {":cod", sqlite::textoOpcional(a.codEstatus)},
                           {":mensaje", sqlite::textoOpcional(a.mensaje)},
                           {":error", sqlite::textoOpcional(a.ultimoError)},
                           {":id", id}},
                          kContexto);
    case EstadoLocal::Enviando:
        break;
    }
    return R::fallo(argumentoInvalido(kContexto, "destino"));
}

// --- Verificacion ----------------------------------------------------------------

Resultado<ResultadoAplicacionVerificacion, ErrorPersistencia>
SqliteOperacionesSolicitudRepository::aplicarVerificacion(const AplicacionVerificacion& a)
{
    using R = Resultado<ResultadoAplicacionVerificacion, ErrorPersistencia>;
    constexpr QStringView kContexto = u"operaciones.aplicar_verificacion";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    const QVariant solicitud = sqlite::texto(a.solicitudId.texto());
    ResultadoAplicacionVerificacion resultado;

    // 1. Solicitud: estado SAT, agenda y reinicio de la racha de fallas.
    {
        QSqlQuery q(db);
        if (auto r = correr(
                q,
                QStringLiteral("UPDATE solicitud_masiva SET estado_solicitud_sat = :estado, "
                               "codigo_estado_solicitud = :codigo, mensaje_verificacion_sat = :mensaje, "
                               "numero_cfdi = :numero, ultima_verificacion_en = :verificada, "
                               "siguiente_verificacion_en = :siguiente, verificaciones_sin_cambio = :sin_cambio, "
                               "ultimo_error = NULL, ultima_clave_falla_verificacion = NULL, "
                               "fallas_verificacion_iguales = 0 "
                               "WHERE id = :id AND eliminado_en IS NULL AND estado_local = 'Enviada'"),
                {{":estado", sqlite::texto(claveEstable(a.estadoSolicitudSat))},
                 {":codigo", sqlite::textoOpcional(a.codigoEstadoSolicitud)},
                 {":mensaje", sqlite::textoOpcional(a.mensajeVerificacion)},
                 {":numero", a.numeroCfdi ? sqlite::entero(*a.numeroCfdi) : QVariant(QMetaType::fromType<qint64>())},
                 {":verificada", sqlite::instante(a.verificadaEn)},
                 {":siguiente", sqlite::instanteOpcional(a.siguienteVerificacionEn)},
                 {":sin_cambio", sqlite::entero(a.verificacionesSinCambio)},
                 {":id", solicitud}},
                kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        if (q.numRowsAffected() == 0) {
            return R::exito(std::move(resultado)); // eliminada o ya no Enviada: se descarta
        }
        resultado.aplicada = true;
    }

    // 2. Paquetes nuevos: solo pares (solicitud, IdPaquete) no registrados; un
    // paquete existente conserva su vencimiento_estimado_en (D10). No se usa
    // INSERT OR IGNORE porque tambien silenciaria violaciones de CHECK.
    for (const PaqueteNuevo& n : a.paquetesNuevos) {
        QSqlQuery q(db);
        if (auto r = correr(q,
                            QStringLiteral("INSERT INTO paquete_solicitud (id, solicitud_masiva_id, id_paquete_sat, "
                                           "estado_descarga, disponible_en, vencimiento_estimado_en) "
                                           "SELECT :id, :solicitud, :id_sat, 'Disponible', :disponible, :vencimiento "
                                           "WHERE NOT EXISTS (SELECT 1 FROM paquete_solicitud "
                                           "WHERE solicitud_masiva_id = :solicitud AND id_paquete_sat = :id_sat)"),
                            {{":id", sqlite::texto(n.id)},
                             {":solicitud", solicitud},
                             {":id_sat", sqlite::texto(n.idPaqueteSat)},
                             {":disponible", sqlite::instante(n.disponibleEn)},
                             {":vencimiento", sqlite::instante(n.vencimientoEstimadoEn)}},
                            kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        if (q.numRowsAffected() > 0) {
            resultado.paquetesInsertados.append(n.id);
        }
    }

    // 3. Solicitud Vencida en SAT: paquetes no Descargado/Vencido -> Vencido
    // (SAT, solicitud_expirada). Lectura y UPDATE con el mismo WHERE dentro de
    // la transaccion BEGIN IMMEDIATE (sin RETURNING para no exigir 3.35).
    if (a.vencerNoDescargados) {
        const QString kFiltro = QStringLiteral(
            "solicitud_masiva_id = :solicitud AND eliminado_en IS NULL "
            "AND estado_descarga NOT IN ('Descargado', 'Vencido')");
        {
            QSqlQuery q(db);
            q.setForwardOnly(true);
            if (auto r = correr(q,
                                QStringLiteral("SELECT id FROM paquete_solicitud WHERE ") + kFiltro
                                    + QStringLiteral(" ORDER BY id"),
                                {{":solicitud", solicitud}}, kContexto);
                !r) {
                return R::fallo(std::move(r).error());
            }
            while (q.next()) {
                resultado.paquetesVencidos.append(q.value(0).toString());
            }
        }
        QSqlQuery q(db);
        if (auto r = correr(q,
                            QStringLiteral("UPDATE paquete_solicitud SET estado_descarga = 'Vencido', "
                                           "vencido_en = :vencido, motivo_vencimiento = 'solicitud_expirada', "
                                           "origen_vencimiento = 'SAT' WHERE ")
                                + kFiltro,
                            {{":vencido", sqlite::instante(a.verificadaEn)}, {":solicitud", solicitud}}, kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
    }
    return R::exito(std::move(resultado));
}

Resultado<bool, ErrorPersistencia>
SqliteOperacionesSolicitudRepository::aplicarFallaVerificacion(const AplicacionFallaVerificacion& a)
{
    // No toca estado SAT, ultima_verificacion_en ni verificaciones_sin_cambio.
    return actualizar(m_proveedor,
                      QStringLiteral("UPDATE solicitud_masiva SET ultimo_error = :error, "
                                     "ultima_clave_falla_verificacion = :clave, "
                                     "fallas_verificacion_iguales = :fallas, siguiente_verificacion_en = :siguiente "
                                     "WHERE id = :id AND eliminado_en IS NULL AND estado_local = 'Enviada'"),
                      {{":error", sqlite::texto(a.ultimoError)},
                       {":clave", sqlite::texto(a.claveFalla)},
                       {":fallas", sqlite::entero(a.fallasIguales)},
                       {":siguiente", sqlite::instanteOpcional(a.siguienteVerificacionEn)},
                       {":id", sqlite::texto(a.solicitudId.texto())}},
                      u"operaciones.aplicar_falla_verificacion");
}

// --- Descarga --------------------------------------------------------------------

Resultado<bool, ErrorPersistencia> SqliteOperacionesSolicitudRepository::marcarDescargando(const QString& paqueteId,
                                                                                           const QDateTime& ahoraUtc,
                                                                                           bool permitirError)
{
    return actualizar(m_proveedor,
                      QStringLiteral("UPDATE paquete_solicitud SET estado_descarga = 'Descargando', "
                                     "descarga_iniciada_en = :ahora "
                                     "WHERE id = :id AND eliminado_en IS NULL "
                                     "AND (estado_descarga = 'Disponible' "
                                     "OR (:permitir_error = 1 AND estado_descarga = 'Error')) AND ")
                          + kSolicitudPadreVisible,
                      {{":ahora", sqlite::instante(ahoraUtc)},
                       {":id", sqlite::texto(paqueteId)},
                       {":permitir_error", sqlite::booleano(permitirError)}},
                      u"operaciones.marcar_descargando");
}

Resultado<bool, ErrorPersistencia> SqliteOperacionesSolicitudRepository::aplicarDescarga(const AplicacionDescarga& a)
{
    using R = Resultado<bool, ErrorPersistencia>;
    constexpr QStringView kContexto = u"operaciones.aplicar_descarga";
    QString especifico;
    switch (a.destino) {
    case EstadoDescarga::Descargado:
        if (!a.rutaFinal) {
            return R::fallo(argumentoInvalido(kContexto, "Descargado sin ruta"));
        }
        especifico = QStringLiteral(", ruta_local = :ruta, descargado_en = :en");
        break;
    case EstadoDescarga::Vencido:
        if (!a.motivoVencimiento || !a.origenVencimiento) {
            return R::fallo(argumentoInvalido(kContexto, "Vencido sin motivo u origen"));
        }
        especifico = QStringLiteral(", vencido_en = :en, motivo_vencimiento = :motivo, origen_vencimiento = :origen");
        break;
    case EstadoDescarga::Disponible:
        // Recuperacion sin archivo final: se libera el reclamo.
        especifico = QStringLiteral(", descarga_iniciada_en = NULL");
        break;
    case EstadoDescarga::Error:
        break;
    case EstadoDescarga::Descargando:
        return R::fallo(argumentoInvalido(kContexto, "destino"));
    }
    if (a.reconciliado) {
        especifico += QStringLiteral(", reconciliado_en = :en");
    }
    const QString sql = QStringLiteral("UPDATE paquete_solicitud SET estado_descarga = :destino, "
                                       "codigo_descarga_sat = :codigo, mensaje_descarga_sat = :mensaje, "
                                       "ultimo_error = :error")
                        + especifico
                        + QStringLiteral(" WHERE id = :id AND eliminado_en IS NULL "
                                         "AND estado_descarga = 'Descargando' AND ")
                        + kSolicitudPadreVisible;
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (auto r = sqlite::preparar(q, sql, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":destino"), sqlite::texto(claveEstable(a.destino)));
    q.bindValue(QStringLiteral(":codigo"), sqlite::textoOpcional(a.codigoDescargaSat));
    q.bindValue(QStringLiteral(":mensaje"), sqlite::textoOpcional(a.mensajeDescargaSat));
    q.bindValue(QStringLiteral(":error"), sqlite::textoOpcional(a.ultimoError));
    q.bindValue(QStringLiteral(":id"), sqlite::texto(a.paqueteId));
    if (especifico.contains(QStringLiteral(":en"))) {
        q.bindValue(QStringLiteral(":en"), sqlite::instante(a.aplicadaEn));
    }
    if (a.destino == EstadoDescarga::Descargado) {
        q.bindValue(QStringLiteral(":ruta"), sqlite::texto(*a.rutaFinal));
    }
    if (a.destino == EstadoDescarga::Vencido) {
        q.bindValue(QStringLiteral(":motivo"), sqlite::texto(claveEstable(*a.motivoVencimiento)));
        q.bindValue(QStringLiteral(":origen"), sqlite::texto(claveEstable(*a.origenVencimiento)));
    }
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    return R::exito(q.numRowsAffected() > 0);
}

Resultado<bool, ErrorPersistencia>
SqliteOperacionesSolicitudRepository::vencerPaqueteEstimado(const QString& paqueteId, const QDateTime& ahoraUtc)
{
    return actualizar(m_proveedor,
                      QStringLiteral("UPDATE paquete_solicitud SET estado_descarga = 'Vencido', vencido_en = :ahora, "
                                     "motivo_vencimiento = 'vencimiento_estimado', "
                                     "origen_vencimiento = 'estimacion_local' "
                                     "WHERE id = :id AND eliminado_en IS NULL "
                                     "AND estado_descarga IN ('Disponible', 'Descargando', 'Error') "
                                     "AND vencimiento_estimado_en IS NOT NULL AND vencimiento_estimado_en <= :ahora AND ")
                          + kSolicitudPadreVisible,
                      {{":ahora", sqlite::instante(ahoraUtc)}, {":id", sqlite::texto(paqueteId)}},
                      u"operaciones.vencer_estimado");
}

// --- Intenciones -----------------------------------------------------------------

Resultado<bool, ErrorPersistencia> SqliteOperacionesSolicitudRepository::registrarIntencion(
    const SolicitudId& solicitudId, TipoIntencion tipo, const QDateTime& ahoraUtc)
{
    const QString bandera = tipo == TipoIntencion::Verificacion ? QStringLiteral("verificacion_pendiente")
                                                                : QStringLiteral("descarga_pendiente");
    return actualizar(m_proveedor,
                      QStringLiteral("UPDATE solicitud_masiva SET ") + bandera
                          + QStringLiteral(" = 1, accion_pendiente_en = :ahora "
                                           "WHERE id = :id AND eliminado_en IS NULL"),
                      {{":ahora", sqlite::instante(ahoraUtc)}, {":id", sqlite::texto(solicitudId.texto())}},
                      u"operaciones.registrar_intencion");
}

Resultado<bool, ErrorPersistencia> SqliteOperacionesSolicitudRepository::consumirIntencion(
    const SolicitudId& solicitudId, TipoIntencion tipo, const QDateTime& capturadaEn)
{
    const bool verificacion = tipo == TipoIntencion::Verificacion;
    const QString bandera = verificacion ? QStringLiteral("verificacion_pendiente") : QStringLiteral("descarga_pendiente");
    const QString otra = verificacion ? QStringLiteral("descarga_pendiente") : QStringLiteral("verificacion_pendiente");
    // D13: solo si accion_pendiente_en conserva el valor capturado. Las
    // expresiones del SET leen los valores previos de la fila.
    return actualizar(m_proveedor,
                      QStringLiteral("UPDATE solicitud_masiva SET ") + bandera
                          + QStringLiteral(" = 0, accion_pendiente_en = CASE WHEN ") + otra
                          + QStringLiteral(" = 1 THEN accion_pendiente_en ELSE NULL END "
                                           "WHERE id = :id AND eliminado_en IS NULL "
                                           "AND accion_pendiente_en = :capturada"),
                      {{":id", sqlite::texto(solicitudId.texto())}, {":capturada", sqlite::instante(capturadaEn)}},
                      u"operaciones.consumir_intencion");
}

} // namespace satcfdi
