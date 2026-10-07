#include "application/paquetes/AccesoPaquetesService.h"

#include "application/operaciones/OperacionExecutor.h"

namespace satcfdi {

QFuture<ResolucionRevelable> AccesoPaquetesEjecutor::resolverArchivoPaquete(const QString& paqueteId)
{
    return m_ejecutor.resolverArchivoPaquete(paqueteId);
}

QFuture<ResolucionRevelable> AccesoPaquetesEjecutor::resolverCarpetaSolicitud(const SolicitudId& solicitudId)
{
    return m_ejecutor.resolverCarpetaSolicitud(solicitudId);
}

QFuture<ResolucionRevelable> AccesoPaquetesEjecutor::resolverRaizPaquetes()
{
    return m_ejecutor.resolverRaizPaquetes();
}

} // namespace satcfdi
