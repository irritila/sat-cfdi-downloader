#pragma once

#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QString>

namespace satcfdi {

// Transiciones de solicitud que pueden generar notificacion local (T009 D1,
// D9). Nunca hay notificacion por paquete. Titulos segun la matriz T024.1
// D4 (ServicioNotificaciones::componer); el cuerpo lleva la direccion
// (emitidas/recibidas), el periodo y el RFC enmascarado del perfil.
enum class TipoTransicionNotificable {
    Terminada,        // estado SAT -> Terminada: con archivos no notifica; con 0, "Sin facturas en el periodo"
    DescargaCompleta, // no queda archivo por descargar: "Facturas descargadas"
    ErrorSat,         // estado SAT -> Error: "No se pudieron descargar las facturas"
    Rechazada,        // estado SAT -> Rechazada (salvo sin informacion): "El SAT no aceptó preparar las facturas"
    SinFacturas,      // T023.6: Rechazada con 5004 (periodo sin facturas): "Sin facturas en el periodo"
    Vencida,          // estado SAT -> Vencida: "Facturas no descargadas"
};

// Claves estables (tipo de NotificacionLocal): "terminada",
// "descarga_completa", "error_sat", "rechazada", "sin_facturas", "vencida".
inline QString claveEstable(TipoTransicionNotificable tipo)
{
    switch (tipo) {
    case TipoTransicionNotificable::Terminada: return QStringLiteral("terminada");
    case TipoTransicionNotificable::DescargaCompleta: return QStringLiteral("descarga_completa");
    case TipoTransicionNotificable::ErrorSat: return QStringLiteral("error_sat");
    case TipoTransicionNotificable::Rechazada: return QStringLiteral("rechazada");
    case TipoTransicionNotificable::SinFacturas: return QStringLiteral("sin_facturas");
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
    int paquetes = 0;         // Terminada: archivos del SAT (0 = sin facturas); DescargaCompleta: total visibles
    int descargados = 0;      // solo DescargaCompleta

    friend bool operator==(const TransicionNotificable&, const TransicionNotificable&) = default;
};

} // namespace satcfdi
