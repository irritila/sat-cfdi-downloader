#pragma once

#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QString>

namespace satcfdi {

// Transiciones de solicitud que generan notificacion local (T009 D1, D9).
// Nunca hay notificacion por paquete.
enum class TipoTransicionNotificable {
    Terminada,        // estado SAT -> Terminada ("Terminada (N paquetes)")
    DescargaCompleta, // no queda paquete por descargar ("Descarga completa: N de N")
    ErrorSat,         // estado SAT -> Error
    Rechazada,        // estado SAT -> Rechazada
    Vencida,          // estado SAT -> Vencida (los paquetes pueden ya no estar)
};

// Claves estables (tipo de NotificacionLocal): "terminada",
// "descarga_completa", "error_sat", "rechazada", "vencida".
inline QString claveEstable(TipoTransicionNotificable tipo)
{
    switch (tipo) {
    case TipoTransicionNotificable::Terminada: return QStringLiteral("terminada");
    case TipoTransicionNotificable::DescargaCompleta: return QStringLiteral("descarga_completa");
    case TipoTransicionNotificable::ErrorSat: return QStringLiteral("error_sat");
    case TipoTransicionNotificable::Rechazada: return QStringLiteral("rechazada");
    case TipoTransicionNotificable::Vencida: return QStringLiteral("vencida");
    }
    return {};
}

// Hecho ya CONFIRMADO (despues del commit) que publica OperacionExecutor.
// Lleva datos para componer el texto; quien lo muestre enmascara el RFC.
struct TransicionNotificable {
    SolicitudId solicitudId;
    TipoTransicionNotificable tipo = TipoTransicionNotificable::Terminada;
    TipoDescarga tipoDescarga = TipoDescarga::Emitidos;
    QString rfcSolicitante;   // completo: NO se muestra (ver ServicioNotificaciones)
    QString fechaInicialSat;  // "yyyy-MM-ddTHH:mm:ss"
    QString fechaFinalSat;
    int paquetes = 0;         // Terminada: ids de paquete; DescargaCompleta: total visibles
    int descargados = 0;      // solo DescargaCompleta

    friend bool operator==(const TransicionNotificable&, const TransicionNotificable&) = default;
};

} // namespace satcfdi
