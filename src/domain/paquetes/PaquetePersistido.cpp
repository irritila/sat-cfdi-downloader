#include "domain/paquetes/PaquetePersistido.h"

namespace satcfdi {

QString claveEstable(MotivoVencimiento motivo)
{
    switch (motivo) {
    case MotivoVencimiento::SolicitudExpirada: return QStringLiteral("solicitud_expirada");
    case MotivoVencimiento::PaqueteExpirado: return QStringLiteral("paquete_expirado");
    case MotivoVencimiento::VencimientoEstimado: return QStringLiteral("vencimiento_estimado");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString claveEstable(OrigenVencimiento origen)
{
    switch (origen) {
    case OrigenVencimiento::Sat: return QStringLiteral("SAT");
    case OrigenVencimiento::EstimacionLocal: return QStringLiteral("estimacion_local");
    }
    Q_UNREACHABLE_RETURN(QString());
}

std::optional<MotivoVencimiento> motivoVencimientoDesdeClave(QStringView clave)
{
    using M = MotivoVencimiento;
    for (M m : {M::SolicitudExpirada, M::PaqueteExpirado, M::VencimientoEstimado}) {
        if (clave == claveEstable(m)) {
            return m;
        }
    }
    return std::nullopt;
}

std::optional<OrigenVencimiento> origenVencimientoDesdeClave(QStringView clave)
{
    for (OrigenVencimiento o : {OrigenVencimiento::Sat, OrigenVencimiento::EstimacionLocal}) {
        if (clave == claveEstable(o)) {
            return o;
        }
    }
    return std::nullopt;
}

} // namespace satcfdi
