#include "domain/solicitudes/EstadoResumen.h"

namespace satcfdi {

namespace {

EstadoResumen desdeLocal(EstadoLocal estado)
{
    switch (estado) {
    case EstadoLocal::Creada: return EstadoResumen::Creada;
    case EstadoLocal::Enviando: return EstadoResumen::Enviando;
    case EstadoLocal::Enviada: return EstadoResumen::Enviada;
    case EstadoLocal::EnvioFallido: return EstadoResumen::EnvioFallido;
    case EstadoLocal::EnvioIncierto: return EstadoResumen::EnvioIncierto;
    }
    Q_UNREACHABLE_RETURN(EstadoResumen::Creada);
}

EstadoResumen desdeSat(EstadoSolicitudSat estado)
{
    switch (estado) {
    case EstadoSolicitudSat::Aceptada: return EstadoResumen::Aceptada;
    case EstadoSolicitudSat::EnProceso: return EstadoResumen::EnProceso;
    case EstadoSolicitudSat::Terminada: return EstadoResumen::Terminada;
    case EstadoSolicitudSat::Error: return EstadoResumen::ErrorSat;
    case EstadoSolicitudSat::Rechazada: return EstadoResumen::Rechazada;
    case EstadoSolicitudSat::Vencida: return EstadoResumen::Vencida;
    }
    Q_UNREACHABLE_RETURN(EstadoResumen::ErrorSat);
}

} // namespace

EstadoResumen derivarEstadoResumen(EstadoLocal estadoLocal,
                                   std::optional<EstadoSolicitudSat> estadoSat)
{
    return estadoSat ? desdeSat(*estadoSat) : desdeLocal(estadoLocal);
}

QString claveEstable(EstadoResumen estado)
{
    switch (estado) {
    case EstadoResumen::Creada: return QStringLiteral("Creada");
    case EstadoResumen::Enviando: return QStringLiteral("Enviando");
    case EstadoResumen::Enviada: return QStringLiteral("Enviada");
    case EstadoResumen::EnvioFallido: return QStringLiteral("EnvioFallido");
    case EstadoResumen::EnvioIncierto: return QStringLiteral("EnvioIncierto");
    case EstadoResumen::Aceptada: return QStringLiteral("Aceptada");
    case EstadoResumen::EnProceso: return QStringLiteral("EnProceso");
    case EstadoResumen::Terminada: return QStringLiteral("Terminada");
    case EstadoResumen::ErrorSat: return QStringLiteral("ErrorSat");
    case EstadoResumen::Rechazada: return QStringLiteral("Rechazada");
    case EstadoResumen::Vencida: return QStringLiteral("Vencida");
    }
    Q_UNREACHABLE_RETURN(QString());
}

} // namespace satcfdi
