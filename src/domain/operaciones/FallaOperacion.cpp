#include "domain/operaciones/FallaOperacion.h"

namespace satcfdi {

QString claveEstable(FaseOperacion fase)
{
    switch (fase) {
    case FaseOperacion::Preparacion: return QStringLiteral("Preparacion");
    case FaseOperacion::Autenticacion: return QStringLiteral("Autenticacion");
    case FaseOperacion::AntesDeEnvio: return QStringLiteral("AntesDeEnvio");
    case FaseOperacion::DespuesDeEnvio: return QStringLiteral("DespuesDeEnvio");
    case FaseOperacion::RespuestaExplicita: return QStringLiteral("RespuestaExplicita");
    case FaseOperacion::Almacenamiento: return QStringLiteral("Almacenamiento");
    }
    return {};
}

QString claveEstable(CausaAlmacenamiento causa)
{
    switch (causa) {
    case CausaAlmacenamiento::ColisionDestino: return QStringLiteral("ColisionDestino");
    case CausaAlmacenamiento::EscrituraFallida: return QStringLiteral("EscrituraFallida");
    case CausaAlmacenamiento::EspacioInsuficiente: return QStringLiteral("EspacioInsuficiente");
    }
    return {};
}

std::optional<FaseOperacion> faseOperacionDesdeClave(QStringView clave)
{
    for (FaseOperacion f : kFasesOperacion) {
        if (claveEstable(f) == clave) {
            return f;
        }
    }
    return std::nullopt;
}

QString claveFalla(const FallaOperacion& falla)
{
    QString clave = claveEstable(falla.fase);
    if (falla.codigo && !falla.codigo->trimmed().isEmpty()) {
        clave += QLatin1Char(':') + falla.codigo->trimmed();
    }
    return clave;
}

} // namespace satcfdi
