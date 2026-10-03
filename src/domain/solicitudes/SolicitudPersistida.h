#pragma once

#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/DedupKey.h"
#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/OperacionSat.h"
#include "domain/solicitudes/SolicitudCanonica.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

namespace satcfdi {

// Registros persistibles de solicitud_masiva (docs/design/sqlite-physical-model.md).
// Timestamps: QDateTime UTC; infraestructura los escribe/lee con
// domain/common/TimestampUtc.h. Columnas NULL <-> std::optional vacio.

// INSERT de una solicitud local nueva (T003): estado_local = 'Creada'.
// Columnas:
//   id                    <- id
//   perfil_sat_id         <- perfilSatId
//   tipo_cfdi .. complemento, dedup_key <- canonica (tipoCfdi(), operacionSat(),
//       rfcSolicitante(), rfcEmisor(), rfcReceptor(), rfcReceptoresJson(),
//       tipoSolicitudSat(), estadoComprobanteSat(), fechaInicialSat(),
//       fechaFinalSat(), tipoComprobante(), complemento(), dedupKey())
//   creada_en             <- creadaEn
//   estado_local          = 'Creada'
//   resto                 = NULL o DEFAULT del esquema (sin estado SAT,
//       id_solicitud_sat, codigos ni mensajes; contadores 0; banderas 0).
struct SolicitudNuevaPersistida {
    SolicitudId id;
    PerfilId perfilSatId;
    SolicitudCanonica canonica;
    QDateTime creadaEn;
};

// Fila completa de solicitud_masiva, campo a campo en el orden de la tabla.
struct SolicitudPersistida {
    SolicitudId id;                                    // id
    PerfilId perfilSatId;                              // perfil_sat_id
    std::optional<QString> idSolicitudSat;             // id_solicitud_sat
    TipoDescarga tipoCfdi = TipoDescarga::Emitidos;    // tipo_cfdi
    OperacionSat operacionSat = OperacionSat::SolicitaDescargaEmitidos; // operacion_sat
    QString rfcSolicitante;                            // rfc_solicitante
    std::optional<QString> rfcEmisor;                  // rfc_emisor
    std::optional<QString> rfcReceptor;                // rfc_receptor
    QStringList rfcReceptores;                         // rfc_receptores_json (vacia <-> NULL)
    QString tipoSolicitudSat = QStringLiteral("CFDI");          // tipo_solicitud_sat
    QString estadoComprobanteSat = QStringLiteral("Vigente");   // estado_comprobante_sat
    QString fechaInicialSat;                           // fecha_inicial_sat (Centro, sin offset)
    QString fechaFinalSat;                             // fecha_final_sat
    std::optional<QString> tipoComprobante;            // tipo_comprobante
    std::optional<QString> complemento;                // complemento
    DedupKey dedupKey;                                 // dedup_key
    EstadoLocal estadoLocal = EstadoLocal::Creada;     // estado_local
    std::optional<QString> codEstatusSolicitud;        // cod_estatus_solicitud
    std::optional<QString> mensajeSolicitudSat;        // mensaje_solicitud_sat
    std::optional<EstadoSolicitudSat> estadoSolicitudSat; // estado_solicitud_sat
    std::optional<QString> codigoEstadoSolicitud;      // codigo_estado_solicitud
    std::optional<QString> mensajeVerificacionSat;     // mensaje_verificacion_sat
    std::optional<qint64> numeroCfdi;                  // numero_cfdi
    QDateTime creadaEn;                                // creada_en
    std::optional<QDateTime> envioIniciadoEn;          // envio_iniciado_en
    std::optional<QDateTime> enviadaEn;                // enviada_en
    std::optional<QDateTime> ultimaVerificacionEn;     // ultima_verificacion_en
    std::optional<QDateTime> siguienteVerificacionEn;  // siguiente_verificacion_en
    int verificacionesSinCambio = 0;                   // verificaciones_sin_cambio
    std::optional<QString> ultimoError;                // ultimo_error
    bool verificacionPendiente = false;                // verificacion_pendiente
    bool descargaPendiente = false;                    // descarga_pendiente
    std::optional<QDateTime> accionPendienteEn;        // accion_pendiente_en
    std::optional<QDateTime> eliminadoEn;              // eliminado_en
};

} // namespace satcfdi
