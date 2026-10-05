#include "application/operaciones/WorkerLocal.h"

#include "application/configuration/ConfiguracionAppService.h"
#include "application/operaciones/OperacionExecutor.h"
#include "ports/repositories/OperacionesSolicitudRepository.h"

#include <QFuture>
#include <QHash>
#include <QSet>

#include <memory>

namespace satcfdi {

namespace {

// Seleccion acotada por ciclo (D9) y horizonte para la agenda.
constexpr int kLimite = 20;
constexpr std::chrono::minutes kLatidoMaximo{30};

QDateTime lejano(const QDateTime& ahora)
{
    return ahora.addYears(100);
}

std::optional<QDateTime> minimo(std::optional<QDateTime> a, std::optional<QDateTime> b)
{
    if (!a) {
        return b;
    }
    if (!b) {
        return a;
    }
    return std::min(*a, *b);
}

// Lectura inicial de un ciclo (hilo del ejecutor).
struct SeleccionInicial {
    QList<QString> vencimientos;            // paquetes con vencimiento estimado alcanzado
    QList<PerfilId> perfilesConTrabajo;     // para el gate (solo sin pausa)
    std::optional<QDateTime> proximaVerificacion;
    std::optional<QDateTime> proximoVencimiento;
    int pendientes = 0;
    bool error = false;
};

// Lectura de trabajo SAT de los perfiles listos (hilo del ejecutor).
struct SeleccionSat {
    QList<IntencionPendiente> intenciones;
    QHash<SolicitudId, QList<QString>> reintentables; // por intencion de descarga
    QList<SolicitudId> verificaciones;
    QList<QString> descargas;
};

} // namespace

struct WorkerLocal::Impl {
    WorkerLocal* q;
    OperacionExecutor& ejecutor;
    ConfiguracionAppService& configuracion;
    Programador& programador;
    RelojUtc reloj;

    bool iniciado = false;
    bool recuperado = false;
    bool configCargada = false;
    bool deteniendo = false;
    bool detenido = false;
    bool pausado = false;
    bool cicloEnCurso = false;
    bool cicloPendiente = false;
    std::optional<Programador::Id> cicloId;
    std::optional<TipoOperacion> ejecutando;
    int pendientes = 0;
    InstantaneaWorker ultima;

    Impl(WorkerLocal* q_, OperacionExecutor& e, ConfiguracionAppService& c, Programador& p, RelojUtc r)
        : q(q_), ejecutor(e), configuracion(c), programador(p), reloj(std::move(r))
    {
    }

    bool listo() const { return iniciado && recuperado && configCargada && !deteniendo && !detenido; }

    void publicar()
    {
        InstantaneaWorker i;
        if (detenido || !iniciado) {
            i.estado = EstadoWorker::Detenido;
        } else if (deteniendo) {
            i.estado = EstadoWorker::Deteniendo;
        } else if (ejecutando) {
            i.estado = EstadoWorker::Ejecutando;
            i.tipo = ejecutando;
        } else if (pausado) {
            i.estado = EstadoWorker::Pausado;
        } else {
            i.estado = EstadoWorker::ActivoEnEspera;
        }
        i.pendientes = pendientes;
        if (!(i == ultima)) {
            ultima = i;
            emit q->instantaneaCambiada(i);
        }
    }

    void fijarPausa(bool p)
    {
        const bool cambio = p != pausado;
        pausado = p;
        publicar();
        if (cambio && !p) {
            ejecutarCiclo(); // al reanudar: intenciones pendientes (D9)
        }
    }

    // --- Ciclo (D9) -------------------------------------------------------------

    void ejecutarCiclo()
    {
        if (!listo()) {
            return;
        }
        if (cicloEnCurso) {
            cicloPendiente = true;
            return;
        }
        if (cicloId) {
            programador.cancelar(*cicloId);
            cicloId.reset();
        }
        cicloEnCurso = true;
        const QDateTime ahora = reloj();
        const bool pausadoCiclo = pausado;
        auto sel = std::make_shared<SeleccionInicial>();
        ejecutor
            .leer([sel, ahora, pausadoCiclo](OperacionesSolicitudRepository& repo) {
                auto venc = repo.listarVencimientosEstimados(ahora, kLimite);
                auto todos = repo.listarPerfilesConTrabajo(lejano(ahora));
                if (!venc || !todos) {
                    sel->error = true;
                    return;
                }
                for (const PaqueteDescargable& p : venc.valor()) {
                    sel->vencimientos.append(p.paquete.id);
                }
                // Proximo vencimiento estimado futuro.
                if (auto futuros = repo.listarVencimientosEstimados(lejano(ahora), kLimite + 1)) {
                    for (const PaqueteDescargable& p : futuros.valor()) {
                        if (p.paquete.vencimientoEstimadoEn && *p.paquete.vencimientoEstimadoEn > ahora) {
                            sel->proximoVencimiento = p.paquete.vencimientoEstimadoEn;
                            break;
                        }
                    }
                }
                if (auto intenciones = repo.listarIntencionesPendientes(todos.valor(), 100000)) {
                    sel->pendientes = static_cast<int>(intenciones.valor().size());
                }
                if (pausadoCiclo) {
                    return;
                }
                if (auto perfiles = repo.listarPerfilesConTrabajo(ahora)) {
                    sel->perfilesConTrabajo = perfiles.valor();
                }
                // Proxima verificacion futura (agenda por solicitud, D8).
                if (auto programadas = repo.listarVerificacionesDebidas(todos.valor(), lejano(ahora), kLimite + 1)) {
                    for (const SolicitudPersistida& s : programadas.valor()) {
                        if (s.siguienteVerificacionEn && *s.siguienteVerificacionEn > ahora) {
                            sel->proximaVerificacion = s.siguienteVerificacionEn;
                            break;
                        }
                    }
                }
            })
            .then(q, [this, sel, ahora, pausadoCiclo]() {
                pendientes = sel->pendientes;
                publicar();
                QList<QFuture<ResultadoOperacion>> encoladas;
                for (const QString& p : sel->vencimientos) {
                    encoladas.append(ejecutor.vencerEstimado(p));
                }
                if (pausadoCiclo || sel->perfilesConTrabajo.isEmpty()) {
                    finalizarCiclo(encoladas, sel);
                    return;
                }
                // Gate por perfil: una consulta por perfil y ciclo.
                ejecutor.consultarCredenciales(sel->perfilesConTrabajo)
                    .then(q, [this, sel, ahora, encoladas](QList<std::pair<PerfilId, EstadoCredencial>> estados) {
                        QList<PerfilId> listos;
                        for (const auto& [perfil, estado] : estados) {
                            if (estado == EstadoCredencial::Lista) {
                                listos.append(perfil);
                            }
                        }
                        if (listos.isEmpty()) {
                            finalizarCiclo(encoladas, sel);
                            return;
                        }
                        seleccionarYEncolar(listos, ahora, encoladas, sel);
                    });
            });
    }

    void seleccionarYEncolar(const QList<PerfilId>& listos, const QDateTime& ahora,
                             QList<QFuture<ResultadoOperacion>> encoladas, std::shared_ptr<SeleccionInicial> inicial)
    {
        auto sel = std::make_shared<SeleccionSat>();
        ejecutor
            .leer([sel, listos, ahora](OperacionesSolicitudRepository& repo) {
                if (auto i = repo.listarIntencionesPendientes(listos, kLimite)) {
                    sel->intenciones = i.valor();
                    for (const IntencionPendiente& in : sel->intenciones) {
                        if (in.descargaPendiente) {
                            QList<QString> ids;
                            if (auto r = repo.listarPaquetesReintentables(in.solicitudId)) {
                                for (const PaqueteDescargable& p : r.valor()) {
                                    ids.append(p.paquete.id);
                                }
                            }
                            sel->reintentables.insert(in.solicitudId, ids);
                        }
                    }
                }
                if (auto v = repo.listarVerificacionesDebidas(listos, ahora, kLimite)) {
                    for (const SolicitudPersistida& s : v.valor()) {
                        sel->verificaciones.append(s.id);
                    }
                }
                if (auto d = repo.listarDescargasAutomaticas(listos, kLimite)) {
                    for (const PaqueteDescargable& p : d.valor()) {
                        sel->descargas.append(p.paquete.id);
                    }
                }
            })
            .then(q, [this, sel, encoladas, inicial]() mutable {
                QSet<SolicitudId> verificadas;
                QSet<QString> descargados;
                // Intenciones (manuales): verificar y luego descargar (D13).
                for (const IntencionPendiente& in : sel->intenciones) {
                    if (in.verificacionPendiente) {
                        verificadas.insert(in.solicitudId);
                        encoladas.append(ejecutor.verificar(in.solicitudId, OrigenLog::Usuario, in.accionPendienteEn));
                    }
                    if (in.descargaPendiente) {
                        const QList<QString> ids = sel->reintentables.value(in.solicitudId);
                        if (ids.isEmpty()) {
                            encoladas.append(
                                ejecutor.descartarIntencion(in.solicitudId, TipoIntencion::Descarga, in.accionPendienteEn));
                        }
                        for (qsizetype i = 0; i < ids.size(); ++i) {
                            descargados.insert(ids.at(i));
                            // La intencion se limpia despues de la ultima descarga.
                            encoladas.append(ejecutor.descargar(ids.at(i), OrigenLog::Usuario,
                                                                i + 1 == ids.size() ? std::optional(in.accionPendienteEn)
                                                                                    : std::nullopt));
                        }
                    }
                }
                // Verificaciones debidas, luego descargas automaticas.
                for (const SolicitudId& s : sel->verificaciones) {
                    if (!verificadas.contains(s)) {
                        encoladas.append(ejecutor.verificar(s, OrigenLog::Worker));
                    }
                }
                for (const QString& p : sel->descargas) {
                    if (!descargados.contains(p)) {
                        encoladas.append(ejecutor.descargar(p, OrigenLog::Worker));
                    }
                }
                finalizarCiclo(encoladas, inicial);
            });
    }

    void finalizarCiclo(const QList<QFuture<ResultadoOperacion>>& encoladas, std::shared_ptr<SeleccionInicial> sel)
    {
        auto terminar = [this, sel, hizoTrabajo = !encoladas.isEmpty()]() {
            cicloEnCurso = false;
            if (deteniendo || detenido) {
                return;
            }
            const QDateTime ahora = reloj();
            std::optional<QDateTime> siguiente = ahora.addSecs(
                std::chrono::duration_cast<std::chrono::seconds>(kLatidoMaximo).count());
            if (hizoTrabajo) {
                siguiente = ahora; // re-seleccionar de inmediato (nuevos paquetes, mas trabajo)
            } else {
                siguiente = minimo(siguiente, sel->proximoVencimiento);
                if (!pausado) {
                    siguiente = minimo(siguiente, sel->proximaVerificacion);
                }
            }
            cicloId = programador.programar(*siguiente, [this]() {
                cicloId.reset();
                ejecutarCiclo();
            });
            actualizarPendientes();
            if (cicloPendiente) {
                cicloPendiente = false;
                ejecutarCiclo();
            }
        };
        if (encoladas.isEmpty()) {
            terminar();
            return;
        }
        QList<QFuture<ResultadoOperacion>> copia = encoladas;
        QtFuture::whenAll(copia.begin(), copia.end()).then(q, [terminar](const QList<QFuture<ResultadoOperacion>>&) {
            terminar();
        });
    }

    void actualizarPendientes()
    {
        if (deteniendo || detenido) {
            return;
        }
        const QDateTime ahora = reloj();
        auto cuenta = std::make_shared<int>(0);
        ejecutor
            .leer([cuenta, ahora](OperacionesSolicitudRepository& repo) {
                auto perfiles = repo.listarPerfilesConTrabajo(lejano(ahora));
                if (!perfiles) {
                    return;
                }
                if (auto i = repo.listarIntencionesPendientes(perfiles.valor(), 100000)) {
                    *cuenta = static_cast<int>(i.valor().size());
                }
            })
            .then(q, [this, cuenta]() {
                pendientes = *cuenta;
                publicar();
            });
    }

    // Tras una accion manual: reprograma el ciclo para recoger la nueva agenda.
    void reprogramarPronto()
    {
        if (!listo() || cicloEnCurso) {
            cicloPendiente = cicloPendiente || cicloEnCurso;
            return;
        }
        if (cicloId) {
            programador.cancelar(*cicloId);
        }
        cicloId = programador.programar(reloj(), [this]() {
            cicloId.reset();
            ejecutarCiclo();
        });
    }
};

WorkerLocal::WorkerLocal(OperacionExecutor& ejecutor, ConfiguracionAppService& configuracion, Programador& programador,
                         RelojUtc reloj, QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this, ejecutor, configuracion, programador, std::move(reloj)))
{
    qRegisterMetaType<InstantaneaWorker>();
    connect(&configuracion, &ConfiguracionAppService::configuracionCambiada, this,
            [this](const ConfiguracionApp& c) { m_impl->fijarPausa(c.monitoreoPausado); });
    connect(&ejecutor, &OperacionExecutor::operacionIniciada, this, [this](TipoOperacion tipo) {
        m_impl->ejecutando = tipo;
        m_impl->publicar();
    });
    connect(&ejecutor, &OperacionExecutor::operacionTerminada, this, [this](const ResultadoOperacion& r) {
        m_impl->ejecutando.reset();
        m_impl->publicar();
        if (r.tipo == TipoOperacion::RegistroIntencion) {
            m_impl->actualizarPendientes();
        }
    });
    connect(&ejecutor, &OperacionExecutor::inactivo, this, [this]() {
        m_impl->ejecutando.reset();
        m_impl->publicar();
    });
    connect(&ejecutor, &OperacionExecutor::detenido, this, [this]() {
        m_impl->detenido = true;
        m_impl->ejecutando.reset();
        if (m_impl->cicloId) {
            m_impl->programador.cancelar(*m_impl->cicloId);
            m_impl->cicloId.reset();
        }
        m_impl->publicar();
    });
}

WorkerLocal::~WorkerLocal()
{
    if (m_impl->cicloId) {
        m_impl->programador.cancelar(*m_impl->cicloId);
    }
}

void WorkerLocal::iniciar()
{
    if (m_impl->iniciado) {
        return;
    }
    m_impl->iniciado = true;
    m_impl->publicar();
    // Recuperacion antes de cualquier ciclo (D9); en paralelo, la pausa.
    m_impl->ejecutor.recuperar().then(this, [this](const ResultadoOperacion&) {
        m_impl->recuperado = true;
        m_impl->ejecutarCiclo();
    });
    m_impl->configuracion.obtener().then(this, [this](const ConfiguracionAppService::ResultadoConfiguracion& r) {
        m_impl->configCargada = true;
        // Sin configuracion legible se asume activo (como el valor por defecto).
        m_impl->pausado = r.esExito() && r.valor().monitoreoPausado;
        m_impl->publicar();
        m_impl->ejecutarCiclo();
    });
}

void WorkerLocal::detener()
{
    m_impl->deteniendo = true;
    if (m_impl->cicloId) {
        m_impl->programador.cancelar(*m_impl->cicloId);
        m_impl->cicloId.reset();
    }
    m_impl->publicar();
}

void WorkerLocal::enviar(const SolicitudId& solicitudId)
{
    if (m_impl->deteniendo || m_impl->detenido) {
        return;
    }
    // D17: el envio manual se ejecuta aun con el monitoreo pausado.
    m_impl->ejecutor.enviar(solicitudId).then(this, [this](const ResultadoOperacion&) { m_impl->reprogramarPronto(); });
}

void WorkerLocal::verificarAhora(const SolicitudId& solicitudId)
{
    if (m_impl->deteniendo || m_impl->detenido) {
        return;
    }
    if (m_impl->pausado) {
        m_impl->ejecutor.registrarIntencion(solicitudId, TipoIntencion::Verificacion);
        return;
    }
    m_impl->ejecutor.verificar(solicitudId, OrigenLog::Usuario).then(this, [this](const ResultadoOperacion&) {
        m_impl->reprogramarPronto();
    });
}

void WorkerLocal::reintentarDescarga(const SolicitudId& solicitudId)
{
    if (m_impl->deteniendo || m_impl->detenido) {
        return;
    }
    if (m_impl->pausado) {
        m_impl->ejecutor.registrarIntencion(solicitudId, TipoIntencion::Descarga);
        return;
    }
    auto ids = std::make_shared<QList<QString>>();
    m_impl->ejecutor
        .leer([ids, solicitudId](OperacionesSolicitudRepository& repo) {
            if (auto r = repo.listarPaquetesReintentables(solicitudId)) {
                for (const PaqueteDescargable& p : r.valor()) {
                    ids->append(p.paquete.id);
                }
            }
        })
        .then(this, [this, ids]() {
            for (const QString& id : *ids) {
                m_impl->ejecutor.descargar(id, OrigenLog::Usuario);
            }
        });
}

void WorkerLocal::ejecutarCiclo()
{
    m_impl->ejecutarCiclo();
}

InstantaneaWorker WorkerLocal::instantanea() const
{
    return m_impl->ultima;
}

} // namespace satcfdi
