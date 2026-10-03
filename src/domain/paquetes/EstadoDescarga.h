#pragma once

#include <QString>

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
QString claveEstable(EstadoDescarga estado);

} // namespace satcfdi
