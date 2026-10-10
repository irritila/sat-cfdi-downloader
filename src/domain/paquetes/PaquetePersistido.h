#pragma once

#include "domain/paquetes/EstadoDescarga.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QDateTime>
#include <QString>
#include <QStringView>

#include <optional>

namespace satcfdi {

// paquete_solicitud.motivo_vencimiento: 'solicitud_expirada',
// 'paquete_expirado', 'vencimiento_estimado'.
enum class MotivoVencimiento {
    SolicitudExpirada,
    PaqueteExpirado,
    VencimientoEstimado,
};

// paquete_solicitud.origen_vencimiento: 'SAT', 'estimacion_local'.
enum class OrigenVencimiento {
    Sat,
    EstimacionLocal,
};

// Valores literales de columna (iguales a los CHECK de T001).
QString claveEstable(MotivoVencimiento motivo);
QString claveEstable(OrigenVencimiento origen);
std::optional<MotivoVencimiento> motivoVencimientoDesdeClave(QStringView clave);
std::optional<OrigenVencimiento> origenVencimientoDesdeClave(QStringView clave);

// Fila de paquete_solicitud. Solo lectura en T003 (sin insercion productiva;
// las pruebas usan fixtures SQL). `id` es UUID canonico.
struct PaquetePersistido {
    QString id;                                        // id
    SolicitudId solicitudMasivaId;                     // solicitud_masiva_id
    QString idPaqueteSat;                              // id_paquete_sat
    EstadoDescarga estadoDescarga = EstadoDescarga::Disponible; // estado_descarga
    std::optional<QString> rutaLocal;                  // ruta_local
    QDateTime disponibleEn;                            // disponible_en
    std::optional<QDateTime> descargaIniciadaEn;       // descarga_iniciada_en
    std::optional<QDateTime> descargadoEn;             // descargado_en
    std::optional<QDateTime> vencimientoEstimadoEn;    // vencimiento_estimado_en
    std::optional<QDateTime> vencidoEn;                // vencido_en
    std::optional<MotivoVencimiento> motivoVencimiento; // motivo_vencimiento
    std::optional<OrigenVencimiento> origenVencimiento; // origen_vencimiento
    std::optional<QDateTime> reconciliadoEn;           // reconciliado_en
    std::optional<QString> codigoDescargaSat;          // codigo_descarga_sat
    std::optional<QString> mensajeDescargaSat;         // mensaje_descarga_sat
    std::optional<QString> ultimoError;                // ultimo_error
    std::optional<QDateTime> eliminadoEn;              // eliminado_en
    // T014.2 D2 (migracion 004): intencion manual de reintento de ESTE paquete
    // registrada con el monitoreo pausado; NULL sin intencion.
    std::optional<QDateTime> reintentoPendienteEn;     // reintento_pendiente_en
};

} // namespace satcfdi
