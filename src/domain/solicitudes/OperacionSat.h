#pragma once

#include "domain/solicitudes/EstadosSolicitud.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace satcfdi {

// Operacion SAT de creacion de solicitud (ADR 0013). Literal identico al CHECK
// de solicitud_masiva.operacion_sat y a la linea `operacion_sat=` de dedup_key.
enum class OperacionSat {
    SolicitaDescargaEmitidos,
    SolicitaDescargaRecibidos,
};

QString claveEstable(OperacionSat operacion);
std::optional<OperacionSat> operacionSatDesdeClave(QStringView clave);

// Mapeo 1:1 con tipo_cfdi (ck_solicitud_operacion).
OperacionSat operacionPara(TipoDescarga tipo);
TipoDescarga tipoDescargaDe(OperacionSat operacion);

} // namespace satcfdi
