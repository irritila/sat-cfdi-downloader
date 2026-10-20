#pragma once

#include "domain/cfdi/CoberturaMensual.h"
#include "domain/common/Resultado.h"

#include <QLatin1StringView>
#include <QList>

#include <optional>

namespace satcfdi {

// Planeacion pura de la descarga guiada (T023.3, ADR 0019 y 0020, T023 D7,
// D12, D17). Parte de una EvaluacionCoberturaMensual ya calculada con el
// instante capturado al iniciar la accion: no lee el reloj ni persistencia.

enum class AccionGuiada {
    DescargarFacturas, // ninguna direccion tiene tramos acreditados
    ActualizarMes,     // mes en curso con cobertura que no llega a hoy
    CompletarMes,      // mes cerrado incompleto
};

QLatin1StringView claveEstable(AccionGuiada accion);

// Plan de una direccion.
struct PlanDireccion {
    TipoDescarga direccion = TipoDescarga::Emitidos;
    // Causa activa 5002 o 5005 de la direccion: no se crea ni se recrea nada
    // para ella (ADR 0019).
    std::optional<CausaAtencion> inhibidaPor;
    // Solicitudes canonicas en curso que reservan un hueco: se reutilizan.
    QList<SolicitudId> reutilizar;
    // Segmentos libres de los huecos maximos (sin segundos acreditados ni
    // reservados por solicitudes en curso), en orden. Cada uno es una
    // solicitud.
    QList<TramoMensual> crear;

    friend bool operator==(const PlanDireccion&, const PlanDireccion&) = default;
};

struct PlanDescargaGuiada {
    AccionGuiada accion = AccionGuiada::DescargarFacturas;
    PlanDireccion emitidos;
    PlanDireccion recibidos;

    const PlanDireccion& direccion(TipoDescarga d) const { return d == TipoDescarga::Emitidos ? emitidos : recibidos; }
    bool creaAlgo() const { return !emitidos.crear.isEmpty() || !recibidos.crear.isEmpty(); }

    friend bool operator==(const PlanDescargaGuiada&, const PlanDescargaGuiada&) = default;
};

enum class ErrorPlanGuiado {
    MesFuturo,          // el mes aun no empieza en Centro
    AccionNoDisponible, // `accion` no es la que ofrece ofertaDescargaGuiada
};

// Por direccion: inhibida por 5002/5005 activo -> nada; cobertura que alcanza
// la frontera -> nada (D7: el mes en curso cubierto hoy no se actualiza el
// mismo dia); acreditado + reservado en curso que alcanza la frontera -> solo
// reutiliza (no se pide el tramo transcurrido desde la accion anterior); si
// no, por cada hueco reutiliza las solicitudes en curso que lo intersectan y
// pide sus segmentos libres. Repetir la accion no crea nada: las solicitudes
// creadas reservan sus segmentos.
//
// Rechaza `accion` distinta de ofertaDescargaGuiada(evaluacion).accion
// (AccionNoDisponible), incluida la ausencia de oferta.
Resultado<PlanDescargaGuiada, ErrorPlanGuiado> planificarDescargaGuiada(const EvaluacionCoberturaMensual& evaluacion,
                                                                        AccionGuiada accion);

// Lo que la UI debe ofrecer (DTO puro para T023.4).
struct OfertaDescargaGuiada {
    // nullopt: no se ofrece descarga (mes futuro, completo, o solo hay
    // trabajo en curso ya enviado que reutilizar). Tambien se ofrece si hay
    // solicitudes guiadas reutilizables que siguen en Creada (p. ej. cierre
    // entre el commit y el encolado): la accion del usuario las reenvia.
    std::optional<AccionGuiada> accion;
    // Direcciones con 5002 o 5005 activos: no se ofrece recrearlas.
    QList<TipoDescarga> direccionesInhibidas;
    // Causas 5005 activas: la accion de atencion lleva al diagnostico.
    QList<CausaMensual> diagnostico5005;
};

OfertaDescargaGuiada ofertaDescargaGuiada(const EvaluacionCoberturaMensual& evaluacion);

} // namespace satcfdi
