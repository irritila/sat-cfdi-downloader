#pragma once

#include "domain/paquetes/EstadoDescarga.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace satcfdi {

// Fila de la lista de solicitudes. estadoResumen no se guarda: se deriva con
// derivarEstadoResumen(estadoLocal, estadoSat) (domain/solicitudes/EstadoResumen.h).
struct SolicitudResumen {
    SolicitudId id;
    QString perfilRfc;
    std::optional<QString> rfcContraparte;
    TipoDescarga tipoDescarga = TipoDescarga::Emitidos;
    QDate fechaInicial;
    QDate fechaFinal;
    EstadoLocal estadoLocal = EstadoLocal::Creada;
    std::optional<EstadoSolicitudSat> estadoSat; // nulo sin respuesta SAT
    QDateTime creadaEn;                          // UTC
    int totalPaquetes = 0;
};

struct PaqueteResumen {
    QString idPaqueteSat;
    EstadoDescarga estadoDescarga = EstadoDescarga::Disponible;
    QDateTime disponibleEn;                // UTC
    std::optional<QDateTime> descargadoEn; // solo si Descargado
};

struct SolicitudDetalle {
    SolicitudResumen resumen;
    QList<PaqueteResumen> paquetes; // resumen.totalPaquetes == paquetes.size()
};

// Entrada del formulario de nueva solicitud. T003 ampliara los filtros.
struct NuevaSolicitudRequest {
    PerfilId perfilId;
    TipoDescarga tipoDescarga = TipoDescarga::Emitidos;
    QDate fechaInicial;
    QDate fechaFinal;
    std::optional<QString> rfcContraparte;
};

} // namespace satcfdi
