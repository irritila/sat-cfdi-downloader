#include "domain/solicitudes/Duplicados.h"

namespace satcfdi {

namespace {

using C = ClasificacionDuplicado;
using M = MotivoDuplicado;

struct Clasificada {
    C clasificacion;
    M motivo;
};

int severidad(C c)
{
    switch (c) {
    case C::Libre: return 0;
    case C::RequiereConfirmacion: return 1;
    case C::Bloqueado: return 2;
    }
    Q_UNREACHABLE_RETURN(0);
}

Clasificada clasificarTerminada(const QList<EstadoDescarga>& paquetes)
{
    if (paquetes.isEmpty()) {
        return {C::RequiereConfirmacion, M::TerminadaSinPaquetes};
    }
    bool hayVencido = false;
    for (EstadoDescarga e : paquetes) {
        switch (e) {
        case EstadoDescarga::Disponible:
        case EstadoDescarga::Descargando:
        case EstadoDescarga::Error:
            return {C::Bloqueado, M::TerminadaConPaquetesPendientes};
        case EstadoDescarga::Vencido:
            hayVencido = true;
            break;
        case EstadoDescarga::Descargado:
            break;
        }
    }
    return hayVencido ? Clasificada{C::RequiereConfirmacion, M::TerminadaConPaquetesVencidos}
                      : Clasificada{C::Bloqueado, M::TerminadaDescargada};
}

Clasificada clasificar(const CoincidenciaDuplicado& c)
{
    if (c.eliminada) {
        return {C::RequiereConfirmacion, M::SolicitudEliminada};
    }
    if (c.estadoSat) {
        switch (*c.estadoSat) {
        case EstadoSolicitudSat::Aceptada:
        case EstadoSolicitudSat::EnProceso:
            return {C::Bloqueado, M::SolicitudEnCurso};
        case EstadoSolicitudSat::Terminada:
            return clasificarTerminada(c.paquetesNoEliminados);
        case EstadoSolicitudSat::Error:
        case EstadoSolicitudSat::Rechazada:
        case EstadoSolicitudSat::Vencida:
            return {C::RequiereConfirmacion, M::SolicitudSinExito};
        }
    }
    switch (c.estadoLocal) {
    case EstadoLocal::Creada:
    case EstadoLocal::Enviando:
    case EstadoLocal::Enviada:
        return {C::Bloqueado, M::SolicitudEnCurso};
    case EstadoLocal::EnvioIncierto:
        return {C::RequiereConfirmacion, M::EnvioIncierto};
    case EstadoLocal::EnvioFallido:
        return {C::RequiereConfirmacion, M::SolicitudSinExito};
    }
    Q_UNREACHABLE_RETURN((Clasificada{C::Bloqueado, M::SolicitudEnCurso}));
}

} // namespace

QString claveEstable(ClasificacionDuplicado clasificacion)
{
    switch (clasificacion) {
    case C::Libre: return QStringLiteral("Libre");
    case C::Bloqueado: return QStringLiteral("Bloqueado");
    case C::RequiereConfirmacion: return QStringLiteral("RequiereConfirmacion");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString claveEstable(MotivoDuplicado motivo)
{
    switch (motivo) {
    case M::SinCoincidencias: return QStringLiteral("SinCoincidencias");
    case M::SolicitudEnCurso: return QStringLiteral("SolicitudEnCurso");
    case M::TerminadaConPaquetesPendientes: return QStringLiteral("TerminadaConPaquetesPendientes");
    case M::TerminadaDescargada: return QStringLiteral("TerminadaDescargada");
    case M::TerminadaConPaquetesVencidos: return QStringLiteral("TerminadaConPaquetesVencidos");
    case M::TerminadaSinPaquetes: return QStringLiteral("TerminadaSinPaquetes");
    case M::EnvioIncierto: return QStringLiteral("EnvioIncierto");
    case M::SolicitudSinExito: return QStringLiteral("SolicitudSinExito");
    case M::SolicitudEliminada: return QStringLiteral("SolicitudEliminada");
    }
    Q_UNREACHABLE_RETURN(QString());
}

EvaluacionDuplicado clasificarCoincidencias(const DedupKey& clave,
                                            const QList<CoincidenciaDuplicado>& coincidencias)
{
    EvaluacionDuplicado resultado;
    resultado.dedupKey = clave;
    for (const CoincidenciaDuplicado& c : coincidencias) {
        const Clasificada actual = clasificar(c);
        if (severidad(actual.clasificacion) > severidad(resultado.clasificacion)) {
            resultado.clasificacion = actual.clasificacion;
            resultado.motivo = actual.motivo;
            resultado.solicitudReferencia = c.id;
        }
    }
    return resultado;
}

} // namespace satcfdi
