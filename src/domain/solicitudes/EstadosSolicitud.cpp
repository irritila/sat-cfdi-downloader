#include "domain/solicitudes/EstadosSolicitud.h"

namespace satcfdi {

QString claveEstable(EstadoLocal estado)
{
    switch (estado) {
    case EstadoLocal::Creada: return QStringLiteral("Creada");
    case EstadoLocal::Enviando: return QStringLiteral("Enviando");
    case EstadoLocal::Enviada: return QStringLiteral("Enviada");
    case EstadoLocal::EnvioFallido: return QStringLiteral("EnvioFallido");
    case EstadoLocal::EnvioIncierto: return QStringLiteral("EnvioIncierto");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString claveEstable(EstadoSolicitudSat estado)
{
    switch (estado) {
    case EstadoSolicitudSat::Aceptada: return QStringLiteral("Aceptada");
    case EstadoSolicitudSat::EnProceso: return QStringLiteral("EnProceso");
    case EstadoSolicitudSat::Terminada: return QStringLiteral("Terminada");
    case EstadoSolicitudSat::Error: return QStringLiteral("Error");
    case EstadoSolicitudSat::Rechazada: return QStringLiteral("Rechazada");
    case EstadoSolicitudSat::Vencida: return QStringLiteral("Vencida");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString claveEstable(TipoDescarga tipo)
{
    switch (tipo) {
    case TipoDescarga::Emitidos: return QStringLiteral("Emitidos");
    case TipoDescarga::Recibidos: return QStringLiteral("Recibidos");
    }
    Q_UNREACHABLE_RETURN(QString());
}

std::optional<TipoDescarga> tipoDescargaDesdeClave(QStringView clave)
{
    if (clave == u"Emitidos") {
        return TipoDescarga::Emitidos;
    }
    if (clave == u"Recibidos") {
        return TipoDescarga::Recibidos;
    }
    return std::nullopt;
}

} // namespace satcfdi
