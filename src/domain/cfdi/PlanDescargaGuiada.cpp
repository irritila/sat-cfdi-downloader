#include "domain/cfdi/PlanDescargaGuiada.h"

namespace satcfdi {

namespace {

std::optional<CausaAtencion> inhibicion(const EvaluacionCoberturaMensual& e, TipoDescarga d)
{
    // `causas` ya esta ordenada por prioridad: 5002 antes que 5005.
    for (const CausaMensual& c : e.causas) {
        if (c.direccion == d && (c.causa == CausaAtencion::Codigo5002 || c.causa == CausaAtencion::Codigo5005)) {
            return c.causa;
        }
    }
    return std::nullopt;
}

PlanDireccion planDireccion(const EvaluacionCoberturaMensual& e, TipoDescarga d)
{
    PlanDireccion p;
    p.direccion = d;
    p.inhibidaPor = inhibicion(e, d);
    const CoberturaDireccion& c = e.cobertura(d);
    if (p.inhibidaPor || c.alcanzaFrontera || e.tipoMes == TipoMes::Futuro) {
        return p;
    }
    for (const HuecoCobertura& h : c.huecos) {
        for (const SolicitudId& id : h.reservadoPor) {
            if (!p.reutilizar.contains(id)) {
                p.reutilizar.append(id);
            }
        }
        if (!c.alcanzaFronteraConReservas) {
            p.crear.append(h.libres);
        }
    }
    return p;
}

PlanDescargaGuiada planSinValidar(const EvaluacionCoberturaMensual& e, AccionGuiada accion)
{
    PlanDescargaGuiada plan;
    plan.accion = accion;
    plan.emitidos = planDireccion(e, TipoDescarga::Emitidos);
    plan.recibidos = planDireccion(e, TipoDescarga::Recibidos);
    return plan;
}

bool reenviaCreadas(const EvaluacionCoberturaMensual& e, const PlanDescargaGuiada& plan)
{
    for (TipoDescarga d : {TipoDescarga::Emitidos, TipoDescarga::Recibidos}) {
        for (const SolicitudId& id : plan.direccion(d).reutilizar) {
            if (e.cobertura(d).sinEnviar.contains(id)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

QLatin1StringView claveEstable(AccionGuiada accion)
{
    switch (accion) {
    case AccionGuiada::DescargarFacturas:
        return QLatin1StringView("DescargarFacturas");
    case AccionGuiada::ActualizarMes:
        return QLatin1StringView("ActualizarMes");
    case AccionGuiada::CompletarMes:
        return QLatin1StringView("CompletarMes");
    }
    return QLatin1StringView("DescargarFacturas");
}

Resultado<PlanDescargaGuiada, ErrorPlanGuiado> planificarDescargaGuiada(const EvaluacionCoberturaMensual& evaluacion,
                                                                        AccionGuiada accion)
{
    using R = Resultado<PlanDescargaGuiada, ErrorPlanGuiado>;
    if (evaluacion.tipoMes == TipoMes::Futuro) {
        return R::fallo(ErrorPlanGuiado::MesFuturo);
    }
    if (ofertaDescargaGuiada(evaluacion).accion != accion) {
        return R::fallo(ErrorPlanGuiado::AccionNoDisponible);
    }
    return R::exito(planSinValidar(evaluacion, accion));
}

OfertaDescargaGuiada ofertaDescargaGuiada(const EvaluacionCoberturaMensual& evaluacion)
{
    OfertaDescargaGuiada o;
    for (TipoDescarga d : {TipoDescarga::Emitidos, TipoDescarga::Recibidos}) {
        if (inhibicion(evaluacion, d)) {
            o.direccionesInhibidas.append(d);
        }
    }
    for (const CausaMensual& c : evaluacion.causas) {
        if (c.causa == CausaAtencion::Codigo5005) {
            o.diagnostico5005.append(c);
        }
    }
    if (evaluacion.tipoMes == TipoMes::Futuro) {
        return o;
    }
    const PlanDescargaGuiada plan = planSinValidar(evaluacion, AccionGuiada::DescargarFacturas);
    if (!plan.creaAlgo() && !reenviaCreadas(evaluacion, plan)) {
        return o;
    }
    if (evaluacion.emitidos.tramos.isEmpty() && evaluacion.recibidos.tramos.isEmpty()) {
        o.accion = AccionGuiada::DescargarFacturas;
    } else if (evaluacion.tipoMes == TipoMes::EnCurso) {
        o.accion = AccionGuiada::ActualizarMes;
    } else {
        o.accion = AccionGuiada::CompletarMes;
    }
    return o;
}

} // namespace satcfdi
