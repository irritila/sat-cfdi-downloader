#include "application/operaciones/OperacionExecutor.h"

#include "application/operaciones/WorkerLocal.h"
#include "domain/common/UuidCanonico.h"
#include "domain/operaciones/PoliticasOperacion.h"
#include "ports/LogSanitizer.h"
#include "ports/PackageStorage.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"
#include "ports/repositories/LogSolicitudRepository.h"
#include "ports/repositories/OperacionesSolicitudRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QHash>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPromise>
#include <QSet>
#include <QThread>

#include <array>
#include <deque>
#include <exception>
#include <utility>

Q_LOGGING_CATEGORY(lcRecuperacionArchivos, "satcfdi.recuperacion.archivos")

namespace satcfdi {

namespace {

using R = ResultadoOperacion;
using Desenlace = ResultadoOperacion::Desenlace;
using RB = Resultado<bool, ErrorPersistencia>;

PrioridadOperacion prioridadDe(OrigenLog origen)
{
    switch (origen) {
    case OrigenLog::Recuperacion: return PrioridadOperacion::Recuperacion;
    case OrigenLog::Usuario: return PrioridadOperacion::Manual;
    case OrigenLog::Worker: return PrioridadOperacion::Automatica;
    }
    return PrioridadOperacion::Automatica;
}

R resultado(TipoOperacion tipo, OrigenLog origen, Desenlace d, std::optional<SolicitudId> s = std::nullopt,
            std::optional<QString> paquete = std::nullopt, std::optional<FallaOperacion> falla = std::nullopt)
{
    R r;
    r.tipo = tipo;
    r.origen = origen;
    r.desenlace = d;
    r.solicitudId = std::move(s);
    r.paqueteId = std::move(paquete);
    r.falla = std::move(falla);
    return r;
}

QString diagnostico(const FallaOperacion& f)
{
    QString d = claveFalla(f);
    if (f.causaAlmacenamiento) {
        d += QLatin1Char('/') + claveEstable(*f.causaAlmacenamiento);
    }
    if (f.cancelada) {
        d += QStringLiteral(" (cancelada)");
    }
    if (!f.diagnosticoSanitizado.isEmpty()) {
        d += QStringLiteral(": ") + f.diagnosticoSanitizado;
    }
    return d;
}

bool verificable(const SolicitudPersistida& s)
{
    if (s.estadoLocal != EstadoLocal::Enviada || !s.idSolicitudSat) {
        return false;
    }
    return !s.estadoSolicitudSat
           || (*s.estadoSolicitudSat != EstadoSolicitudSat::Error && *s.estadoSolicitudSat != EstadoSolicitudSat::Rechazada
               && *s.estadoSolicitudSat != EstadoSolicitudSat::Vencida);
}

} // namespace

struct Trabajo {
    PrioridadOperacion prioridad = PrioridadOperacion::Automatica;
    std::optional<TipoOperacion> tipo;
    std::function<void(const SenalCancelacion&)> ejecutar;
    std::function<void()> rechazar;
};

struct OperacionExecutor::Impl {
    OperacionExecutor* q;
    PuertosEjecutor p;
    RelojUtc reloj;
    Programador& programador;

    QThread hilo;
    QObject* contexto = nullptr;

    QMutex mutex;
    std::array<std::deque<Trabajo>, 3> colas;
    bool aceptando = true;
    bool activo = false;
    bool terminando = false;
    bool finalizado = false;
    SenalCancelacion cancelacionActual;

    // Hilo grafico.
    std::optional<Programador::Id> plazoId;
    std::shared_ptr<QPromise<void>> promesaDetener;
    QFuture<void> futuroDetener;

    // Hilo del ejecutor.
    QHash<PerfilId, EstadoCredencial> credencialesObservadas;

    Impl(OperacionExecutor* q_, PuertosEjecutor puertos, RelojUtc r, Programador& prog)
        : q(q_), p(std::move(puertos)), reloj(std::move(r)), programador(prog)
    {
    }

    // --- Cola -------------------------------------------------------------

    template <typename T>
    QFuture<T> encolar(PrioridadOperacion prioridad, std::optional<TipoOperacion> tipo,
                       std::function<T(const SenalCancelacion&)> cuerpo, std::function<T()> rechazo)
    {
        auto promesa = std::make_shared<QPromise<T>>();
        QFuture<T> futuro = promesa->future();
        Trabajo t;
        t.prioridad = prioridad;
        t.tipo = tipo;
        t.ejecutar = [promesa, cuerpo = std::move(cuerpo)](const SenalCancelacion& c) {
            promesa->start();
            try {
                if constexpr (std::is_void_v<T>) {
                    cuerpo(c);
                } else {
                    promesa->addResult(cuerpo(c));
                }
            } catch (...) {
                promesa->setException(std::current_exception());
            }
            promesa->finish();
        };
        t.rechazar = [promesa, rechazo = std::move(rechazo)]() {
            promesa->start();
            if constexpr (!std::is_void_v<T>) {
                if (rechazo) {
                    promesa->addResult(rechazo());
                }
            }
            promesa->finish();
        };
        bool aceptado = false;
        {
            QMutexLocker l(&mutex);
            if (aceptando) {
                colas.at(static_cast<std::size_t>(prioridad)).push_back(t);
                aceptado = true;
            }
        }
        if (!aceptado) {
            t.rechazar();
            return futuro;
        }
        QMetaObject::invokeMethod(contexto, [this]() { procesar(); }, Qt::QueuedConnection);
        return futuro;
    }

    QFuture<R> encolarOperacion(TipoOperacion tipo, OrigenLog origen, std::optional<SolicitudId> s,
                                std::optional<QString> paquete, std::function<R(const SenalCancelacion&)> cuerpo)
    {
        auto conPublicacion = [this, cuerpo = std::move(cuerpo)](const SenalCancelacion& c) {
            R r = cuerpo(c);
            emit q->operacionTerminada(r);
            return r;
        };
        return encolar<R>(prioridadDe(origen), tipo, std::move(conPublicacion),
                          [tipo, origen, s, paquete]() { return resultado(tipo, origen, Desenlace::Rechazada, s, paquete); });
    }

    // Hilo del ejecutor: una invocacion por trabajo encolado; toma el de mayor
    // prioridad disponible.
    void procesar()
    {
        Trabajo t;
        SenalCancelacion cancelacion;
        {
            QMutexLocker l(&mutex);
            auto it = std::find_if(colas.begin(), colas.end(), [](const auto& c) { return !c.empty(); });
            if (it == colas.end()) {
                return;
            }
            t = std::move(it->front());
            it->pop_front();
            activo = true;
            cancelacionActual = SenalCancelacion();
            cancelacion = cancelacionActual;
        }
        if (t.tipo) {
            emit q->operacionIniciada(*t.tipo);
        }
        t.ejecutar(cancelacion);
        bool terminar = false;
        bool vacia = false;
        {
            QMutexLocker l(&mutex);
            activo = false;
            terminar = terminando;
            vacia = std::all_of(colas.cbegin(), colas.cend(), [](const auto& c) { return c.empty(); });
        }
        if (vacia) {
            emit q->inactivo();
        }
        if (terminar) {
            finalizarEnHilo();
        }
    }

    void finalizarEnHilo()
    {
        {
            QMutexLocker l(&mutex);
            if (finalizado) {
                return;
            }
            finalizado = true;
        }
        if (p.cerrarConexion) {
            p.cerrarConexion();
        }
        hilo.quit();
    }

    // --- Persistencia -------------------------------------------------------

    // begin; cuerpo; commit si devolvio true; rollback si false o error.
    template <typename F>
    RB enTransaccion(F cuerpo)
    {
        auto b = p.unidadDeTrabajo.begin();
        if (!b) {
            return RB::fallo(std::move(b).error());
        }
        RB r = cuerpo();
        if (!r || !r.valor()) {
            (void)p.unidadDeTrabajo.rollback();
            return r;
        }
        auto c = p.unidadDeTrabajo.commit();
        if (!c) {
            (void)p.unidadDeTrabajo.rollback();
            return RB::fallo(std::move(c).error());
        }
        return r;
    }

    struct DatosLog {
        TipoEventoLog tipo;
        OrigenLog origen;
        std::optional<OrigenCodigoSat> origenCodigo;
        std::optional<QString> codigo;
        std::optional<QString> mensajeSat;
        QString mensaje;
        QString detalle;
        std::optional<QString> idPaqueteSat;
    };

    std::optional<ErrorPersistencia> log(const SolicitudId& s, DatosLog d)
    {
        LogEntradaCruda e;
        e.id = uuid::generarCanonico();
        e.solicitudId = s;
        e.tipoEvento = d.tipo;
        e.origen = d.origen;
        e.creadoEn = reloj();
        if (d.codigo) {
            e.origenCodigoSat = d.origenCodigo;
            e.codigoSat = d.codigo;
            e.mensajeSat = d.mensajeSat;
        }
        e.idPaqueteSat = d.idPaqueteSat;
        e.mensaje = d.mensaje;
        e.detalle = d.detalle;
        auto r = p.logs.agregar(p.sanitizer.sanitizar(e));
        if (!r) {
            return std::move(r).error();
        }
        return std::nullopt;
    }

    // Consume (o descarta con log) una intencion en la transaccion actual.
    std::optional<ErrorPersistencia> consumir(const SolicitudId& s, TipoIntencion tipo, const QDateTime& capturada,
                                              bool descartarConLog)
    {
        auto c = p.operaciones.consumirIntencion(s, tipo, capturada);
        if (!c) {
            return std::move(c).error();
        }
        if (c.valor() && descartarConLog) {
            return log(s, {TipoEventoLog::AccionPendienteDescartada, OrigenLog::Usuario, std::nullopt, std::nullopt,
                           std::nullopt,
                           tipo == TipoIntencion::Verificacion ? QStringLiteral("Verificacion pendiente ya no aplica")
                                                               : QStringLiteral("Descarga pendiente ya no aplica"),
                           {}, std::nullopt});
        }
        return std::nullopt;
    }

    R errorLocal(TipoOperacion tipo, OrigenLog origen, std::optional<SolicitudId> s, std::optional<QString> paquete,
                 std::optional<FallaOperacion> falla = std::nullopt)
    {
        return resultado(tipo, origen, Desenlace::ErrorLocal, std::move(s), std::move(paquete), std::move(falla));
    }

    // --- Envio (D4, D7, ADR 0017) -------------------------------------------

    R ejecutarEnvio(const SolicitudId& id, const SenalCancelacion& cancelacion)
    {
        constexpr auto tipo = TipoOperacion::Envio;
        const OrigenLog origen = OrigenLog::Usuario;
        // 1. Marcar Enviando.
        std::optional<SolicitudPersistida> solicitud;
        RB marcado = enTransaccion([&]() -> RB {
            auto leida = p.solicitudes.obtenerVisible(id);
            if (!leida) {
                return RB::fallo(std::move(leida).error());
            }
            if (!leida.valor()) {
                return RB::exito(false);
            }
            auto m = p.operaciones.marcarEnviando(id, reloj());
            if (!m || !m.valor()) {
                return m;
            }
            solicitud = std::move(leida).valor();
            solicitud->estadoLocal = EstadoLocal::Enviando;
            if (auto e = log(id, {TipoEventoLog::EnvioIniciado, origen, std::nullopt, std::nullopt, std::nullopt,
                                  QStringLiteral("Envio iniciado"), {}, std::nullopt})) {
                return RB::fallo(*e);
            }
            return RB::exito(true);
        });
        if (!marcado) {
            return errorLocal(tipo, origen, id, std::nullopt);
        }
        if (!marcado.valor()) {
            return resultado(tipo, origen, Desenlace::Descartada, id);
        }
        // 2. Puerto, sin transaccion.
        ContextoEnvio ctx{*solicitud, cancelacion};
        auto r = p.sat.enviar(ctx);
        // 3. Aplicar.
        return aplicarEnvio(id, origen, r.esExito() ? std::optional(r.valor()) : std::nullopt,
                            r.esExito() ? std::nullopt : std::optional(r.error()));
    }

    R aplicarEnvio(const SolicitudId& id, OrigenLog origen, std::optional<ResultadoEnvio> exito,
                   std::optional<FallaOperacion> falla)
    {
        constexpr auto tipo = TipoOperacion::Envio;
        const QDateTime ahora = reloj();
        AplicacionEnvio a;
        a.solicitudId = id;
        DatosLog d{TipoEventoLog::EnvioIncierto, origen, OrigenCodigoSat::Creacion, std::nullopt, std::nullopt, {}, {}, std::nullopt};
        if (exito) {
            a.destino = politicas::destinoEnvio(*exito);
            a.codEstatus = exito->codEstatus;
            a.mensaje = exito->mensaje;
            d.codigo = exito->codEstatus;
            d.mensajeSat = exito->mensaje;
            if (a.destino == EstadoLocal::Enviada) {
                a.idSolicitudSat = exito->idSolicitudSat;
                a.enviadaEn = ahora;
                a.siguienteVerificacionEn =
                    ahora.addSecs(std::chrono::duration_cast<std::chrono::seconds>(politicas::kIntervaloCorto).count());
            } else {
                a.ultimoError = QStringLiteral("CodEstatus %1").arg(exito->codEstatus);
            }
        } else {
            a.destino = politicas::destinoEnvio(*falla);
            a.ultimoError = diagnostico(*falla);
            d.detalle = *a.ultimoError;
            if (falla->codigo && falla->fase == FaseOperacion::RespuestaExplicita) {
                d.codigo = falla->codigo;
            }
        }
        switch (a.destino) {
        case EstadoLocal::Enviada:
            d.tipo = TipoEventoLog::SolicitudEnviada;
            d.mensaje = QStringLiteral("Solicitud enviada");
            break;
        case EstadoLocal::Creada:
            d.tipo = TipoEventoLog::EnvioNoIniciado;
            d.mensaje = QStringLiteral("El envio no se inicio; la solicitud regresa a Creada");
            break;
        case EstadoLocal::EnvioFallido:
            d.tipo = TipoEventoLog::EnvioFallido;
            d.mensaje = QStringLiteral("SAT rechazo la solicitud");
            break;
        default:
            d.tipo = TipoEventoLog::EnvioIncierto;
            d.mensaje = QStringLiteral("Resultado del envio incierto");
            break;
        }
        RB aplicado = enTransaccion([&]() -> RB {
            auto ap = p.operaciones.aplicarEnvio(a);
            if (!ap || !ap.valor()) {
                return ap;
            }
            if (auto e = log(id, d)) {
                return RB::fallo(*e);
            }
            return RB::exito(true);
        });
        if (!aplicado) {
            return errorLocal(tipo, origen, id, std::nullopt, falla);
        }
        if (!aplicado.valor()) {
            return resultado(tipo, origen, Desenlace::Descartada, id, std::nullopt, falla);
        }
        emit q->solicitudActualizada(id);
        return resultado(tipo, origen, Desenlace::Aplicada, id, std::nullopt, falla);
    }

    // --- Verificacion (D8, D10, D13, ADR 0015) ---------------------------------

    R ejecutarVerificacion(const SolicitudId& id, OrigenLog origen, std::optional<QDateTime> intencion,
                           const SenalCancelacion& cancelacion)
    {
        constexpr auto tipo = TipoOperacion::Verificacion;
        auto leida = p.solicitudes.obtenerVisible(id);
        if (!leida) {
            return errorLocal(tipo, origen, id, std::nullopt);
        }
        if (!leida.valor()) {
            return resultado(tipo, origen, Desenlace::Descartada, id);
        }
        const SolicitudPersistida anterior = *leida.valor();
        if (!verificable(anterior)) {
            if (intencion) {
                RB r = enTransaccion([&]() -> RB {
                    if (auto e = consumir(id, TipoIntencion::Verificacion, *intencion, true)) {
                        return RB::fallo(*e);
                    }
                    return RB::exito(true);
                });
                if (r && r.valor()) {
                    emit q->solicitudActualizada(id);
                }
            }
            return resultado(tipo, origen, Desenlace::Descartada, id);
        }
        ContextoVerificacion ctx{id, anterior.perfilSatId, anterior.rfcSolicitante, *anterior.idSolicitudSat, cancelacion};
        auto r = p.sat.verificar(ctx);
        const QDateTime ahora = reloj();
        std::optional<FallaOperacion> falla = r.esExito() ? std::nullopt : std::optional(r.error());

        RB aplicado = enTransaccion([&]() -> RB {
            auto racha = p.operaciones.leerRachaVerificacion(id);
            if (!racha) {
                return RB::fallo(std::move(racha).error());
            }
            if (!racha.valor()) {
                return RB::exito(false); // eliminada
            }
            if (r.esExito()) {
                auto ok = aplicarExitoVerificacion(id, origen, anterior, *racha.valor(), r.valor(), ahora);
                if (!ok || !ok.valor()) {
                    return ok;
                }
            } else {
                auto ok = aplicarFallaVerificacion(id, origen, *racha.valor(), *falla, ahora);
                if (!ok || !ok.valor()) {
                    return ok;
                }
            }
            if (intencion) {
                if (auto e = consumir(id, TipoIntencion::Verificacion, *intencion, false)) {
                    return RB::fallo(*e);
                }
            }
            return RB::exito(true);
        });
        if (!aplicado) {
            return errorLocal(tipo, origen, id, std::nullopt, falla);
        }
        if (!aplicado.valor()) {
            return resultado(tipo, origen, Desenlace::Descartada, id, std::nullopt, falla);
        }
        emit q->solicitudActualizada(id);
        return resultado(tipo, origen, Desenlace::Aplicada, id, std::nullopt, falla);
    }

    // Exito con false si la solicitud ya no esta Enviada (resultado descartado).
    RB aplicarExitoVerificacion(const SolicitudId& id, OrigenLog origen,
                                                              const SolicitudPersistida& anterior,
                                                              const RachaVerificacion& racha,
                                                              const ResultadoVerificacion& v, const QDateTime& ahora)
    {
        // Sin cambio en los campos escalares; el conjunto de ids se evalua con
        // los paquetes efectivamente insertados (los ya registrados se omiten).
        const bool escalaresIguales = anterior.estadoSolicitudSat == std::optional(v.estadoSolicitudSat)
                                      && anterior.codigoEstadoSolicitud == v.codigoEstadoSolicitud
                                      && anterior.numeroCfdi == v.numeroCfdi;
        AplicacionVerificacion a;
        a.solicitudId = id;
        a.verificadaEn = ahora;
        a.estadoSolicitudSat = v.estadoSolicitudSat;
        a.codigoEstadoSolicitud = v.codigoEstadoSolicitud;
        a.mensajeVerificacion = v.mensaje;
        a.numeroCfdi = v.numeroCfdi;
        a.vencerNoDescargados = v.estadoSolicitudSat == EstadoSolicitudSat::Vencida;
        QSet<QString> vistos;
        for (const QString& idPaquete : v.idsPaquetes) {
            const QString limpio = idPaquete.trimmed();
            if (!limpio.isEmpty() && !vistos.contains(limpio)) {
                vistos.insert(limpio);
                a.paquetesNuevos.append(
                    PaqueteNuevo{uuid::generarCanonico(), limpio, ahora, politicas::vencimientoEstimado(ahora)});
            }
        }
        auto agenda = politicas::agendaTrasExito(racha.verificacionesSinCambio, !escalaresIguales, v.estadoSolicitudSat, ahora);
        a.siguienteVerificacionEn = agenda.siguienteVerificacionEn;
        a.verificacionesSinCambio = agenda.verificacionesSinCambio;
        auto res = p.operaciones.aplicarVerificacion(a);
        if (!res) {
            return RB::fallo(std::move(res).error());
        }
        ResultadoAplicacionVerificacion aplicada = std::move(res).valor();
        if (!aplicada.aplicada) {
            return RB::exito(false);
        }
        bool huboCambio = !escalaresIguales;
        if (aplicada.aplicada && escalaresIguales && !aplicada.paquetesInsertados.isEmpty()) {
            // Cambio el conjunto de ids: la agenda vuelve a 0 / 10 min (D8).
            huboCambio = true;
            agenda = politicas::agendaTrasExito(racha.verificacionesSinCambio, true, v.estadoSolicitudSat, ahora);
            a.siguienteVerificacionEn = agenda.siguienteVerificacionEn;
            a.verificacionesSinCambio = agenda.verificacionesSinCambio;
            a.paquetesNuevos.clear();
            a.vencerNoDescargados = false;
            auto otra = p.operaciones.aplicarVerificacion(a);
            if (!otra) {
                return RB::fallo(std::move(otra).error());
            }
        }
        // Log de verificacion solo si hubo cambio o la pidio el usuario (las
        // repeticiones sin cambio del worker solo actualizan la agenda).
        if (huboCambio || origen == OrigenLog::Usuario) {
            if (auto e = log(id, {TipoEventoLog::VerificacionRealizada, origen, OrigenCodigoSat::Verificacion, v.codEstatus,
                                  v.mensaje,
                                  QStringLiteral("Estado SAT %1").arg(claveEstable(v.estadoSolicitudSat)),
                                  v.codigoEstadoSolicitud ? QStringLiteral("CodigoEstadoSolicitud %1").arg(*v.codigoEstadoSolicitud)
                                                          : QString(),
                                  std::nullopt})) {
                return RB::fallo(*e);
            }
        }
        if (!aplicada.paquetesInsertados.isEmpty()) {
            if (auto e = log(id, {TipoEventoLog::PaquetesRegistrados, origen, std::nullopt, std::nullopt, std::nullopt,
                                  QStringLiteral("%1 paquete(s) registrados").arg(aplicada.paquetesInsertados.size()),
                                  {}, std::nullopt})) {
                return RB::fallo(*e);
            }
        }
        if (!aplicada.paquetesVencidos.isEmpty()) {
            if (auto e = log(id, {TipoEventoLog::PaqueteVencido, origen, std::nullopt, std::nullopt, std::nullopt,
                                  QStringLiteral("%1 paquete(s) vencidos por la solicitud (SAT)")
                                      .arg(aplicada.paquetesVencidos.size()),
                                  {}, std::nullopt})) {
                return RB::fallo(*e);
            }
        }
        return RB::exito(true);
    }

    // Exito con false si la solicitud ya no esta Enviada.
    RB aplicarFallaVerificacion(const SolicitudId& id, OrigenLog origen, const RachaVerificacion& racha,
                                const FallaOperacion& falla, const QDateTime& ahora)
    {
        const auto agenda = politicas::agendaTrasFalla(racha, falla, ahora);
        AplicacionFallaVerificacion a;
        a.solicitudId = id;
        a.falladaEn = ahora;
        a.ultimoError = diagnostico(falla);
        a.claveFalla = agenda.claveFalla;
        a.fallasIguales = agenda.fallasIguales;
        a.siguienteVerificacionEn = agenda.siguienteVerificacionEn;
        auto ap = p.operaciones.aplicarFallaVerificacion(a);
        if (!ap || !ap.valor()) {
            return ap;
        }
        const std::optional<QString> codigo =
            falla.fase == FaseOperacion::RespuestaExplicita ? falla.codigo : std::nullopt;
        if (agenda.registrarLog) {
            if (auto e = log(id, {TipoEventoLog::VerificacionFallida, origen, OrigenCodigoSat::Verificacion, codigo,
                                  std::nullopt, QStringLiteral("Verificacion fallida"), a.ultimoError, std::nullopt})) {
                return RB::fallo(*e);
            }
        }
        if (agenda.suspender && agenda.fallasIguales == politicas::kFallasParaSuspender) {
            if (auto e = log(id, {TipoEventoLog::VerificacionSuspendida, origen, OrigenCodigoSat::Verificacion, codigo,
                                  std::nullopt,
                                  QStringLiteral("Verificacion automatica suspendida hasta Verificar ahora"),
                                  a.claveFalla, std::nullopt})) {
                return RB::fallo(*e);
            }
        }
        return RB::exito(true);
    }

    // --- Descarga (D6, D14) ------------------------------------------------------

    R ejecutarDescarga(const QString& paqueteId, OrigenLog origen, std::optional<QDateTime> intencion,
                       const SenalCancelacion& cancelacion)
    {
        constexpr auto tipo = TipoOperacion::Descarga;
        std::optional<PaqueteDescargable> paquete;
        bool noAplica = false;
        RB marcado = enTransaccion([&]() -> RB {
            auto leido = p.operaciones.obtenerPaquete(paqueteId);
            if (!leido) {
                return RB::fallo(std::move(leido).error());
            }
            if (!leido.valor()) {
                return RB::exito(false);
            }
            paquete = *leido.valor();
            auto m = p.operaciones.marcarDescargando(paqueteId, reloj(), origen == OrigenLog::Usuario);
            if (!m) {
                return m;
            }
            if (!m.valor()) {
                // Ya no aplica: si venia de una intencion, se descarta con log
                // (se confirma esa limpieza, pero la descarga no se ejecuta).
                noAplica = true;
                if (!intencion) {
                    return RB::exito(false);
                }
                if (auto e = consumir(paquete->paquete.solicitudMasivaId, TipoIntencion::Descarga, *intencion, true)) {
                    return RB::fallo(*e);
                }
                return RB::exito(true);
            }
            if (auto e = log(paquete->paquete.solicitudMasivaId,
                             {TipoEventoLog::DescargaIniciada, origen, std::nullopt, std::nullopt, std::nullopt,
                              QStringLiteral("Descarga iniciada"), {}, paquete->paquete.idPaqueteSat})) {
                return RB::fallo(*e);
            }
            return RB::exito(true);
        });
        const std::optional<SolicitudId> sid =
            paquete ? std::optional(paquete->paquete.solicitudMasivaId) : std::nullopt;
        if (!marcado) {
            return errorLocal(tipo, origen, sid, paqueteId);
        }
        if (!marcado.valor() || noAplica) {
            if (noAplica && marcado.valor()) {
                emit q->solicitudActualizada(*sid);
            }
            return resultado(tipo, origen, Desenlace::Descartada, sid, paqueteId);
        }
        ContextoDescarga ctx{paqueteId, paquete->paquete.solicitudMasivaId, paquete->perfilSatId, paquete->rfcSolicitante,
                             paquete->paquete.idPaqueteSat, cancelacion};
        auto r = p.sat.descargar(ctx);
        const QDateTime ahora = reloj();
        std::optional<FallaOperacion> falla = r.esExito() ? std::nullopt : std::optional(r.error());

        AplicacionDescarga a;
        a.paqueteId = paqueteId;
        a.aplicadaEn = ahora;
        DatosLog d{TipoEventoLog::DescargaFallida, origen, OrigenCodigoSat::Descarga, std::nullopt, std::nullopt, {}, {},
                   paquete->paquete.idPaqueteSat};
        if (r.esExito()) {
            a.destino = EstadoDescarga::Descargado;
            a.rutaFinal = r.valor().rutaFinal;
            a.codigoDescargaSat = r.valor().codEstatus;
            a.mensajeDescargaSat = r.valor().mensaje;
            d.tipo = TipoEventoLog::PaqueteDescargado;
            d.codigo = r.valor().codEstatus;
            d.mensaje = QStringLiteral("Paquete descargado");
            if (r.valor().advertenciaDurabilidad) {
                d.detalle = QStringLiteral("Advertencia de durabilidad: ") + *r.valor().advertenciaDurabilidad;
            }
        } else {
            const auto desenlace = politicas::desenlaceDescarga(*falla);
            a.destino = desenlace.destino;
            a.motivoVencimiento = desenlace.motivo;
            a.origenVencimiento = desenlace.origen;
            a.ultimoError = diagnostico(*falla);
            if (falla->fase == FaseOperacion::RespuestaExplicita) {
                a.codigoDescargaSat = falla->codigo;
                d.codigo = falla->codigo;
            }
            d.detalle = *a.ultimoError;
            if (a.destino == EstadoDescarga::Vencido) {
                d.tipo = TipoEventoLog::PaqueteVencido;
                d.mensaje = QStringLiteral("SAT reporta el paquete como inexistente (vencido)");
            } else {
                d.tipo = TipoEventoLog::DescargaFallida;
                d.mensaje = QStringLiteral("Descarga fallida");
            }
        }
        RB aplicado = enTransaccion([&]() -> RB {
            auto ap = p.operaciones.aplicarDescarga(a);
            if (!ap || !ap.valor()) {
                return ap;
            }
            if (auto e = log(paquete->paquete.solicitudMasivaId, d)) {
                return RB::fallo(*e);
            }
            if (intencion) {
                if (auto e = consumir(paquete->paquete.solicitudMasivaId, TipoIntencion::Descarga, *intencion, false)) {
                    return RB::fallo(*e);
                }
            }
            return RB::exito(true);
        });
        if (!aplicado) {
            return errorLocal(tipo, origen, sid, paqueteId, falla);
        }
        if (!aplicado.valor()) {
            return resultado(tipo, origen, Desenlace::Descartada, sid, paqueteId, falla);
        }
        emit q->solicitudActualizada(*sid);
        return resultado(tipo, origen, Desenlace::Aplicada, sid, paqueteId, falla);
    }

    // --- Intenciones, vencimiento y recuperacion ---------------------------------

    R ejecutarRegistroIntencion(const SolicitudId& id, TipoIntencion tipoIntencion)
    {
        constexpr auto tipo = TipoOperacion::RegistroIntencion;
        RB r = enTransaccion([&]() -> RB {
            auto reg = p.operaciones.registrarIntencion(id, tipoIntencion, reloj());
            if (!reg || !reg.valor()) {
                return reg;
            }
            if (auto e = log(id, {TipoEventoLog::AccionPendienteRegistrada, OrigenLog::Usuario, std::nullopt,
                                  std::nullopt, std::nullopt,
                                  tipoIntencion == TipoIntencion::Verificacion
                                      ? QStringLiteral("Verificacion pendiente por pausa")
                                      : QStringLiteral("Descarga pendiente por pausa"),
                                  {}, std::nullopt})) {
                return RB::fallo(*e);
            }
            return RB::exito(true);
        });
        if (!r) {
            return errorLocal(tipo, OrigenLog::Usuario, id, std::nullopt);
        }
        if (r.valor()) {
            emit q->solicitudActualizada(id);
        }
        return resultado(tipo, OrigenLog::Usuario, r.valor() ? Desenlace::Aplicada : Desenlace::Descartada, id);
    }

    R ejecutarVencimiento(const QString& paqueteId)
    {
        constexpr auto tipo = TipoOperacion::VencimientoEstimado;
        std::optional<SolicitudId> sid;
        RB r = enTransaccion([&]() -> RB {
            auto leido = p.operaciones.obtenerPaquete(paqueteId);
            if (!leido) {
                return RB::fallo(std::move(leido).error());
            }
            if (!leido.valor()) {
                return RB::exito(false);
            }
            sid = leido.valor()->paquete.solicitudMasivaId;
            auto v = p.operaciones.vencerPaqueteEstimado(paqueteId, reloj());
            if (!v || !v.valor()) {
                return v;
            }
            if (auto e = log(*sid, {TipoEventoLog::PaqueteVencido, OrigenLog::Worker, std::nullopt, std::nullopt,
                                    std::nullopt, QStringLiteral("Paquete vencido por estimacion local (72 h)"), {},
                                    leido.valor()->paquete.idPaqueteSat})) {
                return RB::fallo(*e);
            }
            return RB::exito(true);
        });
        if (!r) {
            return errorLocal(tipo, OrigenLog::Worker, sid, paqueteId);
        }
        if (r.valor()) {
            emit q->solicitudActualizada(*sid);
        }
        return resultado(tipo, OrigenLog::Worker, r.valor() ? Desenlace::Aplicada : Desenlace::Descartada, sid, paqueteId);
    }

    // Revalidacion para escrituras que no pasan por un aplicar* condicional:
    // solicitud visible, paquete visible y aun en Descargando.
    RB sigueVisibleDescargando(const QString& paqueteId, const SolicitudId& solicitudId)
    {
        auto s = p.solicitudes.obtenerVisible(solicitudId);
        if (!s) {
            return RB::fallo(std::move(s).error());
        }
        if (!s.valor()) {
            return RB::exito(false);
        }
        auto paq = p.operaciones.obtenerPaquete(paqueteId);
        if (!paq) {
            return RB::fallo(std::move(paq).error());
        }
        return RB::exito(paq.valor() && !paq.valor()->paquete.eliminadoEn
                         && paq.valor()->paquete.estadoDescarga == EstadoDescarga::Descargando);
    }

    // --- Archivos (T008 D3, D9, D10, D11) ------------------------------------

    std::optional<QList<HallazgoFilesystem>> escanearArchivos()
    {
        if (!p.almacenamiento) {
            qCInfo(lcRecuperacionArchivos, "sin almacenamiento de paquetes: se omite el escaneo de archivos");
            return std::nullopt;
        }
        auto r = p.almacenamiento->escanearRecuperacion();
        if (!r) {
            qCWarning(lcRecuperacionArchivos, "escaneo de recuperacion fallido (%s): no se borra nada",
                      qPrintable(claveEstable(r.error())));
            return std::nullopt;
        }
        return std::move(r).valor();
    }

    void procesarHallazgos(const QList<HallazgoFilesystem>& hallazgos, const QList<PaqueteDescargable>& descargando,
                           bool& error)
    {
        for (const HallazgoFilesystem& h : hallazgos) {
            if (h.tipo == TipoHallazgo::Temporal) {
                const bool asociado = std::any_of(descargando.cbegin(), descargando.cend(), [&](const PaqueteDescargable& pd) {
                    return pd.paquete.solicitudMasivaId == h.solicitudId
                           && rutapaquete::nombreArchivoFinal(pd.paquete.idPaqueteSat) == h.archivoFinal;
                });
                auto borrado = p.almacenamiento->eliminarTemporal(h);
                if (!borrado) {
                    qCWarning(lcRecuperacionArchivos, "no se pudo eliminar el temporal %s (%s)",
                              qPrintable(h.rutaRelativa), qPrintable(claveEstable(borrado.error())));
                } else if (!asociado) {
                    qCInfo(lcRecuperacionArchivos, "temporal no asociable eliminado: %s", qPrintable(h.rutaRelativa));
                }
                continue;
            }
            procesarFinal(h, error);
        }
    }

    void procesarFinal(const HallazgoFilesystem& h, bool& error)
    {
        auto solicitud = p.solicitudes.obtenerVisible(h.solicitudId);
        if (!solicitud) {
            qCWarning(lcRecuperacionArchivos, "no se pudo leer la solicitud del archivo %s", qPrintable(h.rutaRelativa));
            error = true;
            return;
        }
        if (!solicitud.valor()) {
            // D3: sin solicitud asociable; se conserva y solo hay diagnostico.
            qCWarning(lcRecuperacionArchivos, "archivo final sin solicitud asociable (se conserva): %s",
                      qPrintable(h.rutaRelativa));
            return;
        }
        if (!p.paquetes) {
            return; // sin repositorio de paquetes no se puede asociar
        }
        auto paquetes = p.paquetes->listarVisiblesPorSolicitud(h.solicitudId);
        if (!paquetes) {
            error = true;
            return;
        }
        for (const PaquetePersistido& paq : paquetes.valor()) {
            if (rutapaquete::nombreArchivoFinal(paq.idPaqueteSat) == h.archivoFinal) {
                return; // archivo de un paquete registrado
            }
        }
        // Huerfano: se conserva y se registra una sola vez (D10), revalidando
        // la visibilidad en la misma transaccion (D5).
        const QString marca = QStringLiteral("Archivo final sin paquete registrado: ") + h.archivoFinal;
        RB r = enTransaccion([&]() -> RB {
            auto visible = p.solicitudes.obtenerVisible(h.solicitudId);
            if (!visible) {
                return RB::fallo(std::move(visible).error());
            }
            if (!visible.valor()) {
                return RB::exito(false);
            }
            auto previos = p.logs.listarVisiblesPorSolicitud(h.solicitudId);
            if (!previos) {
                return RB::fallo(std::move(previos).error());
            }
            for (const LogPersistido& l : previos.valor()) {
                if (l.tipoEvento == TipoEventoLog::ArchivoHuerfano && l.payloadResumenJson
                    && l.payloadResumenJson->contains(h.archivoFinal)) {
                    return RB::exito(false);
                }
            }
            if (auto e = log(h.solicitudId, {TipoEventoLog::ArchivoHuerfano, OrigenLog::Recuperacion, std::nullopt,
                                             std::nullopt, std::nullopt, marca,
                                             QStringLiteral("Ruta relativa: ") + h.rutaRelativa, std::nullopt})) {
                return RB::fallo(*e);
            }
            return RB::exito(true);
        });
        error = error || !r;
        if (r && r.valor()) {
            emit q->solicitudActualizada(h.solicitudId);
        }
    }

    ExistenciaArchivo ejecutarExistencia(const QString& rutaRelativa)
    {
        ExistenciaArchivo e = ExistenciaArchivo::ErrorComprobacion;
        if (p.almacenamiento) {
            auto r = p.almacenamiento->existeArchivoFinal(rutaRelativa);
            if (r) {
                e = r.valor() ? ExistenciaArchivo::Presente : ExistenciaArchivo::NoEncontrado;
            }
        }
        emit q->existenciaConsultada(rutaRelativa, e);
        return e;
    }

    R ejecutarRecuperacion(const SenalCancelacion& cancelacion)
    {
        constexpr auto tipo = TipoOperacion::Recuperacion;
        const OrigenLog origen = OrigenLog::Recuperacion;
        auto interrumpido = p.operaciones.listarInterrumpidos();
        if (!interrumpido) {
            return errorLocal(tipo, origen, std::nullopt, std::nullopt);
        }
        // T008 D9: el escaneo se hace antes de aplicar nada; si falla no se
        // borra nada y la recuperacion SQLite continua.
        const std::optional<QList<HallazgoFilesystem>> hallazgos = escanearArchivos();
        bool error = false;
        for (const SolicitudId& id : interrumpido.valor().solicitudesEnviando) {
            AplicacionEnvio a;
            a.solicitudId = id;
            a.destino = EstadoLocal::EnvioIncierto;
            a.ultimoError = QStringLiteral("Envio interrumpido por cierre; resultado incierto");
            RB r = enTransaccion([&]() -> RB {
                auto ap = p.operaciones.aplicarEnvio(a);
                if (!ap || !ap.valor()) {
                    return ap;
                }
                if (auto e = log(id, {TipoEventoLog::EnvioIncierto, origen, std::nullopt, std::nullopt, std::nullopt,
                                      QStringLiteral("Envio interrumpido al cerrar la app"), {}, std::nullopt})) {
                    return RB::fallo(*e);
                }
                return RB::exito(true);
            });
            error = error || !r;
            if (r && r.valor()) {
                emit q->solicitudActualizada(id);
            }
        }
        for (const PaqueteDescargable& pd : interrumpido.valor().paquetesDescargando) {
            const PaquetePersistido& paq = pd.paquete;
            ContextoArchivoFinal ctx{paq.id, paq.solicitudMasivaId, pd.perfilSatId, paq.idPaqueteSat, cancelacion};
            auto existe = p.sat.existeArchivoFinal(ctx);
            const QDateTime ahora = reloj();
            RB r = enTransaccion([&]() -> RB {
                if (!existe.esExito()) {
                    // D14: no se infiere ausencia; conserva Descargando. D5: el
                    // log solo se escribe si solicitud y paquete siguen visibles
                    // y el paquete sigue en Descargando (revalidado aqui, en la
                    // misma transaccion).
                    auto vigente = sigueVisibleDescargando(paq.id, paq.solicitudMasivaId);
                    if (!vigente || !vigente.valor()) {
                        return vigente;
                    }
                    if (auto e = log(paq.solicitudMasivaId,
                                     {TipoEventoLog::DescargaInterrumpida, origen, std::nullopt, std::nullopt, std::nullopt,
                                      QStringLiteral("No se pudo reconciliar la descarga interrumpida"),
                                      diagnostico(existe.error()), paq.idPaqueteSat})) {
                        return RB::fallo(*e);
                    }
                    return RB::exito(true);
                }
                AplicacionDescarga a;
                a.paqueteId = paq.id;
                a.aplicadaEn = ahora;
                a.reconciliado = true;
                DatosLog d{TipoEventoLog::DescargaInterrumpida, origen, std::nullopt, std::nullopt, std::nullopt,
                           QStringLiteral("Descarga interrumpida; el paquete vuelve a Disponible"), {}, paq.idPaqueteSat};
                if (existe.valor().existe) {
                    a.destino = EstadoDescarga::Descargado;
                    a.rutaFinal = existe.valor().rutaFinal.value_or(QString());
                    d.tipo = TipoEventoLog::PaqueteReconciliado;
                    d.mensaje = QStringLiteral("Archivo final encontrado; paquete reconciliado como Descargado");
                } else {
                    a.destino = EstadoDescarga::Disponible;
                    a.reconciliado = false;
                }
                auto ap = p.operaciones.aplicarDescarga(a);
                if (!ap || !ap.valor()) {
                    return ap;
                }
                if (auto e = log(paq.solicitudMasivaId, d)) {
                    return RB::fallo(*e);
                }
                return RB::exito(true);
            });
            error = error || !r;
            if (r && r.valor()) {
                emit q->solicitudActualizada(paq.solicitudMasivaId);
            }
        }
        if (hallazgos) {
            // Despues de aplicar la regla de T007 a los paquetes Descargando.
            procesarHallazgos(*hallazgos, interrumpido.valor().paquetesDescargando, error);
        }
        return resultado(tipo, origen, error ? Desenlace::ErrorLocal : Desenlace::Aplicada);
    }

    QList<std::pair<PerfilId, EstadoCredencial>> ejecutarConsultaCredenciales(const QList<PerfilId>& perfiles)
    {
        QList<std::pair<PerfilId, EstadoCredencial>> r;
        QSet<PerfilId> vistos;
        for (const PerfilId& perfil : perfiles) {
            if (vistos.contains(perfil)) {
                continue; // una vez por perfil (D9)
            }
            vistos.insert(perfil);
            auto estado = p.sat.obtenerEstadoCredencial(perfil);
            if (!estado.esExito()) {
                continue;
            }
            r.append({perfil, estado.valor()});
            const auto anterior = credencialesObservadas.constFind(perfil);
            if (anterior == credencialesObservadas.cend() || *anterior != estado.valor()) {
                credencialesObservadas.insert(perfil, estado.valor());
                emit q->estadoCredencialCambiado(perfil, estado.valor());
            }
        }
        return r;
    }
};

OperacionExecutor::OperacionExecutor(PuertosEjecutor puertos, RelojUtc reloj, Programador& programador,
                                     QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this, std::move(puertos), std::move(reloj), programador))
{
    qRegisterMetaType<TipoOperacion>();
    qRegisterMetaType<ResultadoOperacion>();
    qRegisterMetaType<SolicitudId>();
    qRegisterMetaType<PerfilId>();
    qRegisterMetaType<EstadoCredencial>();
    qRegisterMetaType<InstantaneaWorker>();
    qRegisterMetaType<ExistenciaArchivo>();

    m_impl->hilo.setObjectName(QStringLiteral("satcfdi-ejecutor"));
    m_impl->contexto = new QObject;
    m_impl->contexto->moveToThread(&m_impl->hilo);
    connect(&m_impl->hilo, &QThread::finished, m_impl->contexto, &QObject::deleteLater);
    connect(&m_impl->hilo, &QThread::finished, this, [this]() {
        if (m_impl->plazoId) {
            m_impl->programador.cancelar(*m_impl->plazoId);
            m_impl->plazoId.reset();
        }
        if (m_impl->promesaDetener) {
            m_impl->promesaDetener->finish();
        }
        emit detenido();
    });
    m_impl->hilo.start();
}

OperacionExecutor::~OperacionExecutor()
{
    // Salida inmediata: deja de aceptar, rechaza lo encolado y, si no hay
    // operacion activa, encola finalizarEnHilo (cierra la conexion del hilo y
    // termina el loop). Con operacion activa, finalizarEnHilo corre al terminar
    // esa operacion, tras la cancelacion cooperativa de abajo.
    if (!m_impl->promesaDetener) {
        detener(std::chrono::milliseconds(0));
    }
    if (m_impl->plazoId) {
        m_impl->programador.cancelar(*m_impl->plazoId);
        m_impl->plazoId.reset();
    }
    SenalCancelacion actual;
    {
        QMutexLocker l(&m_impl->mutex);
        actual = m_impl->cancelacionActual;
    }
    actual.solicitar(); // fuera del mutex: las notificaciones pueden bloquear
    // NO se llama hilo.quit(): el hilo termina solo despues de ejecutar
    // cerrarConexion en su propio hilo. La espera dura lo que tarde el puerto
    // en atender la cancelacion (D12) y nada mas.
    m_impl->hilo.wait();
}

QFuture<ResultadoOperacion> OperacionExecutor::enviar(const SolicitudId& solicitudId)
{
    return m_impl->encolarOperacion(TipoOperacion::Envio, OrigenLog::Usuario, solicitudId, std::nullopt,
                                    [this, solicitudId](const SenalCancelacion& c) {
                                        return m_impl->ejecutarEnvio(solicitudId, c);
                                    });
}

QFuture<ResultadoOperacion> OperacionExecutor::verificar(const SolicitudId& solicitudId, OrigenLog origen,
                                                         std::optional<QDateTime> intencionCapturadaEn)
{
    return m_impl->encolarOperacion(TipoOperacion::Verificacion, origen, solicitudId, std::nullopt,
                                    [this, solicitudId, origen, intencionCapturadaEn](const SenalCancelacion& c) {
                                        return m_impl->ejecutarVerificacion(solicitudId, origen, intencionCapturadaEn, c);
                                    });
}

QFuture<ResultadoOperacion> OperacionExecutor::descargar(const QString& paqueteId, OrigenLog origen,
                                                         std::optional<QDateTime> intencionCapturadaEn)
{
    return m_impl->encolarOperacion(TipoOperacion::Descarga, origen, std::nullopt, paqueteId,
                                    [this, paqueteId, origen, intencionCapturadaEn](const SenalCancelacion& c) {
                                        return m_impl->ejecutarDescarga(paqueteId, origen, intencionCapturadaEn, c);
                                    });
}

QFuture<ResultadoOperacion> OperacionExecutor::registrarIntencion(const SolicitudId& solicitudId, TipoIntencion tipo)
{
    return m_impl->encolarOperacion(TipoOperacion::RegistroIntencion, OrigenLog::Usuario, solicitudId, std::nullopt,
                                    [this, solicitudId, tipo](const SenalCancelacion&) {
                                        return m_impl->ejecutarRegistroIntencion(solicitudId, tipo);
                                    });
}

QFuture<ResultadoOperacion> OperacionExecutor::descartarIntencion(const SolicitudId& solicitudId, TipoIntencion tipo,
                                                                  const QDateTime& capturadaEn)
{
    return m_impl->encolarOperacion(
        TipoOperacion::RegistroIntencion, OrigenLog::Usuario, solicitudId, std::nullopt,
        [this, solicitudId, tipo, capturadaEn](const SenalCancelacion&) {
            constexpr auto t = TipoOperacion::RegistroIntencion;
            RB r = m_impl->enTransaccion([&]() -> RB {
                auto c = m_impl->p.operaciones.consumirIntencion(solicitudId, tipo, capturadaEn);
                if (!c || !c.valor()) {
                    return c;
                }
                if (auto e = m_impl->log(solicitudId,
                                         {TipoEventoLog::AccionPendienteDescartada, OrigenLog::Usuario, std::nullopt,
                                          std::nullopt, std::nullopt,
                                          tipo == TipoIntencion::Verificacion
                                              ? QStringLiteral("Verificacion pendiente ya no aplica")
                                              : QStringLiteral("Descarga pendiente ya no aplica"),
                                          {}, std::nullopt})) {
                    return RB::fallo(*e);
                }
                return RB::exito(true);
            });
            if (!r) {
                return m_impl->errorLocal(t, OrigenLog::Usuario, solicitudId, std::nullopt);
            }
            if (r.valor()) {
                emit solicitudActualizada(solicitudId);
            }
            return resultado(t, OrigenLog::Usuario, r.valor() ? Desenlace::Aplicada : Desenlace::Descartada, solicitudId);
        });
}

QFuture<ResultadoOperacion> OperacionExecutor::vencerEstimado(const QString& paqueteId)
{
    return m_impl->encolarOperacion(TipoOperacion::VencimientoEstimado, OrigenLog::Worker, std::nullopt, paqueteId,
                                    [this, paqueteId](const SenalCancelacion&) {
                                        return m_impl->ejecutarVencimiento(paqueteId);
                                    });
}

QFuture<ResultadoOperacion> OperacionExecutor::recuperar()
{
    return m_impl->encolarOperacion(TipoOperacion::Recuperacion, OrigenLog::Recuperacion, std::nullopt, std::nullopt,
                                    [this](const SenalCancelacion& c) { return m_impl->ejecutarRecuperacion(c); });
}

QFuture<ExistenciaArchivo> OperacionExecutor::consultarExistencia(const QString& rutaRelativa)
{
    return m_impl->encolar<ExistenciaArchivo>(
        PrioridadOperacion::Manual, std::nullopt,
        [this, rutaRelativa](const SenalCancelacion&) { return m_impl->ejecutarExistencia(rutaRelativa); },
        [this, rutaRelativa]() {
            emit existenciaConsultada(rutaRelativa, ExistenciaArchivo::ErrorComprobacion);
            return ExistenciaArchivo::ErrorComprobacion;
        });
}

QFuture<QList<std::pair<PerfilId, EstadoCredencial>>>
OperacionExecutor::consultarCredenciales(const QList<PerfilId>& perfiles)
{
    using L = QList<std::pair<PerfilId, EstadoCredencial>>;
    return m_impl->encolar<L>(PrioridadOperacion::Automatica, std::nullopt,
                              [this, perfiles](const SenalCancelacion&) {
                                  return m_impl->ejecutarConsultaCredenciales(perfiles);
                              },
                              []() { return L{}; });
}

QFuture<void> OperacionExecutor::leer(std::function<void(OperacionesSolicitudRepository&)> lectura)
{
    return m_impl->encolar<void>(PrioridadOperacion::Automatica, std::nullopt,
                                 [this, lectura = std::move(lectura)](const SenalCancelacion&) {
                                     lectura(m_impl->p.operaciones);
                                 },
                                 {});
}

bool OperacionExecutor::aceptaOperaciones() const noexcept
{
    QMutexLocker l(&m_impl->mutex);
    return m_impl->aceptando;
}

QFuture<void> OperacionExecutor::detener(std::chrono::milliseconds plazo)
{
    std::vector<Trabajo> rechazados;
    bool activo = false;
    {
        QMutexLocker l(&m_impl->mutex);
        if (m_impl->promesaDetener) {
            return m_impl->futuroDetener; // idempotente: la senal repetida no duplica nada
        }
        m_impl->promesaDetener = std::make_shared<QPromise<void>>();
        m_impl->promesaDetener->start();
        m_impl->futuroDetener = m_impl->promesaDetener->future();
        m_impl->aceptando = false;
        m_impl->terminando = true;
        for (auto& cola : m_impl->colas) {
            for (auto& t : cola) {
                rechazados.push_back(std::move(t));
            }
            cola.clear();
        }
        activo = m_impl->activo;
    }
    for (Trabajo& t : rechazados) {
        t.rechazar();
    }
    if (activo) {
        auto cancelar = [this]() {
            QMutexLocker l(&m_impl->mutex);
            m_impl->plazoId.reset();
            m_impl->cancelacionActual.solicitar();
        };
        if (plazo.count() <= 0) {
            cancelar();
        } else {
            m_impl->plazoId = m_impl->programador.programar(m_impl->reloj().addMSecs(plazo.count()), cancelar);
        }
    } else {
        QMetaObject::invokeMethod(m_impl->contexto, [this]() { m_impl->finalizarEnHilo(); }, Qt::QueuedConnection);
    }
    return m_impl->futuroDetener;
}

} // namespace satcfdi
