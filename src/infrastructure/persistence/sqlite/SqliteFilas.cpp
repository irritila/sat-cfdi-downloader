#include "infrastructure/persistence/sqlite/SqliteFilas.h"

#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QSqlQuery>
#include <QStringList>

#include <utility>

namespace satcfdi::sqlite {

namespace {

constexpr QStringView kTablaSolicitud = u"solicitud_masiva";

const QString kColumnasSolicitudTexto = QStringLiteral(
    "id, perfil_sat_id, id_solicitud_sat, tipo_cfdi, operacion_sat, rfc_solicitante, "
    "rfc_emisor, rfc_receptor, rfc_receptores_json, tipo_solicitud_sat, "
    "estado_comprobante_sat, fecha_inicial_sat, fecha_final_sat, tipo_comprobante, "
    "complemento, dedup_key, estado_local, cod_estatus_solicitud, mensaje_solicitud_sat, "
    "estado_solicitud_sat, codigo_estado_solicitud, mensaje_verificacion_sat, numero_cfdi, "
    "creada_en, envio_iniciado_en, enviada_en, ultima_verificacion_en, "
    "siguiente_verificacion_en, verificaciones_sin_cambio, ultimo_error, "
    "verificacion_pendiente, descarga_pendiente, accion_pendiente_en, eliminado_en");

QVariant valor(const QSqlQuery& q, const char* columna)
{
    return q.value(QString::fromLatin1(columna));
}

} // namespace

Resultado<SolicitudPersistida, ErrorPersistencia> leerSolicitud(const QSqlQuery& q)
{
    using R = Resultado<SolicitudPersistida, ErrorPersistencia>;
    auto ilegible = [](const char* columna) {
        return R::fallo(sqlite::filaIlegible(kTablaSolicitud, QString::fromLatin1(columna)));
    };

    SolicitudPersistida s;
    const auto id = SolicitudId::desdeTexto(valor(q, "id").toString());
    if (!id) {
        return ilegible("id");
    }
    s.id = *id;
    const auto perfil = PerfilId::desdeTexto(valor(q, "perfil_sat_id").toString());
    if (!perfil) {
        return ilegible("perfil_sat_id");
    }
    s.perfilSatId = *perfil;
    s.idSolicitudSat = sqlite::leerTextoOpcional(valor(q, "id_solicitud_sat"));
    const auto tipo = tipoDescargaDesdeTipoCfdi(valor(q, "tipo_cfdi").toString());
    if (!tipo) {
        return ilegible("tipo_cfdi");
    }
    s.tipoCfdi = *tipo;
    const auto operacion = operacionSatDesdeClave(valor(q, "operacion_sat").toString());
    if (!operacion) {
        return ilegible("operacion_sat");
    }
    s.operacionSat = *operacion;
    s.rfcSolicitante = valor(q, "rfc_solicitante").toString();
    s.rfcEmisor = sqlite::leerTextoOpcional(valor(q, "rfc_emisor"));
    s.rfcReceptor = sqlite::leerTextoOpcional(valor(q, "rfc_receptor"));
    if (const auto json = sqlite::leerTextoOpcional(valor(q, "rfc_receptores_json"))) {
        const auto receptores = parsearRfcReceptoresJson(*json);
        if (!receptores) {
            return ilegible("rfc_receptores_json");
        }
        s.rfcReceptores = *receptores;
    }
    s.tipoSolicitudSat = valor(q, "tipo_solicitud_sat").toString();
    s.estadoComprobanteSat = valor(q, "estado_comprobante_sat").toString();
    s.fechaInicialSat = valor(q, "fecha_inicial_sat").toString();
    s.fechaFinalSat = valor(q, "fecha_final_sat").toString();
    s.tipoComprobante = sqlite::leerTextoOpcional(valor(q, "tipo_comprobante"));
    s.complemento = sqlite::leerTextoOpcional(valor(q, "complemento"));
    const auto clave = DedupKey::desdeTexto(valor(q, "dedup_key").toString());
    if (!clave) {
        return ilegible("dedup_key");
    }
    s.dedupKey = *clave;
    const auto estadoLocal = estadoLocalDesdeClave(valor(q, "estado_local").toString());
    if (!estadoLocal) {
        return ilegible("estado_local");
    }
    s.estadoLocal = *estadoLocal;
    s.codEstatusSolicitud = sqlite::leerTextoOpcional(valor(q, "cod_estatus_solicitud"));
    s.mensajeSolicitudSat = sqlite::leerTextoOpcional(valor(q, "mensaje_solicitud_sat"));
    if (const auto estadoSat = sqlite::leerTextoOpcional(valor(q, "estado_solicitud_sat"))) {
        const auto e = estadoSolicitudSatDesdeClave(*estadoSat);
        if (!e) {
            return ilegible("estado_solicitud_sat");
        }
        s.estadoSolicitudSat = *e;
    }
    s.codigoEstadoSolicitud = sqlite::leerTextoOpcional(valor(q, "codigo_estado_solicitud"));
    s.mensajeVerificacionSat = sqlite::leerTextoOpcional(valor(q, "mensaje_verificacion_sat"));
    if (const QVariant numero = valor(q, "numero_cfdi"); !numero.isNull()) {
        s.numeroCfdi = numero.toLongLong();
    }
    if (!sqlite::leerInstante(valor(q, "creada_en"), s.creadaEn)) {
        return ilegible("creada_en");
    }
    if (!sqlite::leerInstanteOpcional(valor(q, "envio_iniciado_en"), s.envioIniciadoEn)) {
        return ilegible("envio_iniciado_en");
    }
    if (!sqlite::leerInstanteOpcional(valor(q, "enviada_en"), s.enviadaEn)) {
        return ilegible("enviada_en");
    }
    if (!sqlite::leerInstanteOpcional(valor(q, "ultima_verificacion_en"), s.ultimaVerificacionEn)) {
        return ilegible("ultima_verificacion_en");
    }
    if (!sqlite::leerInstanteOpcional(valor(q, "siguiente_verificacion_en"),
                                      s.siguienteVerificacionEn)) {
        return ilegible("siguiente_verificacion_en");
    }
    s.verificacionesSinCambio = valor(q, "verificaciones_sin_cambio").toInt();
    s.ultimoError = sqlite::leerTextoOpcional(valor(q, "ultimo_error"));
    s.verificacionPendiente = valor(q, "verificacion_pendiente").toInt() != 0;
    s.descargaPendiente = valor(q, "descarga_pendiente").toInt() != 0;
    if (!sqlite::leerInstanteOpcional(valor(q, "accion_pendiente_en"), s.accionPendienteEn)) {
        return ilegible("accion_pendiente_en");
    }
    if (!sqlite::leerInstanteOpcional(valor(q, "eliminado_en"), s.eliminadoEn)) {
        return ilegible("eliminado_en");
    }
    return R::exito(std::move(s));
}


namespace {

constexpr QStringView kTablaPaquete = u"paquete_solicitud";

const QString kColumnasPaqueteTexto = QStringLiteral(
    "id, solicitud_masiva_id, id_paquete_sat, estado_descarga, ruta_local, disponible_en, "
    "descarga_iniciada_en, descargado_en, vencimiento_estimado_en, vencido_en, "
    "motivo_vencimiento, origen_vencimiento, reconciliado_en, codigo_descarga_sat, "
    "mensaje_descarga_sat, ultimo_error, eliminado_en");

} // namespace

Resultado<PaquetePersistido, ErrorPersistencia> leerPaquete(const QSqlQuery& q, int desde)
{
    using R = Resultado<PaquetePersistido, ErrorPersistencia>;
    auto ilegible = [](QStringView columna) { return R::fallo(sqlite::filaIlegible(kTablaPaquete, columna)); };

    PaquetePersistido p;
    p.id = q.value(desde).toString();
    const auto solicitud = SolicitudId::desdeTexto(q.value(desde + 1).toString());
    if (!solicitud) {
        return ilegible(u"solicitud_masiva_id");
    }
    p.solicitudMasivaId = *solicitud;
    p.idPaqueteSat = q.value(desde + 2).toString();
    const auto estado = estadoDescargaDesdeClave(q.value(desde + 3).toString());
    if (!estado) {
        return ilegible(u"estado_descarga");
    }
    p.estadoDescarga = *estado;
    p.rutaLocal = sqlite::leerTextoOpcional(q.value(desde + 4));
    if (!sqlite::leerInstante(q.value(desde + 5), p.disponibleEn)) {
        return ilegible(u"disponible_en");
    }
    if (!sqlite::leerInstanteOpcional(q.value(desde + 6), p.descargaIniciadaEn)) {
        return ilegible(u"descarga_iniciada_en");
    }
    if (!sqlite::leerInstanteOpcional(q.value(desde + 7), p.descargadoEn)) {
        return ilegible(u"descargado_en");
    }
    if (!sqlite::leerInstanteOpcional(q.value(desde + 8), p.vencimientoEstimadoEn)) {
        return ilegible(u"vencimiento_estimado_en");
    }
    if (!sqlite::leerInstanteOpcional(q.value(desde + 9), p.vencidoEn)) {
        return ilegible(u"vencido_en");
    }
    if (const auto motivo = sqlite::leerTextoOpcional(q.value(desde + 10))) {
        const auto m = motivoVencimientoDesdeClave(*motivo);
        if (!m) {
            return ilegible(u"motivo_vencimiento");
        }
        p.motivoVencimiento = *m;
    }
    if (const auto origen = sqlite::leerTextoOpcional(q.value(desde + 11))) {
        const auto o = origenVencimientoDesdeClave(*origen);
        if (!o) {
            return ilegible(u"origen_vencimiento");
        }
        p.origenVencimiento = *o;
    }
    if (!sqlite::leerInstanteOpcional(q.value(desde + 12), p.reconciliadoEn)) {
        return ilegible(u"reconciliado_en");
    }
    p.codigoDescargaSat = sqlite::leerTextoOpcional(q.value(desde + 13));
    p.mensajeDescargaSat = sqlite::leerTextoOpcional(q.value(desde + 14));
    p.ultimoError = sqlite::leerTextoOpcional(q.value(desde + 15));
    if (!sqlite::leerInstanteOpcional(q.value(desde + 16), p.eliminadoEn)) {
        return ilegible(u"eliminado_en");
    }
    return R::exito(std::move(p));
}


namespace {

QString prefijar(const QString& columnas, QStringView alias)
{
    if (alias.isEmpty()) {
        return columnas;
    }
    QStringList partes = columnas.split(QStringLiteral(", "));
    for (QString& parte : partes) {
        parte = alias.toString() + QLatin1Char('.') + parte;
    }
    return partes.join(QStringLiteral(", "));
}

} // namespace

QString columnasSolicitud()
{
    return kColumnasSolicitudTexto;
}

QString columnasPaquete(QStringView alias)
{
    return prefijar(kColumnasPaqueteTexto, alias);
}

} // namespace satcfdi::sqlite
