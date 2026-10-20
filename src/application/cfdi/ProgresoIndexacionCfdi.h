#pragma once

#include "domain/solicitudes/SolicitudId.h"

#include <QFuture>

namespace satcfdi {

// Lo que el flujo mensual (T023.2) necesita del indexador de CFDI: saber si
// hay trabajo que alcanza a una solicitud y pedir la revalidacion de sus ZIP.
// La implementa IndexadorCfdi; las pruebas usan un fake.
// Hilo: el del objeto que la implementa (grafico para IndexadorCfdi).
class ProgresoIndexacionCfdi {
public:
    virtual ~ProgresoIndexacionCfdi() = default;

    // Hay trabajo de indexacion en curso o en cola que alcanza a la solicitud.
    virtual bool cubre(const SolicitudId& solicitud) const = 0;
    // Revalida las huellas de los paquetes de la solicitud y reindexa lo que
    // cambio; el future se completa al terminar (cancelado si se detuvo).
    virtual QFuture<void> revisarSolicitud(const SolicitudId& solicitud) = 0;
    // Version vigente del parser (indices de otra version son pendientes).
    virtual int versionParser() const = 0;
};

} // namespace satcfdi
