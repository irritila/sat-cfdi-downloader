#pragma once

#include "domain/solicitudes/DedupKey.h"
#include "domain/solicitudes/OperacionSat.h"
#include "domain/solicitudes/SolicitudCanonica.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QDateTime>
#include <QString>
#include <QStringView>

#include <array>
#include <optional>

namespace satcfdi {

// Catalogo cerrado log_solicitud.tipo_evento (DC8 de T001). claveEstable()
// devuelve el literal snake_case del CHECK. Ampliarlo exige migracion.
enum class TipoEventoLog {
    SolicitudCreada,
    DuplicadoConfirmado,
    EnvioIniciado,
    SolicitudEnviada,
    EnvioFallido,
    EnvioIncierto,
    VerificacionRealizada,
    VerificacionFallida,
    PaquetesRegistrados,
    DescargaIniciada,
    PaqueteDescargado,
    DescargaFallida,
    DescargaInterrumpida,
    PaqueteReconciliado,
    PaqueteVencido,
    ArchivoHuerfano,
    AccionPendienteRegistrada,
    AccionPendienteDescartada,
};

inline constexpr std::array<TipoEventoLog, 18> kTiposEventoLog = {
    TipoEventoLog::SolicitudCreada,       TipoEventoLog::DuplicadoConfirmado,
    TipoEventoLog::EnvioIniciado,         TipoEventoLog::SolicitudEnviada,
    TipoEventoLog::EnvioFallido,          TipoEventoLog::EnvioIncierto,
    TipoEventoLog::VerificacionRealizada, TipoEventoLog::VerificacionFallida,
    TipoEventoLog::PaquetesRegistrados,   TipoEventoLog::DescargaIniciada,
    TipoEventoLog::PaqueteDescargado,     TipoEventoLog::DescargaFallida,
    TipoEventoLog::DescargaInterrumpida,  TipoEventoLog::PaqueteReconciliado,
    TipoEventoLog::PaqueteVencido,        TipoEventoLog::ArchivoHuerfano,
    TipoEventoLog::AccionPendienteRegistrada, TipoEventoLog::AccionPendienteDescartada,
};

// log_solicitud.origen: 'worker', 'usuario', 'recuperacion'.
enum class OrigenLog {
    Worker,
    Usuario,
    Recuperacion,
};

// log_solicitud.origen_codigo_sat: 'creacion', 'verificacion', 'descarga'.
enum class OrigenCodigoSat {
    Creacion,
    Verificacion,
    Descarga,
};

QString claveEstable(TipoEventoLog tipo);
QString claveEstable(OrigenLog origen);
QString claveEstable(OrigenCodigoSat origen);
std::optional<TipoEventoLog> tipoEventoLogDesdeClave(QStringView clave);
std::optional<OrigenLog> origenLogDesdeClave(QStringView clave);
std::optional<OrigenCodigoSat> origenCodigoSatDesdeClave(QStringView clave);

// Entrada de log SIN sanear. Nunca llega a un repositorio: LogSanitizer la
// convierte en LogEntradaSaneada. Separa metadata tipada de texto libre; no
// existen campos para tokens, contrasenas, llaves, certificados, firmas,
// XML/SOAP crudo, contenido Paquete, ZIP ni base64.
struct LogEntradaCruda {
    // Metadata (se copia tal cual).
    QString id;                                   // UUID canonico, lo asigna aplicacion
    SolicitudId solicitudId;
    TipoEventoLog tipoEvento = TipoEventoLog::SolicitudCreada;
    OrigenLog origen = OrigenLog::Usuario;
    QDateTime creadoEn;                           // UTC

    // Codigos SAT. Invariante del esquema (ck_log_codigo_sat): origen y
    // codigo van juntos y mensajeSat requiere codigo; el sanitizer descarta lo
    // incoherente.
    std::optional<OrigenCodigoSat> origenCodigoSat;
    std::optional<QString> codigoSat;
    std::optional<QString> mensajeSat;            // texto libre SAT: se sanea (max 500)

    // Identificadores SAT y operacion/filtros -> payload_resumen_json.
    std::optional<QString> idSolicitudSat;
    std::optional<QString> idPaqueteSat;
    std::optional<OperacionSat> operacionSat;
    std::optional<DedupKey> dedupKey;
    std::optional<SolicitudCanonica> filtros;     // filtros normalizados

    // Texto libre -> payload_resumen_json, saneado defensivamente.
    QString mensaje;                              // max 500 caracteres
    QString detalle;                              // max 8192 caracteres
};

// Entrada saneada: exactamente las columnas que se insertan en log_solicitud
// (sin eliminado_en, que nace NULL). Solo la produce un LogSanitizer;
// LogSolicitudRepository::agregar() solo acepta este tipo.
struct LogEntradaSaneada {
    QString id;                                   // id
    SolicitudId solicitudMasivaId;                // solicitud_masiva_id
    TipoEventoLog tipoEvento = TipoEventoLog::SolicitudCreada; // tipo_evento
    OrigenLog origen = OrigenLog::Usuario;        // origen
    std::optional<OrigenCodigoSat> origenCodigoSat; // origen_codigo_sat
    std::optional<QString> codigoSat;             // codigo_sat
    std::optional<QString> mensajeSat;            // mensaje_sat (saneado)
    std::optional<QString> payloadResumenJson;    // payload_resumen_json (objeto JSON compacto)
    QDateTime creadoEn;                           // creado_en
};

// Fila leida de log_solicitud.
struct LogPersistido : LogEntradaSaneada {
    std::optional<QDateTime> eliminadoEn;         // eliminado_en
};

} // namespace satcfdi
