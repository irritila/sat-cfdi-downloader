#include "domain/cfdi/LoteDescargaGuiada.h"

namespace satcfdi {

QLatin1StringView claveEstable(ResultadoLote resultado)
{
    switch (resultado) {
    case ResultadoLote::SinSolicitudesVigentes:
        return QLatin1StringView("SinSolicitudesVigentes");
    case ResultadoLote::ConAtencion:
        return QLatin1StringView("ConAtencion");
    case ResultadoLote::EnCurso:
        return QLatin1StringView("EnCurso");
    case ResultadoLote::Terminado:
        return QLatin1StringView("Terminado");
    }
    return QLatin1StringView("SinSolicitudesVigentes");
}

ResultadoLote resultadoLote(const QList<EstadoResumen>& vigentes)
{
    if (vigentes.isEmpty()) {
        return ResultadoLote::SinSolicitudesVigentes;
    }
    bool enCurso = false;
    for (EstadoResumen e : vigentes) {
        switch (e) {
        case EstadoResumen::EnvioFallido:
        case EstadoResumen::EnvioIncierto:
        case EstadoResumen::ErrorSat:
        case EstadoResumen::Rechazada:
        case EstadoResumen::Vencida:
            return ResultadoLote::ConAtencion;
        case EstadoResumen::Creada:
        case EstadoResumen::Enviando:
        case EstadoResumen::Enviada:
        case EstadoResumen::Aceptada:
        case EstadoResumen::EnProceso:
            enCurso = true;
            break;
        case EstadoResumen::Terminada:
            break;
        }
    }
    return enCurso ? ResultadoLote::EnCurso : ResultadoLote::Terminado;
}

} // namespace satcfdi
