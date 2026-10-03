#pragma once

#include <QString>
#include <QStringView>

#include <optional>

namespace satcfdi {

// Estado de descarga de un PaqueteSolicitud (operational-rules). `Vencido` es
// vencimiento del paquete y no cambia por si solo el estado SAT de la solicitud.
enum class EstadoDescarga {
    Disponible,
    Descargando,
    Descargado,
    Error,
    Vencido,
};

// Clave estable identica al nombre del enumerador.
// Coincide con el CHECK de paquete_solicitud.estado_descarga (T001).
QString claveEstable(EstadoDescarga estado);
std::optional<EstadoDescarga> estadoDescargaDesdeClave(QStringView clave);

} // namespace satcfdi
