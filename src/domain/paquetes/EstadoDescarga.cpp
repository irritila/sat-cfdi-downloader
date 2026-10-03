#include "domain/paquetes/EstadoDescarga.h"

namespace satcfdi {

QString claveEstable(EstadoDescarga estado)
{
    switch (estado) {
    case EstadoDescarga::Disponible: return QStringLiteral("Disponible");
    case EstadoDescarga::Descargando: return QStringLiteral("Descargando");
    case EstadoDescarga::Descargado: return QStringLiteral("Descargado");
    case EstadoDescarga::Error: return QStringLiteral("Error");
    case EstadoDescarga::Vencido: return QStringLiteral("Vencido");
    }
    Q_UNREACHABLE_RETURN(QString());
}

} // namespace satcfdi
