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

std::optional<EstadoDescarga> estadoDescargaDesdeClave(QStringView clave)
{
    using D = EstadoDescarga;
    for (D e : {D::Disponible, D::Descargando, D::Descargado, D::Error, D::Vencido}) {
        if (clave == claveEstable(e)) {
            return e;
        }
    }
    return std::nullopt;
}

} // namespace satcfdi
