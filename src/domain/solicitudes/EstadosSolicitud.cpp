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

std::optional<EstadoLocal> estadoLocalDesdeClave(QStringView clave)
{
    for (EstadoLocal e : {EstadoLocal::Creada, EstadoLocal::Enviando, EstadoLocal::Enviada,
                          EstadoLocal::EnvioFallido, EstadoLocal::EnvioIncierto}) {
        if (clave == claveEstable(e)) {
            return e;
        }
    }
    return std::nullopt;
}

std::optional<EstadoSolicitudSat> estadoSolicitudSatDesdeClave(QStringView clave)
{
    using S = EstadoSolicitudSat;
    for (S e : {S::Aceptada, S::EnProceso, S::Terminada, S::Error, S::Rechazada, S::Vencida}) {
        if (clave == claveEstable(e)) {
            return e;
        }
    }
    return std::nullopt;
}

QString valorTipoCfdi(TipoDescarga tipo)
{
    switch (tipo) {
    case TipoDescarga::Emitidos: return QStringLiteral("emitidos");
    case TipoDescarga::Recibidos: return QStringLiteral("recibidos");
    }
    Q_UNREACHABLE_RETURN(QString());
}

std::optional<TipoDescarga> tipoDescargaDesdeTipoCfdi(QStringView valor)
{
    if (valor == u"emitidos") {
        return TipoDescarga::Emitidos;
    }
    if (valor == u"recibidos") {
        return TipoDescarga::Recibidos;
    }
    return std::nullopt;
}

} // namespace satcfdi
