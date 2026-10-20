#include "application/requests/CreacionSolicitudLocal.h"

#include "domain/common/UuidCanonico.h"
#include "ports/LogSanitizer.h"
#include "ports/repositories/LogSolicitudRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"

#include <utility>

namespace satcfdi {

namespace {

using T = ErrorPersistencia::Tipo;

// Traduce un error de escritura de crearLocal (salvo DedupBloqueado).
ErrorCrear errorDeEscritura(ErrorPersistencia e)
{
    if (e.tipo == T::Unicidad || e.tipo == T::Integridad) {
        return ErrorCrear::integridad(std::move(e));
    }
    return ErrorCrear::persistencia(std::move(e));
}

LogEntradaCruda logBase(const SolicitudId& id, TipoEventoLog tipo, const QDateTime& en,
                        const SolicitudCanonica& canonica)
{
    LogEntradaCruda l;
    l.id = uuid::generarCanonico();
    l.solicitudId = id;
    l.tipoEvento = tipo;
    l.origen = OrigenLog::Usuario;
    l.creadoEn = en;
    l.operacionSat = canonica.operacionSat();
    l.dedupKey = canonica.dedupKey();
    l.filtros = canonica;
    return l;
}

} // namespace

Resultado<EvaluacionDuplicado, ErrorCrear> crearSolicitudLocalEnTransaccion(const PuertosPersistencia& p,
                                                                           const SolicitudId& id,
                                                                           const PerfilId& perfil,
                                                                           const SolicitudCanonica& canonica,
                                                                           ConfirmacionDuplicado confirmacion,
                                                                           const QDateTime& ahora)
{
    using R = Resultado<EvaluacionDuplicado, ErrorCrear>;
    auto abortar = [](ErrorCrear error) { return R::fallo(std::move(error)); };
    const SolicitudNuevaPersistida nueva{id, perfil, canonica, ahora};

    // 4-6. Reclasificacion autoritativa dentro de la transaccion.
    auto clasificada = p.solicitudes.clasificarDuplicado(canonica.dedupKey());
    if (!clasificada.esExito()) {
        return abortar(ErrorCrear::persistencia(std::move(clasificada).error()));
    }
    const EvaluacionDuplicado evaluacion = clasificada.valor();
    if (evaluacion.clasificacion == ClasificacionDuplicado::Bloqueado) {
        return abortar(ErrorCrear::dedupBloqueado(evaluacion));
    }
    const bool confirmable = evaluacion.clasificacion == ClasificacionDuplicado::RequiereConfirmacion;
    if (confirmable && confirmacion != ConfirmacionDuplicado::Confirmada) {
        return abortar(ErrorCrear::requiereConfirmacion(evaluacion));
    }

    // 7. INSERT Creada.
    auto insercion = p.solicitudes.insertarCreada(nueva);
    if (!insercion.esExito()) {
        ErrorPersistencia e = std::move(insercion).error();
        if (e.tipo == T::DedupBloqueado) {
            // Defensa DM6: el indice parcial detecto un bloqueante.
            EvaluacionDuplicado bloqueada;
            bloqueada.clasificacion = ClasificacionDuplicado::Bloqueado;
            bloqueada.dedupKey = canonica.dedupKey();
            bloqueada.motivo = MotivoDuplicado::SolicitudEnCurso;
            return abortar(ErrorCrear::dedupBloqueado(bloqueada));
        }
        return abortar(errorDeEscritura(std::move(e)));
    }

    // 8. Logs saneados.
    LogEntradaCruda creada = logBase(id, TipoEventoLog::SolicitudCreada, ahora, canonica);
    creada.mensaje = QStringLiteral("Solicitud local creada.");
    QList<LogEntradaCruda> logs = {creada};
    if (confirmable) {
        LogEntradaCruda dup = logBase(id, TipoEventoLog::DuplicadoConfirmado, ahora, canonica);
        dup.mensaje = QStringLiteral("Duplicado confirmado por el usuario (%1).")
                          .arg(claveEstable(evaluacion.motivo));
        if (evaluacion.solicitudReferencia) {
            dup.detalle = QStringLiteral("solicitud_referencia=%1")
                              .arg(evaluacion.solicitudReferencia->texto());
        }
        logs.append(dup);
    }
    for (const LogEntradaCruda& cruda : std::as_const(logs)) {
        auto agregado = p.logs.agregar(p.sanitizer.sanitizar(cruda));
        if (!agregado.esExito()) {
            return abortar(errorDeEscritura(std::move(agregado).error()));
        }
    }

    return R::exito(evaluacion);
}

} // namespace satcfdi
