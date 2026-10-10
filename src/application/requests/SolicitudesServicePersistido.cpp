#include "application/requests/SolicitudesServicePersistido.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "application/requests/PreparacionSolicitud.h"
#include "domain/common/UuidCanonico.h"
#include "domain/operaciones/PoliticasOperacion.h"
#include "ports/LogSanitizer.h"
#include "ports/repositories/LogSolicitudRepository.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QHash>

#include <utility>

namespace satcfdi {

// Todas las lambdas despachadas capturan SOLO copias (PuertosPersistencia,
// request, ids, timestamps); nunca `this`. Las continuaciones `then(this, ...)`
// corren en el hilo grafico, emiten senales y luego completan el future publico.

namespace {

using T = ErrorPersistencia::Tipo;

QDate diaDe(const QString& fechaSat)
{
    return QDate::fromString(fechaSat.left(10), u"yyyy-MM-dd");
}

SolicitudResumen aResumen(const SolicitudPersistida& s, int totalPaquetes)
{
    SolicitudResumen r;
    r.id = s.id;
    r.perfilRfc = s.rfcSolicitante;
    if (s.tipoCfdi == TipoDescarga::Emitidos) {
        if (!s.rfcReceptores.isEmpty()) {
            r.rfcContraparte = s.rfcReceptores.constFirst();
        }
    } else {
        r.rfcContraparte = s.rfcEmisor;
    }
    r.tipoDescarga = s.tipoCfdi;
    r.fechaInicial = diaDe(s.fechaInicialSat);
    r.fechaFinal = diaDe(s.fechaFinalSat);
    r.estadoLocal = s.estadoLocal;
    r.estadoSat = s.estadoSolicitudSat;
    r.creadaEn = s.creadaEn;
    r.totalPaquetes = totalPaquetes;
    return r;
}

PaqueteResumen aPaquete(const PaquetePersistido& p)
{
    PaqueteResumen r;
    r.idPaqueteSat = p.idPaqueteSat;
    r.estadoDescarga = p.estadoDescarga;
    r.disponibleEn = p.disponibleEn;
    r.descargadoEn = p.descargadoEn;
    r.vencidoEn = p.vencidoEn;
    r.codigoDescargaSat = p.codigoDescargaSat;
    r.puedeReintentar = politicas::puedeReintentarManual(p);
    r.reintentoPendiente = p.reintentoPendienteEn.has_value();
    return r;
}

LogResumen aLog(const LogPersistido& l)
{
    LogResumen r;
    r.tipoEvento = l.tipoEvento;
    r.origen = l.origen;
    r.origenCodigoSat = l.origenCodigoSat;
    r.codigoSat = l.codigoSat;
    r.mensajeSat = l.mensajeSat;
    r.creadoEn = l.creadoEn;
    return r;
}

SolicitudDetalle aDetalle(const SolicitudPersistida& s, const QList<PaquetePersistido>& paquetes,
                          const QList<LogPersistido>& logs)
{
    SolicitudDetalle d;
    d.resumen = aResumen(s, int(paquetes.size()));
    for (const PaquetePersistido& p : paquetes) {
        if (p.estadoDescarga == EstadoDescarga::Descargado) {
            ++d.resumen.paquetesDescargados;
        } else if (p.estadoDescarga == EstadoDescarga::Disponible || p.estadoDescarga == EstadoDescarga::Error) {
            ++d.resumen.paquetesPendientesDescarga;
        }
    }
    d.fechaInicialSat = s.fechaInicialSat;
    d.fechaFinalSat = s.fechaFinalSat;
    if (s.tipoCfdi == TipoDescarga::Emitidos) {
        d.rfcContrapartes = s.rfcReceptores;
    } else if (s.rfcEmisor) {
        d.rfcContrapartes = {*s.rfcEmisor};
    }
    d.tipoComprobante = s.tipoComprobante;
    d.complemento = s.complemento;
    d.idSolicitudSat = s.idSolicitudSat;
    d.codEstatusSolicitud = s.codEstatusSolicitud;
    d.mensajeSolicitudSat = s.mensajeSolicitudSat;
    d.codigoEstadoSolicitud = s.codigoEstadoSolicitud;
    d.mensajeVerificacionSat = s.mensajeVerificacionSat;
    d.numeroCfdi = s.numeroCfdi;
    d.enviadaEn = s.enviadaEn;
    d.ultimaVerificacionEn = s.ultimaVerificacionEn;
    d.ultimoError = s.ultimoError;
    if (s.ultimoError) {
        d.ultimoErrorDesglosado = desglosarUltimoError(*s.ultimoError);
    }
    for (const PaquetePersistido& p : paquetes) {
        d.paquetes.append(aPaquete(p));
    }
    for (const LogPersistido& l : logs) {
        d.logs.append(aLog(l));
    }
    return d;
}

// RFC del perfil si existe, es visible y esta activo.
Resultado<std::optional<QString>, ErrorPersistencia> rfcPerfilActivo(PerfilSatRepository& repo,
                                                                     const PerfilId& id)
{
    using R = Resultado<std::optional<QString>, ErrorPersistencia>;
    if (id.esNulo()) {
        return R::exito(std::nullopt);
    }
    auto perfil = repo.obtener(id);
    if (!perfil.esExito()) {
        return R::fallo(std::move(perfil).error());
    }
    const std::optional<PerfilSat>& p = perfil.valor();
    if (!p || !p->activo || p->eliminadoEn) {
        return R::exito(std::nullopt);
    }
    return R::exito(p->rfc);
}

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

SolicitudesServicePersistido::SolicitudesServicePersistido(PersistenceDispatcher& dispatcher,
                                                           PuertosPersistencia puertos,
                                                           RelojUtc reloj,
                                                           QObject* parent)
    : SolicitudesService(parent)
    , m_dispatcher(dispatcher)
    , m_puertos(puertos)
    , m_reloj(std::move(reloj))
{
}

QFuture<SolicitudesService::ResultadoLista> SolicitudesServicePersistido::listar()
{
    const PuertosPersistencia p = m_puertos;
    return m_dispatcher.despachar<ResultadoLista>([p]() {
        auto filas = p.solicitudes.listarVisibles();
        if (!filas.esExito()) {
            return ResultadoLista::fallo(std::move(filas).error());
        }
        auto conteos = p.paquetes.contarVisiblesPorSolicitudYEstado();
        if (!conteos.esExito()) {
            return ResultadoLista::fallo(std::move(conteos).error());
        }
        // T013 D8: nombre del perfil por id (perfiles visibles, activos o no),
        // en la misma tarea; el orden de la lista (creada_en DESC) no cambia.
        auto perfiles = p.perfiles.listarVisibles();
        if (!perfiles.esExito()) {
            return ResultadoLista::fallo(std::move(perfiles).error());
        }
        QHash<PerfilId, QString> nombres;
        for (const PerfilSat& perfil : perfiles.valor()) {
            nombres.insert(perfil.id, perfil.nombre.trimmed());
        }
        QList<SolicitudResumen> lista;
        lista.reserve(filas.valor().size());
        for (const SolicitudPersistida& s : filas.valor()) {
            const ConteoPaquetes conteo = conteos.valor().value(s.id);
            SolicitudResumen r = aResumen(s, conteo.total);
            r.paquetesDescargados = conteo.descargados;
            r.paquetesPendientesDescarga = conteo.pendientesDescarga;
            r.perfilNombre = nombres.value(s.perfilSatId);
            lista.append(std::move(r));
        }
        return ResultadoLista::exito(std::move(lista));
    });
}

QFuture<SolicitudesService::ResultadoDetalle>
SolicitudesServicePersistido::obtener(const SolicitudId& id)
{
    const ErrorObtener noEncontrada{ErrorObtener::Tipo::NoEncontrada,
                                    QStringLiteral("La solicitud no existe."), std::nullopt};
    if (id.esNulo()) {
        return QtFuture::makeReadyValueFuture(ResultadoDetalle::fallo(noEncontrada));
    }
    const PuertosPersistencia p = m_puertos;
    return m_dispatcher.despachar<ResultadoDetalle>([p, id, noEncontrada]() {
        auto persistencia = [](ErrorPersistencia e) {
            return ResultadoDetalle::fallo(
                ErrorObtener{ErrorObtener::Tipo::Persistencia, e.mensaje, e});
        };
        auto fila = p.solicitudes.obtenerVisible(id);
        if (!fila.esExito()) {
            return persistencia(std::move(fila).error());
        }
        if (!fila.valor()) {
            return ResultadoDetalle::fallo(noEncontrada);
        }
        auto paquetes = p.paquetes.listarVisiblesPorSolicitud(id);
        if (!paquetes.esExito()) {
            return persistencia(std::move(paquetes).error());
        }
        auto logs = p.logs.listarVisiblesPorSolicitud(id);
        if (!logs.esExito()) {
            return persistencia(std::move(logs).error());
        }
        return ResultadoDetalle::exito(aDetalle(*fila.valor(), paquetes.valor(), logs.valor()));
    });
}

QFuture<SolicitudesService::ResultadoEvaluarDuplicado>
SolicitudesServicePersistido::evaluarDuplicado(const NuevaSolicitudRequest& request)
{
    const PuertosPersistencia p = m_puertos;
    return m_dispatcher.despachar<ResultadoEvaluarDuplicado>([p, request]() {
        using R = ResultadoEvaluarDuplicado;
        auto rfc = rfcPerfilActivo(p.perfiles, request.perfilId);
        if (!rfc.esExito()) {
            return R::fallo(ErrorCrear::persistencia(std::move(rfc).error()));
        }
        auto canonica = prepararSolicitud(request, rfc.valor());
        if (!canonica.esExito()) {
            return R::fallo(std::move(canonica).error());
        }
        auto evaluacion = p.solicitudes.clasificarDuplicado(canonica.valor().dedupKey());
        if (!evaluacion.esExito()) {
            return R::fallo(ErrorCrear::persistencia(std::move(evaluacion).error()));
        }
        return R::exito(std::move(evaluacion).valor());
    });
}

QFuture<SolicitudesService::ResultadoCrear>
SolicitudesServicePersistido::crearLocal(const NuevaSolicitudRequest& request,
                                         ConfirmacionDuplicado confirmacion)
{
    const PuertosPersistencia p = m_puertos;
    const QDateTime ahora = m_reloj();
    const SolicitudId id = SolicitudId::generar();

    auto tarea = [p, request, confirmacion, ahora, id]() -> ResultadoCrear {
        using R = ResultadoCrear;

        // 1-2. Validacion de dominio y perfil activo, fuera de transaccion.
        auto rfc = rfcPerfilActivo(p.perfiles, request.perfilId);
        if (!rfc.esExito()) {
            return R::fallo(ErrorCrear::persistencia(std::move(rfc).error()));
        }
        auto preparada = prepararSolicitud(request, rfc.valor());
        if (!preparada.esExito()) {
            return R::fallo(std::move(preparada).error());
        }
        const SolicitudCanonica canonica = std::move(preparada).valor();

        // 3. BEGIN IMMEDIATE.
        auto begin = p.unidadDeTrabajo.begin();
        if (!begin.esExito()) {
            return R::fallo(ErrorCrear::persistencia(std::move(begin).error()));
        }
        auto abortar = [&p](ErrorCrear error) {
            (void)p.unidadDeTrabajo.rollback(); // best-effort; prevalece el error original
            return R::fallo(std::move(error));
        };

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
        auto insercion = p.solicitudes.insertarCreada(SolicitudNuevaPersistida{id, request.perfilId, canonica, ahora});
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

        // 9. COMMIT.
        auto commit = p.unidadDeTrabajo.commit();
        if (!commit.esExito()) {
            return abortar(errorDeEscritura(std::move(commit).error()));
        }
        return R::exito(id);
    };

    return m_dispatcher.despachar<ResultadoCrear>(std::move(tarea))
        .then(this, [this](ResultadoCrear r) {
            if (r.esExito()) {
                emit listaCambiada();
                emit solicitudActualizada(r.valor());
            }
            return r;
        });
}

QFuture<SolicitudesService::ResultadoEliminar>
SolicitudesServicePersistido::eliminar(const SolicitudId& id)
{
    if (id.esNulo()) {
        return QtFuture::makeReadyValueFuture(
            ResultadoEliminar::exito(ResultadoEliminacion{false, std::nullopt}));
    }
    const PuertosPersistencia p = m_puertos;
    const QDateTime ahora = m_reloj();

    auto tarea = [p, id, ahora]() -> ResultadoEliminar {
        using R = ResultadoEliminar;
        auto begin = p.unidadDeTrabajo.begin();
        if (!begin.esExito()) {
            return R::fallo(std::move(begin).error());
        }
        auto abortar = [&p](ErrorPersistencia e) {
            (void)p.unidadDeTrabajo.rollback();
            return R::fallo(std::move(e));
        };

        auto marcada = p.solicitudes.marcarEliminadaVisible(id, ahora);
        if (!marcada.esExito()) {
            return abortar(std::move(marcada).error());
        }
        if (!marcada.valor()) {
            // No existe o ya estaba eliminada: idempotente, sin cambios.
            (void)p.unidadDeTrabajo.rollback();
            return R::exito(ResultadoEliminacion{false, std::nullopt});
        }
        auto paquetes = p.paquetes.marcarEliminadosPorSolicitud(id, ahora);
        if (!paquetes.esExito()) {
            return abortar(std::move(paquetes).error());
        }
        auto logs = p.logs.marcarEliminadosPorSolicitud(id, ahora);
        if (!logs.esExito()) {
            return abortar(std::move(logs).error());
        }
        auto commit = p.unidadDeTrabajo.commit();
        if (!commit.esExito()) {
            return abortar(std::move(commit).error());
        }
        return R::exito(ResultadoEliminacion{true, ahora});
    };

    return m_dispatcher.despachar<ResultadoEliminar>(std::move(tarea))
        .then(this, [this, id](ResultadoEliminar r) {
            if (r.esExito() && r.valor().cambio) {
                emit listaCambiada();
                emit solicitudEliminada(id);
            }
            return r;
        });
}

} // namespace satcfdi
