#pragma once

#include "domain/common/Resultado.h"
#include "domain/solicitudes/DedupKey.h"
#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/OperacionSat.h"

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace satcfdi {

// Entrada (sin normalizar) para construir una SolicitudCanonica. La arma
// aplicacion a partir de NuevaSolicitudRequest y del PerfilSat activo.
struct EntradaSolicitudCanonica {
    TipoDescarga tipoDescarga = TipoDescarga::Emitidos;
    QString rfcPerfil;          // PerfilSat.rfc; sera rfc_solicitante
    QDate fechaInicial;         // dia civil Centro de Mexico
    QDate fechaFinal;
    // Contrapartes. Emitidos: 0..n receptores (rfc_receptores_json).
    // Recibidos: 0..1 emisor (rfc_emisor). Vacios/espacios se ignoran.
    QStringList rfcContrapartes;
    std::optional<QString> tipoComprobante; // I, E, T, N, P (trim + mayusculas)
    std::optional<QString> complemento;     // texto libre (trim)
};

// Error de normalizacion. `codigo` es estable; `mensaje` es descriptivo, sin
// secretos. Se devuelven todos los errores detectados, en el orden de campos.
struct ErrorSolicitudCanonica {
    enum class Codigo {
        RfcSolicitanteInvalido,
        RfcContraparteInvalido,
        ContraparteMultipleNoPermitida, // recibidos con mas de un emisor
        FechaInicialRequerida,
        FechaFinalRequerida,
        RangoFechasInvalido,            // fechaFinal < fechaInicial
        TipoComprobanteInvalido,
        CaracterReservado,              // LF, CR, '=' o ',' (dedup_key v1)
    };

    Codigo codigo = Codigo::RfcSolicitanteInvalido;
    QString campo;   // nombre de columna/linea afectada, p. ej. "complemento"
    QString mensaje;
};

// Solicitud de descarga normalizada (DA2): fuente unica de las columnas de
// filtros de solicitud_masiva y de dedup_key v1. Inmutable; solo se obtiene
// con normalizar().
//
// Reglas:
// - RFC: trim, mayusculas, sin espacios internos y validado (domain/common/Rfc.h).
// - Mapeo DC2 de T001:
//     Emitidos:  rfc_emisor = rfc_solicitante, rfc_receptor = NULL,
//                contrapartes -> rfc_receptores_json (deduplicadas, ordenadas
//                por bytes UTF-8; NULL si no hay).
//     Recibidos: rfc_receptor = rfc_solicitante, rfc_receptores_json = NULL,
//                contraparte -> rfc_emisor (NULL si no hay).
// - Fechas: `YYYY-MM-DDT00:00:00` / `YYYY-MM-DDT23:59:59`, hora Centro de
//   Mexico SIN offset. No se convierte a UTC.
// - tipo_solicitud_sat = "CFDI", estado_comprobante_sat = "Vigente" (fijos).
// - Opcionales vacios tras trim se tratan como ausentes.
class SolicitudCanonica {
public:
    static Resultado<SolicitudCanonica, QList<ErrorSolicitudCanonica>>
    normalizar(const EntradaSolicitudCanonica& entrada);

    TipoDescarga tipoDescarga() const noexcept { return m_tipo; }
    QString tipoCfdi() const { return valorTipoCfdi(m_tipo); }         // columna tipo_cfdi
    OperacionSat operacionSat() const noexcept { return operacionPara(m_tipo); }
    const QString& rfcSolicitante() const noexcept { return m_rfcSolicitante; }
    const std::optional<QString>& rfcEmisor() const noexcept { return m_rfcEmisor; }
    const std::optional<QString>& rfcReceptor() const noexcept { return m_rfcReceptor; }
    // Emitidos: receptores normalizados/ordenados (puede ser vacia).
    const QStringList& rfcReceptores() const noexcept { return m_rfcReceptores; }
    // Columna rfc_receptores_json: arreglo JSON compacto o nullopt si vacia.
    std::optional<QString> rfcReceptoresJson() const;
    // Contraparte unica para UI (emitidos: primer receptor; recibidos: emisor).
    std::optional<QString> rfcContraparte() const;
    QString tipoSolicitudSat() const { return QStringLiteral("CFDI"); }
    QString estadoComprobanteSat() const { return QStringLiteral("Vigente"); }
    const QString& fechaInicialSat() const noexcept { return m_fechaInicialSat; }
    const QString& fechaFinalSat() const noexcept { return m_fechaFinalSat; }
    const std::optional<QString>& tipoComprobante() const noexcept { return m_tipoComprobante; }
    const std::optional<QString>& complemento() const noexcept { return m_complemento; }

    // Serializacion canonica v1 (UTF-8 sin BOM, `clave=valor` separados por LF,
    // sin LF final, orden fijo de operational-rules). Expuesta para vectores
    // golden.
    QByteArray serializacionCanonicaV1() const;

    // `v1:<sha256-hex>` de serializacionCanonicaV1().
    const DedupKey& dedupKey() const noexcept { return m_dedupKey; }

private:
    SolicitudCanonica() = default;

    TipoDescarga m_tipo = TipoDescarga::Emitidos;
    QString m_rfcSolicitante;
    std::optional<QString> m_rfcEmisor;
    std::optional<QString> m_rfcReceptor;
    QStringList m_rfcReceptores;
    QString m_fechaInicialSat;
    QString m_fechaFinalSat;
    std::optional<QString> m_tipoComprobante;
    std::optional<QString> m_complemento;
    DedupKey m_dedupKey;
};

// Utilidades de la columna rfc_receptores_json para infraestructura.
// serializar: arreglo JSON compacto (`["A","B"]`), nullopt si `rfcs` vacia.
// parsear: nullopt si no es un arreglo JSON de strings.
std::optional<QString> serializarRfcReceptoresJson(const QStringList& rfcs);
std::optional<QStringList> parsearRfcReceptoresJson(QStringView json);

} // namespace satcfdi
