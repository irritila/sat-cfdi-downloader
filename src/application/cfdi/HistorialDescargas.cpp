#include "application/cfdi/HistorialDescargas.h"

#include "domain/cfdi/PlanDescargaGuiada.h"

#include <QSet>

#include <algorithm>

namespace satcfdi {

QLatin1StringView claveEstable(ResultadoHistorial resultado)
{
    switch (resultado) {
    case ResultadoHistorial::Completa: return QLatin1StringView("Completa");
    case ResultadoHistorial::EnPreparacion: return QLatin1StringView("EnPreparacion");
    case ResultadoHistorial::RequiereAtencion: return QLatin1StringView("RequiereAtencion");
    case ResultadoHistorial::SinFacturas: return QLatin1StringView("SinFacturas");
    case ResultadoHistorial::NoCompletada: return QLatin1StringView("NoCompletada");
    case ResultadoHistorial::Eliminada: return QLatin1StringView("Eliminada");
    case ResultadoHistorial::Anterior: return QLatin1StringView("Anterior");
    }
    return QLatin1StringView("Completa");
}

namespace {

// Clase de una solicitud vigente del lote en la evaluacion del mes, en orden
// de precedencia (menor = gana).
enum class Clase { Atencion = 0, Preparacion = 1, Superada = 2, Acreditada = 3 };

bool contiene(const QList<CausaMensual>& causas, const SolicitudId& s)
{
    return std::any_of(causas.cbegin(), causas.cend(), [&](const CausaMensual& c) { return c.solicitud == s; });
}

Clase clasificar(const SolicitudId& s, const EvaluacionCoberturaMensual& e)
{
    if (contiene(e.causas, s)) {
        return Clase::Atencion;
    }
    for (const CoberturaDireccion* c : {&e.emitidos, &e.recibidos}) {
        if (c->solicitudesEnCurso.contains(s)) {
            return Clase::Preparacion;
        }
    }
    for (const CoberturaDireccion* c : {&e.emitidos, &e.recibidos}) {
        if (c->solicitudesAcreditadas.contains(s)) {
            return Clase::Acreditada;
        }
    }
    if (contiene(e.causasSuperadas, s)) {
        return Clase::Superada;
    }
    // Sin causa, trabajo ni acreditacion (p. ej. Terminada cuyos paquetes aun
    // no se registran): sigue preparandose.
    return Clase::Preparacion;
}

ResultadoHistorial desdeResultadoLote(ResultadoLote r)
{
    switch (r) {
    case ResultadoLote::ConAtencion: return ResultadoHistorial::RequiereAtencion;
    case ResultadoLote::EnCurso: return ResultadoHistorial::EnPreparacion;
    case ResultadoLote::Terminado: return ResultadoHistorial::Completa;
    case ResultadoLote::SinSolicitudesVigentes: return ResultadoHistorial::Eliminada;
    }
    return ResultadoHistorial::Completa;
}

// Solicitud de diagnostico de una direccion: la de la primera causa activa
// (ya ordenadas por prioridad), luego la de la primera superada, luego la
// vigente mas reciente (tramos ordenados por fecha inicial).
std::optional<SolicitudId> diagnostico(TipoDescarga direccion, const QList<SolicitudId>& vigentes,
                                       const EvaluacionCoberturaMensual* e)
{
    if (vigentes.isEmpty()) {
        return std::nullopt;
    }
    if (e) {
        for (const QList<CausaMensual>* lista : {&e->causas, &e->causasSuperadas}) {
            for (const CausaMensual& c : *lista) {
                if (c.direccion == direccion && vigentes.contains(c.solicitud)) {
                    return c.solicitud;
                }
            }
        }
    }
    return vigentes.constLast();
}

} // namespace

EntradaHistorial entradaDeLote(const LoteGuiado& lote, const EvaluacionCoberturaMensual* evaluacion,
                               const QHash<SolicitudId, int>& conteos)
{
    EntradaHistorial e;
    e.tipo = EntradaHistorial::Tipo::Guiada;
    e.loteId = lote.id;
    e.perfil = lote.perfil;
    e.mes = lote.mes;
    e.desde = lote.mes.primerDia();
    e.hasta = lote.mes.ultimoDia();
    e.creadoEn = lote.creadoEn;
    e.eliminacionParcial = lote.eliminacionParcial;

    QList<SolicitudId> emitidas;
    QList<SolicitudId> recibidas;
    for (const TramoLoteConsultado& t : lote.tramos) {
        if (t.eliminada) {
            continue;
        }
        const bool emitida = t.tramo.direccion == TipoDescarga::Emitidos;
        (emitida ? emitidas : recibidas).append(t.tramo.solicitud);
        (emitida ? e.emitidas : e.recibidas) += conteos.value(t.tramo.solicitud, 0);
    }
    e.tieneEmitidas = !emitidas.isEmpty();
    e.tieneRecibidas = !recibidas.isEmpty();

    if (!e.tieneEmitidas && !e.tieneRecibidas) {
        e.resultado = ResultadoHistorial::Eliminada;
        e.eliminacionParcial = true;
        return e;
    }

    if (evaluacion) {
        Clase peor = Clase::Acreditada;
        for (const QList<SolicitudId>* lista : {&emitidas, &recibidas}) {
            for (const SolicitudId& s : *lista) {
                peor = std::min(peor, clasificar(s, *evaluacion));
            }
        }
        switch (peor) {
        case Clase::Atencion: e.resultado = ResultadoHistorial::RequiereAtencion; break;
        case Clase::Preparacion: e.resultado = ResultadoHistorial::EnPreparacion; break;
        case Clase::Superada: e.resultado = ResultadoHistorial::NoCompletada; break;
        case Clase::Acreditada: e.resultado = ResultadoHistorial::Completa; break;
        }
        e.puedeReintentar = e.resultado == ResultadoHistorial::RequiereAtencion
                            && ofertaDescargaGuiada(*evaluacion).accion.has_value();
    } else {
        e.resultado = desdeResultadoLote(lote.resultado);
    }
    if (e.resultado == ResultadoHistorial::Completa && e.emitidas + e.recibidas == 0) {
        e.resultado = ResultadoHistorial::SinFacturas;
    }
    e.diagnosticoEmitidas = diagnostico(TipoDescarga::Emitidos, emitidas, evaluacion);
    e.diagnosticoRecibidas = diagnostico(TipoDescarga::Recibidos, recibidas, evaluacion);
    return e;
}

EntradaHistorial entradaAnterior(const PerfilId& perfil, const SolicitudSinLote& solicitud,
                                 const QHash<SolicitudId, int>& conteos)
{
    EntradaHistorial e;
    e.tipo = EntradaHistorial::Tipo::Anterior;
    e.perfil = perfil;
    e.creadoEn = solicitud.creadaEn;
    e.resultado = ResultadoHistorial::Anterior;
    e.conFiltros = solicitud.conFiltros;
    const auto inicio = centro::desdeFechaSat(solicitud.fechaInicialSat);
    const auto fin = centro::desdeFechaSat(solicitud.fechaFinalSat);
    if (inicio && fin) {
        e.desde = inicio->date();
        e.hasta = fin->date();
        const auto mesInicio = MesCalendario::de(e.desde);
        if (mesInicio && mesInicio == MesCalendario::de(e.hasta)) {
            e.mes = mesInicio;
        }
    }
    const int facturas = conteos.value(solicitud.id, 0);
    if (solicitud.direccion == TipoDescarga::Emitidos) {
        e.tieneEmitidas = true;
        e.emitidas = facturas;
        e.diagnosticoEmitidas = solicitud.id;
    } else {
        e.tieneRecibidas = true;
        e.recibidas = facturas;
        e.diagnosticoRecibidas = solicitud.id;
    }
    return e;
}

} // namespace satcfdi
