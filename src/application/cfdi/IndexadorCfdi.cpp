#include "application/cfdi/IndexadorCfdi.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "ports/repositories/UnitOfWork.h"

#include <QLoggingCategory>
#include <QMetaObject>

#include <algorithm>
#include <utility>

namespace satcfdi {

namespace {

Q_LOGGING_CATEGORY(lcIndiceCfdi, "satcfdi.cfdi.indice")

// D11: Id local del paquete enmascarado (solo el primer bloque del UUID).
QString idEnmascarado(const QString& paqueteId)
{
    return paqueteId.left(8) + QStringLiteral("...");
}

using ResultadoEscritura = Resultado<bool, ErrorPersistencia>;

// D11: clave estable del tipo de error. Nunca se registra
// ErrorPersistencia::mensaje ni la restriccion.
const char* claveTipoError(ErrorPersistencia::Tipo tipo)
{
    switch (tipo) {
    case ErrorPersistencia::Tipo::Almacenamiento:
        return "almacenamiento";
    case ErrorPersistencia::Tipo::Ocupado:
        return "ocupado";
    case ErrorPersistencia::Tipo::Integridad:
        return "integridad";
    case ErrorPersistencia::Tipo::Unicidad:
        return "unicidad";
    case ErrorPersistencia::Tipo::DedupBloqueado:
        return "dedup-bloqueado";
    case ErrorPersistencia::Tipo::NoEncontrado:
        return "no-encontrado";
    case ErrorPersistencia::Tipo::Migracion:
        return "migracion";
    case ErrorPersistencia::Tipo::Transaccion:
        return "transaccion";
    case ErrorPersistencia::Tipo::Interno:
        break;
    }
    return "interno";
}

void registrar(const QString& paqueteId, const CalculoPaquete& calculo)
{
    const QString id = idEnmascarado(paqueteId);
    if (calculo.tipo == CalculoPaquete::Tipo::ActualizarHuella) {
        qCInfo(lcIndiceCfdi).noquote() << "paquete" << id << "sin cambios de contenido; metadatos de huella actualizados";
        return;
    }
    const IndicePaquete& i = calculo.indice;
    switch (i.estado) {
    case EstadoIndicePaquete::Indexado:
        qCInfo(lcIndiceCfdi).noquote() << "paquete" << id << "indexado; cfdi:" << i.cfdis.size()
                                       << "ilegibles:" << i.conteos.cfdiIlegibles
                                       << "no-soportados:" << i.conteos.cfdiNoSoportados
                                       << "no-cfdi:" << i.conteos.xmlNoCfdi
                                       << "omitidas:" << i.conteos.totalOmitidas();
        break;
    case EstadoIndicePaquete::OrigenNoDisponible:
    case EstadoIndicePaquete::Ilegible:
        qCInfo(lcIndiceCfdi).noquote() << "paquete" << id << "sin indice; estado:" << claveEstadoIndice(i.estado)
                                       << "causa:" << i.causa;
        break;
    case EstadoIndicePaquete::Pendiente:
        qCInfo(lcIndiceCfdi).noquote() << "paquete" << id << "pendiente; el ZIP cambio durante la lectura";
        break;
    }
}

} // namespace

IndexadorCfdi::IndexadorCfdi(PuertosIndexadorCfdi puertos, QObject* parent)
    : QObject(parent)
    , m_puertos(std::move(puertos))
    , m_entorno{m_puertos.lector, m_puertos.archivos, m_puertos.parser, m_puertos.versionParser}
    , m_cancelado(std::make_shared<std::atomic_bool>(false))
    , m_contextoHilo(new QObject)
{
    if (!m_puertos.reloj) {
        m_puertos.reloj = relojSistema();
    }
    m_hilo.setObjectName(QStringLiteral("satcfdi-indice-cfdi"));
    m_contextoHilo->moveToThread(&m_hilo);
    connect(&m_hilo, &QThread::finished, m_contextoHilo, &QObject::deleteLater);
    m_hilo.start(QThread::LowPriority);
}

IndexadorCfdi::~IndexadorCfdi()
{
    detener();
    if (m_actual) {
        for (const auto& promesa : std::as_const(m_actual->promesas)) {
            promesa->future().cancel();
            promesa->finish();
        }
        m_actual.reset();
    }
    // El calculo en curso ve la cancelacion entre entradas y termina sin
    // escribir; quit() se encola detras de el.
    QThread* hilo = &m_hilo;
    QMetaObject::invokeMethod(m_contextoHilo, [hilo]() { hilo->quit(); }, Qt::QueuedConnection);
    m_hilo.wait();
}

void IndexadorCfdi::iniciar()
{
    if (m_iniciado || m_detenido) {
        return;
    }
    m_iniciado = true;
    encolar(std::nullopt);
}

QFuture<void> IndexadorCfdi::sincronizarSolicitud(const SolicitudId& solicitud)
{
    if (solicitud.esNulo()) {
        return QtFuture::makeReadyVoidFuture();
    }
    return encolar(solicitud);
}

void IndexadorCfdi::detener()
{
    if (m_detenido) {
        return;
    }
    m_detenido = true;
    m_cancelado->store(true);
    for (Trabajo& t : m_cola) {
        for (const auto& promesa : std::as_const(t.promesas)) {
            promesa->future().cancel();
            promesa->finish();
        }
    }
    m_cola.clear();
    publicarOcupado();
}

bool IndexadorCfdi::cubre(const SolicitudId& solicitud) const
{
    const auto alcanza = [this, &solicitud](const Trabajo& t) {
        if (t.alcance && *t.alcance != solicitud) {
            return false;
        }
        if (!t.leido) {
            return true;
        }
        for (qsizetype i = t.siguiente; i < t.paquetes.size(); ++i) {
            if (t.paquetes.at(i).solicitudId == solicitud) {
                return true;
            }
        }
        return m_solicitudEnCurso == solicitud;
    };
    if (m_actual && alcanza(*m_actual)) {
        return true;
    }
    for (const Trabajo& t : m_cola) {
        if (alcanza(t)) {
            return true;
        }
    }
    return false;
}

QFuture<void> IndexadorCfdi::encolar(std::optional<SolicitudId> alcance)
{
    if (m_detenido) {
        return QFuture<void>(); // cancelado
    }
    auto promesa = std::make_shared<QPromise<void>>();
    promesa->start();
    QFuture<void> futuro = promesa->future();
    // Coalescer: un trabajo en cola (no el actual) con el mismo alcance, o la
    // pasada completa en cola, ya cubre esta peticion.
    for (Trabajo& t : m_cola) {
        if (!t.alcance || t.alcance == alcance) {
            t.promesas.append(promesa);
            return futuro;
        }
    }
    Trabajo nuevo;
    nuevo.alcance = std::move(alcance);
    nuevo.promesas.append(promesa);
    m_cola.append(std::move(nuevo));
    publicarOcupado();
    avanzar();
    return futuro;
}

void IndexadorCfdi::avanzar()
{
    if (m_actual || m_detenido || m_cola.isEmpty()) {
        return;
    }
    m_actual = m_cola.takeFirst();
    CfdiCatalogRepository* catalogo = &m_puertos.catalogo;
    const std::optional<SolicitudId> alcance = m_actual->alcance;
    using ResultadoLista = Resultado<QList<PaqueteIndexable>, ErrorPersistencia>;
    m_puertos.dispatcher
        .despachar<ResultadoLista>([catalogo, alcance] { return catalogo->listarPaquetesIndexables(alcance); })
        .then(this,
              [this](ResultadoLista r) {
                  if (!m_actual) {
                      return;
                  }
                  if (!r.esExito()) {
                      qCWarning(lcIndiceCfdi, "no se pudieron leer los paquetes a indexar; error: %s",
                                claveTipoError(r.error().tipo));
                      terminarTrabajo();
                      return;
                  }
                  m_actual->paquetes = std::move(r).valor();
                  // Agrupar por solicitud (orden estable dentro de cada una) para
                  // emitir indiceCambiado una sola vez por solicitud, sin
                  // depender del orden que devuelva el repositorio.
                  std::stable_sort(m_actual->paquetes.begin(), m_actual->paquetes.end(),
                                   [](const PaqueteIndexable& a, const PaqueteIndexable& b) {
                                       return a.solicitudId.texto() < b.solicitudId.texto();
                                   });
                  m_actual->leido = true;
                  procesarSiguientePaquete();
              })
        .onCanceled(this, [this] { terminarTrabajo(); })
        .onFailed(this, [this] { terminarTrabajo(); });
}

void IndexadorCfdi::procesarSiguientePaquete()
{
    if (!m_actual) {
        return;
    }
    if (m_detenido || m_actual->siguiente >= m_actual->paquetes.size()) {
        terminarTrabajo();
        return;
    }
    const PaqueteIndexable paquete = m_actual->paquetes.at(m_actual->siguiente);
    ++m_actual->siguiente;
    if (m_solicitudEnCurso != paquete.solicitudId) {
        cerrarSolicitud();
        m_solicitudEnCurso = paquete.solicitudId;
    }

    // Calculo bloqueante en el hilo propio del indexador.
    auto promesa = std::make_shared<QPromise<CalculoPaquete>>();
    QFuture<CalculoPaquete> futuro = promesa->future();
    EntornoCalculoIndice* entorno = &m_entorno;
    const std::shared_ptr<std::atomic_bool> cancelado = m_cancelado;
    const QDateTime ahora = m_puertos.reloj();
    QMetaObject::invokeMethod(
        m_contextoHilo,
        [promesa, entorno, cancelado, paquete, ahora]() {
            promesa->start();
            const auto sigue = [cancelado] { return !cancelado->load(); };
            try {
                promesa->addResult(calcularIndicePaquete(paquete, *entorno, sigue, ahora));
            } catch (...) {
                promesa->setException(std::current_exception());
            }
            promesa->finish();
        },
        Qt::QueuedConnection);
    futuro.then(this, [this, paquete](CalculoPaquete calculo) { aplicar(paquete, std::move(calculo)); })
        .onCanceled(this, [this] { terminarTrabajo(); })
        .onFailed(this, [this, paquete] {
            qCWarning(lcIndiceCfdi).noquote() << "fallo inesperado al indexar el paquete"
                                              << idEnmascarado(paquete.paqueteId);
            continuar(paquete, false);
        });
}

void IndexadorCfdi::aplicar(const PaqueteIndexable& paquete, CalculoPaquete calculo)
{
    if (!m_actual) {
        return;
    }
    switch (calculo.tipo) {
    case CalculoPaquete::Tipo::Cancelado:
        // Solo ocurre al detener: nada se escribe; el paquete queda como estaba.
        qCInfo(lcIndiceCfdi).noquote() << "indexacion cancelada; paquete" << idEnmascarado(paquete.paqueteId)
                                       << "sin escribir";
        terminarTrabajo();
        return;
    case CalculoPaquete::Tipo::SinCambio:
        continuar(paquete, false);
        return;
    case CalculoPaquete::Tipo::ActualizarHuella:
    case CalculoPaquete::Tipo::Reemplazar:
        break;
    }
    if (m_detenido) {
        // El calculo termino despues de detener(): se descarta sin escribir.
        qCInfo(lcIndiceCfdi).noquote() << "indexador detenido; paquete" << idEnmascarado(paquete.paqueteId)
                                       << "sin escribir";
        terminarTrabajo();
        return;
    }

    // UNA transaccion por paquete (D10).
    CfdiCatalogRepository* catalogo = &m_puertos.catalogo;
    UnitOfWork* uow = &m_puertos.unidadDeTrabajo;
    const QDateTime ahora = m_puertos.reloj();
    auto compartido = std::make_shared<CalculoPaquete>(std::move(calculo));
    m_puertos.dispatcher
        .despachar<ResultadoEscritura>([catalogo, uow, compartido, ahora, paqueteId = paquete.paqueteId,
                                        cancelado = m_cancelado] {
            using R = ResultadoEscritura;
            // Ultima comprobacion justo antes de escribir (hilo del dispatcher):
            // detener() entre el despacho y la ejecucion tampoco escribe.
            if (cancelado->load()) {
                return R::exito(false);
            }
            auto begin = uow->begin();
            if (!begin.esExito()) {
                return R::fallo(std::move(begin).error());
            }
            ResultadoEscritura r = compartido->tipo == CalculoPaquete::Tipo::Reemplazar
                                       ? catalogo->reemplazarIndicePaquete(compartido->indice)
                                       : catalogo->actualizarMetadatosHuella(paqueteId, compartido->huella, ahora);
            if (!r.esExito() || !r.valor()) {
                (void)uow->rollback();
                return r;
            }
            auto commit = uow->commit();
            if (!commit.esExito()) {
                (void)uow->rollback();
                return R::fallo(std::move(commit).error());
            }
            return r;
        })
        .then(this,
              [this, paquete, compartido](ResultadoEscritura r) {
                  if (!r.esExito()) {
                      qCWarning(lcIndiceCfdi, "no se pudo escribir el indice del paquete %s; error: %s",
                                qUtf8Printable(idEnmascarado(paquete.paqueteId)), claveTipoError(r.error().tipo));
                      continuar(paquete, false);
                      return;
                  }
                  if (r.valor()) {
                      registrar(paquete.paqueteId, *compartido);
                  }
                  // Solo un reemplazo cambia lo que se lista o resume.
                  continuar(paquete, r.valor() && compartido->tipo == CalculoPaquete::Tipo::Reemplazar);
              })
        .onCanceled(this, [this] { terminarTrabajo(); })
        .onFailed(this, [this, paquete] { continuar(paquete, false); });
}

void IndexadorCfdi::continuar(const PaqueteIndexable& paquete, bool cambio)
{
    if (cambio && m_solicitudEnCurso == paquete.solicitudId) {
        m_solicitudCambio = true;
    }
    procesarSiguientePaquete();
}

void IndexadorCfdi::cerrarSolicitud()
{
    if (m_solicitudEnCurso && m_solicitudCambio) {
        const SolicitudId id = *m_solicitudEnCurso;
        m_solicitudCambio = false;
        emit indiceCambiado(id);
    }
    m_solicitudEnCurso.reset();
    m_solicitudCambio = false;
}

void IndexadorCfdi::terminarTrabajo()
{
    cerrarSolicitud();
    if (m_actual) {
        Trabajo terminado = std::move(*m_actual);
        m_actual.reset();
        for (const auto& promesa : std::as_const(terminado.promesas)) {
            if (m_detenido) {
                promesa->future().cancel();
            }
            promesa->finish();
        }
    }
    publicarOcupado();
    avanzar();
}

void IndexadorCfdi::publicarOcupado()
{
    const bool actual = ocupado();
    if (actual != m_ocupadoPublicado) {
        m_ocupadoPublicado = actual;
        emit ocupadoCambiado(actual);
    }
}

} // namespace satcfdi
