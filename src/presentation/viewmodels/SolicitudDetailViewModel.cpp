#include "SolicitudDetailViewModel.h"

#include "AccionesSolicitud.h"
#include "AccionesFinder.h"
#include "CatalogoMensajes.h"
#include "ConsultaExistenciaPaquetes.h"
#include "application/profiles/ConsultaPreparacionPerfiles.h"
#include "application/requests/SolicitudesService.h"
#include "domain/solicitudes/EstadoResumen.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QFuture>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace satcfdi {

QString claveEstable(ExistenciaPaquete existencia)
{
    switch (existencia) {
    case ExistenciaPaquete::Presente:
        return QStringLiteral("Presente");
    case ExistenciaPaquete::NoEncontrado:
        return QStringLiteral("NoEncontrado");
    case ExistenciaPaquete::ErrorComprobacion:
        break;
    }
    return QStringLiteral("ErrorComprobacion");
}


namespace {

QString texto(const std::optional<QString>& valor)
{
    return valor.value_or(QString());
}

QVariant fechaONulo(const std::optional<QDateTime>& valor)
{
    return valor ? QVariant(*valor) : QVariant::fromValue(nullptr);
}

} // namespace

SolicitudDetailViewModel::SolicitudDetailViewModel(SolicitudesService* servicio, QObject* parent)
    : QObject(parent)
    , m_servicio(servicio)
{
    Q_ASSERT(servicio != nullptr);
    connect(servicio, &SolicitudesService::solicitudActualizada, this,
            [this](const SolicitudId& id) {
                if (!m_solicitudId.isEmpty() && id.texto() == m_solicitudId
                    && m_estado != Estado::NoEncontrada) {
                    recargar();
                }
            });
    connect(servicio, &SolicitudesService::solicitudEliminada, this,
            [this](const SolicitudId& id) {
                if (m_solicitudId.isEmpty() || id.texto() != m_solicitudId) {
                    return;
                }
                ++m_genCarga; // ninguna lectura anterior puede restaurar el detalle
                setDetalle(std::nullopt);
                setEstado(Estado::NoEncontrada);
            });
}

#define SATCFDI_RESUMEN(expr, vacio) (m_detalle ? (expr) : (vacio))

QString SolicitudDetailViewModel::perfilRfc() const
{
    return SATCFDI_RESUMEN(m_detalle->resumen.perfilRfc, QString());
}

QString SolicitudDetailViewModel::rfcContraparte() const
{
    return SATCFDI_RESUMEN(texto(m_detalle->resumen.rfcContraparte), QString());
}

QString SolicitudDetailViewModel::tipoDescarga() const
{
    return SATCFDI_RESUMEN(claveEstable(m_detalle->resumen.tipoDescarga), QString());
}

QString SolicitudDetailViewModel::fechaInicial() const
{
    return SATCFDI_RESUMEN(m_detalle->resumen.fechaInicial.toString(Qt::ISODate), QString());
}

QString SolicitudDetailViewModel::fechaFinal() const
{
    return SATCFDI_RESUMEN(m_detalle->resumen.fechaFinal.toString(Qt::ISODate), QString());
}

QDateTime SolicitudDetailViewModel::creadaEn() const
{
    return SATCFDI_RESUMEN(m_detalle->resumen.creadaEn, QDateTime());
}

QString SolicitudDetailViewModel::fechaInicialSat() const
{
    return SATCFDI_RESUMEN(m_detalle->fechaInicialSat, QString());
}

QString SolicitudDetailViewModel::fechaFinalSat() const
{
    return SATCFDI_RESUMEN(m_detalle->fechaFinalSat, QString());
}

QStringList SolicitudDetailViewModel::rfcContrapartes() const
{
    return SATCFDI_RESUMEN(m_detalle->rfcContrapartes, QStringList());
}

QString SolicitudDetailViewModel::tipoComprobante() const
{
    return SATCFDI_RESUMEN(texto(m_detalle->tipoComprobante), QString());
}

QString SolicitudDetailViewModel::complemento() const
{
    return SATCFDI_RESUMEN(texto(m_detalle->complemento), QString());
}

QString SolicitudDetailViewModel::estadoLocal() const
{
    return SATCFDI_RESUMEN(claveEstable(m_detalle->resumen.estadoLocal), QString());
}

QVariant SolicitudDetailViewModel::estadoSat() const
{
    if (m_detalle && m_detalle->resumen.estadoSat) {
        return claveEstable(*m_detalle->resumen.estadoSat);
    }
    return QVariant::fromValue(nullptr);
}

QString SolicitudDetailViewModel::estadoResumen() const
{
    return SATCFDI_RESUMEN(claveEstable(derivarEstadoResumen(m_detalle->resumen.estadoLocal,
                                                             m_detalle->resumen.estadoSat)),
                           QString());
}

QString SolicitudDetailViewModel::idSolicitudSat() const
{
    return SATCFDI_RESUMEN(texto(m_detalle->idSolicitudSat), QString());
}

QString SolicitudDetailViewModel::codEstatusSolicitud() const
{
    return SATCFDI_RESUMEN(texto(m_detalle->codEstatusSolicitud), QString());
}

QString SolicitudDetailViewModel::mensajeSolicitudSat() const
{
    return SATCFDI_RESUMEN(texto(m_detalle->mensajeSolicitudSat), QString());
}

QString SolicitudDetailViewModel::codigoEstadoSolicitud() const
{
    return SATCFDI_RESUMEN(texto(m_detalle->codigoEstadoSolicitud), QString());
}

QString SolicitudDetailViewModel::mensajeVerificacionSat() const
{
    return SATCFDI_RESUMEN(texto(m_detalle->mensajeVerificacionSat), QString());
}

QVariant SolicitudDetailViewModel::numeroCfdi() const
{
    if (m_detalle && m_detalle->numeroCfdi) {
        return QVariant::fromValue(*m_detalle->numeroCfdi);
    }
    return QVariant::fromValue(nullptr);
}

QVariant SolicitudDetailViewModel::enviadaEn() const
{
    return m_detalle ? fechaONulo(m_detalle->enviadaEn) : QVariant::fromValue(nullptr);
}

QVariant SolicitudDetailViewModel::ultimaVerificacionEn() const
{
    return m_detalle ? fechaONulo(m_detalle->ultimaVerificacionEn) : QVariant::fromValue(nullptr);
}

QString SolicitudDetailViewModel::ultimoError() const
{
    return SATCFDI_RESUMEN(texto(m_detalle->ultimoError), QString());
}

#undef SATCFDI_RESUMEN

QVariantList SolicitudDetailViewModel::paquetes() const
{
    QVariantList lista;
    if (!m_detalle) {
        return lista;
    }
    lista.reserve(m_detalle->paquetes.size());
    for (const PaqueteResumen& p : m_detalle->paquetes) {
        QVariantMap mapa;
        mapa.insert(QStringLiteral("idPaqueteSat"), p.idPaqueteSat);
        mapa.insert(QStringLiteral("estadoDescarga"), claveEstable(p.estadoDescarga));
        mapa.insert(QStringLiteral("disponibleEn"), p.disponibleEn);
        mapa.insert(QStringLiteral("descargadoEn"), fechaONulo(p.descargadoEn));
        mapa.insert(QStringLiteral("vencidoEn"), fechaONulo(p.vencidoEn));
        mapa.insert(QStringLiteral("codigoDescargaSat"), texto(p.codigoDescargaSat));
        mapa.insert(QStringLiteral("existencia"), m_existencias.value(p.idPaqueteSat));
        mapa.insert(QStringLiteral("mensaje"), catalogo::mensajePaquete(p));
        mapa.insert(QStringLiteral("maximoDescargas"), catalogo::esMaximoDescargas(p));
        // T014.2: la regla vive en el DTO (Error sin 5008); aqui solo se exige
        // que haya acciones. reintentoPendiente: intencion en pausa (D2).
        mapa.insert(QStringLiteral("puedeReintentar"), m_acciones != nullptr && p.puedeReintentar);
        mapa.insert(QStringLiteral("reintentoPendiente"), p.reintentoPendiente);
        mapa.insert(QStringLiteral("puedeMostrarFinder"),
                    m_finder != nullptr && p.estadoDescarga == EstadoDescarga::Descargado
                        && m_existencias.value(p.idPaqueteSat) == QLatin1String("Presente"));
        lista.append(mapa);
    }
    return lista;
}

int SolicitudDetailViewModel::totalPaquetes() const
{
    return m_detalle ? int(m_detalle->paquetes.size()) : 0;
}

QVariantList SolicitudDetailViewModel::logs() const
{
    QVariantList lista;
    if (!m_detalle) {
        return lista;
    }
    lista.reserve(m_detalle->logs.size());
    for (const LogResumen& l : m_detalle->logs) {
        QVariantMap mapa;
        mapa.insert(QStringLiteral("tipoEvento"), claveEstable(l.tipoEvento));
        mapa.insert(QStringLiteral("origen"), claveEstable(l.origen));
        mapa.insert(QStringLiteral("origenCodigoSat"),
                    l.origenCodigoSat ? claveEstable(*l.origenCodigoSat) : QString());
        mapa.insert(QStringLiteral("codigoSat"), texto(l.codigoSat));
        mapa.insert(QStringLiteral("mensajeSat"), texto(l.mensajeSat));
        mapa.insert(QStringLiteral("creadoEn"), l.creadoEn);
        lista.append(mapa);
    }
    return lista;
}

void SolicitudDetailViewModel::cargar(const QString& id)
{
    if (m_solicitudId != id) {
        m_solicitudId = id;
        emit solicitudIdChanged();
        setDetalle(std::nullopt);
        ++m_genEliminar; // una eliminacion de otra solicitud ya no aplica aqui
        if (!m_accionSolicitada.isEmpty()) {
            m_accionSolicitada.clear();
            emit accionSolicitadaChanged();
        }
        ++m_genFinder;
        setMensajeFinder(QString());
        setEliminacion(false, QString());
    }
    const quint64 generacion = ++m_genCarga;
    const std::optional<SolicitudId> solicitudId = SolicitudId::desdeTexto(id);
    if (!solicitudId || !m_servicio) {
        setDetalle(std::nullopt);
        setEstado(Estado::NoEncontrada);
        return;
    }
    setEstado(Estado::Cargando);
    m_servicio->obtener(*solicitudId)
        .then(this, [this, generacion](SolicitudesService::ResultadoDetalle r) {
            if (generacion != m_genCarga) {
                return; // respuesta de una seleccion o carga anterior
            }
            if (r.esExito()) {
                setDetalle(std::move(r).valor());
                setEstado(Estado::ConDatos);
                return;
            }
            setDetalle(std::nullopt);
            if (r.error().tipo == ErrorObtener::Tipo::NoEncontrada) {
                setEstado(Estado::NoEncontrada);
            } else {
                setEstado(Estado::Error, tr("No se pudo cargar la solicitud."));
            }
        });
}

void SolicitudDetailViewModel::recargar()
{
    if (!m_solicitudId.isEmpty()) {
        cargar(m_solicitudId);
    }
}

void SolicitudDetailViewModel::limpiar()
{
    ++m_genCarga;
    ++m_genEliminar;
    if (!m_solicitudId.isEmpty()) {
        m_solicitudId.clear();
        emit solicitudIdChanged();
    }
    setDetalle(std::nullopt);
    setEstado(Estado::Ninguno);
    setEliminacion(false, QString());
}

void SolicitudDetailViewModel::eliminar()
{
    const std::optional<SolicitudId> id = SolicitudId::desdeTexto(m_solicitudId);
    if (!id || !m_servicio || m_eliminando) {
        return;
    }
    const quint64 generacion = ++m_genEliminar;
    const QString texto = m_solicitudId;
    setEliminacion(true, QString());
    m_servicio->eliminar(*id).then(
        this, [this, generacion, texto](SolicitudesService::ResultadoEliminar r) {
            if (generacion != m_genEliminar) {
                return;
            }
            if (r.esExito()) {
                setEliminacion(false, QString());
                emit eliminada(texto);
                return;
            }
            setEliminacion(false, tr("No se pudo eliminar la solicitud. Intenta de nuevo."));
        });
}

void SolicitudDetailViewModel::setEstado(Estado estado, const QString& mensaje)
{
    if (m_estado == estado && m_errorMessage == mensaje) {
        return;
    }
    m_estado = estado;
    m_errorMessage = mensaje;
    emit estadoChanged();
}

void SolicitudDetailViewModel::setAccionesSolicitud(AccionesSolicitud* acciones)
{
    m_acciones = acciones;
    emit datosChanged();
}

void SolicitudDetailViewModel::setConsultaExistencia(ConsultaExistenciaPaquetes* consulta)
{
    m_existencia = consulta;
    consultarExistencias();
    emit datosChanged();
}

void SolicitudDetailViewModel::consultarExistencias()
{
    // Cada carga invalida las respuestas anteriores (token de generacion).
    const quint64 generacion = ++m_genExistencia;
    m_existencias.clear();
    if (!m_existencia || !m_detalle) {
        return;
    }
    const SolicitudId solicitud = m_detalle->resumen.id;
    for (const PaqueteResumen& p : m_detalle->paquetes) {
        if (p.estadoDescarga != EstadoDescarga::Descargado) {
            continue;
        }
        const QString id = p.idPaqueteSat;
        m_existencias.insert(id, QStringLiteral("Comprobando"));
        auto aplicar = [this, generacion, id](ExistenciaPaquete e) {
            if (generacion != m_genExistencia) {
                return;
            }
            m_existencias.insert(id, claveEstable(e));
            emit datosChanged();
        };
        m_existencia->consultar(solicitud, id)
            .then(this, aplicar)
            .onCanceled(this, [aplicar] { aplicar(ExistenciaPaquete::ErrorComprobacion); })
            .onFailed(this, [aplicar] { aplicar(ExistenciaPaquete::ErrorComprobacion); });
    }
}

void SolicitudDetailViewModel::setConsultaPreparacion(ConsultaPreparacionPerfiles* consulta)
{
    m_preparacion = consulta;
    recalcularPreparacion();
}

void SolicitudDetailViewModel::recalcularPreparacion()
{
    const quint64 generacion = ++m_genPreparacion;
    m_credencial.reset();
    if (!m_preparacion || !m_detalle || m_detalle->resumen.estadoLocal != EstadoLocal::Creada) {
        return;
    }
    const QString rfc = m_detalle->resumen.perfilRfc;
    m_preparacion->listarVerificados().then(this, [this, generacion, rfc](ConsultaPreparacionPerfiles::ResultadoLista r) {
        if (generacion != m_genPreparacion) {
            return;
        }
        m_credencial = std::pair{PreparacionPerfil::EstadoNoDisponible, true};
        if (r) {
            for (const PerfilConPreparacion& p : r.valor()) {
                if (p.perfil.rfc == rfc) {
                    m_credencial = std::pair{p.preparacion, p.perfil.activo};
                    break;
                }
            }
        }
        emit datosChanged();
    });
}

void SolicitudDetailViewModel::setAccionesFinder(AccionesFinder* acciones)
{
    m_finder = acciones;
    emit datosChanged();
}

bool SolicitudDetailViewModel::puedeAbrirCarpeta() const
{
    if (!m_finder || !m_detalle) {
        return false;
    }
    for (const PaqueteResumen& p : m_detalle->paquetes) {
        if (p.estadoDescarga == EstadoDescarga::Descargado) {
            return true;
        }
    }
    return false;
}

void SolicitudDetailViewModel::setMensajeFinder(const QString& mensaje)
{
    if (m_mensajeFinder != mensaje) {
        m_mensajeFinder = mensaje;
        emit mensajeFinderChanged();
    }
}

template <typename Futuro>
void SolicitudDetailViewModel::atenderFinder(Futuro futuro)
{
    const quint64 generacion = ++m_genFinder;
    setMensajeFinder(QString());
    futuro.then(this, [this, generacion](const ResultadoAccionFinder& r) {
        if (generacion != m_genFinder) {
            return;
        }
        setMensajeFinder(r.estado == ResultadoAccionFinder::Estado::Mostrado ? QString() : r.mensaje);
        if (r.estado == ResultadoAccionFinder::Estado::NoEncontrado) {
            // D7: refrescar la existencia (sin cambiar el estado persistido).
            consultarExistencias();
            emit datosChanged();
        }
    });
}

void SolicitudDetailViewModel::mostrarEnFinder(const QString& idPaqueteSat)
{
    if (!m_finder || !m_detalle) {
        return;
    }
    for (const PaqueteResumen& p : m_detalle->paquetes) {
        if (p.idPaqueteSat == idPaqueteSat && p.estadoDescarga == EstadoDescarga::Descargado
            && m_existencias.value(idPaqueteSat) == QLatin1String("Presente")) {
            atenderFinder(m_finder->mostrarPaquete(m_detalle->resumen.id, idPaqueteSat));
            return;
        }
    }
}

void SolicitudDetailViewModel::abrirCarpetaSolicitud()
{
    if (puedeAbrirCarpeta()) {
        atenderFinder(m_finder->abrirCarpetaSolicitud(m_detalle->resumen.id));
    }
}

bool SolicitudDetailViewModel::envioVisible() const
{
    return m_acciones && m_detalle && m_detalle->resumen.estadoLocal == EstadoLocal::Creada;
}

bool SolicitudDetailViewModel::puedeEnviar() const
{
    return envioVisible() && m_credencial && m_credencial->first == PreparacionPerfil::Lista && m_credencial->second;
}

QString SolicitudDetailViewModel::motivoEnvio() const
{
    if (!envioVisible()) {
        return {};
    }
    if (!m_credencial) {
        return catalogo::motivoCredencial(PreparacionPerfil::Verificando, true);
    }
    return catalogo::motivoCredencial(m_credencial->first, m_credencial->second);
}

QString SolicitudDetailViewModel::mensajeEstado() const
{
    return m_detalle ? catalogo::mensajeSolicitud(*m_detalle) : QString();
}

namespace {

struct ConteoPaquetes {
    int total = 0;
    int descargados = 0;
    int disponibles = 0;
    int conError = 0;
    int vencidos = 0;
};

ConteoPaquetes contar(const QList<PaqueteResumen>& paquetes)
{
    ConteoPaquetes c;
    c.total = int(paquetes.size());
    for (const PaqueteResumen& p : paquetes) {
        switch (p.estadoDescarga) {
        case EstadoDescarga::Descargado: ++c.descargados; break;
        case EstadoDescarga::Disponible:
        case EstadoDescarga::Descargando: ++c.disponibles; break;
        case EstadoDescarga::Error: ++c.conError; break;
        case EstadoDescarga::Vencido: ++c.vencidos; break;
        }
    }
    return c;
}

} // namespace

bool SolicitudDetailViewModel::todoDescargado() const
{
    if (!m_detalle || estadoResumen() != QLatin1String("Terminada")) {
        return false;
    }
    const ConteoPaquetes c = contar(m_detalle->paquetes);
    return c.total > 0 && c.descargados == c.total;
}

QString SolicitudDetailViewModel::titularResumen() const
{
    if (!m_detalle) {
        return {};
    }
    const QString e = estadoResumen();
    if (e == QLatin1String("Creada")) return tr("La solicitud aún no se envía al SAT");
    if (e == QLatin1String("Enviando")) return tr("Enviando la solicitud al SAT…");
    if (e == QLatin1String("Enviada")) return tr("El SAT recibió la solicitud");
    if (e == QLatin1String("Aceptada")) return tr("El SAT aceptó la solicitud");
    if (e == QLatin1String("EnProceso")) return tr("El SAT está preparando los paquetes");
    if (e == QLatin1String("Terminada")) {
        const ConteoPaquetes c = contar(m_detalle->paquetes);
        if (c.total == 0) return tr("El SAT terminó sin paquetes");
        if (c.descargados == c.total) return tr("Todos los paquetes están descargados");
        if (c.conError > 0) return tr("Hay un paquete con error de descarga");
        return tr("El SAT terminó la solicitud");
    }
    if (e == QLatin1String("EnvioFallido")) return tr("El SAT rechazó el envío");
    if (e == QLatin1String("EnvioIncierto")) return tr("No se sabe si el SAT registró la solicitud");
    if (e == QLatin1String("ErrorSat")) return tr("El SAT reportó un error en la solicitud");
    if (e == QLatin1String("Rechazada")) return tr("El SAT rechazó la solicitud");
    if (e == QLatin1String("Vencida")) return tr("La solicitud venció en el SAT");
    return {};
}

QString SolicitudDetailViewModel::descripcionResumen() const
{
    if (!m_detalle) {
        return {};
    }
    const QString e = estadoResumen();
    if (e == QLatin1String("Creada")) return tr("Guardada en este equipo. Se enviará con la e.firma del perfil.");
    if (e == QLatin1String("Enviando")) return tr("La app la está enviando con la e.firma del perfil.");
    if (e == QLatin1String("Enviada")) return tr("Aún no hay respuesta del SAT. La app verificará automáticamente.");
    if (e == QLatin1String("Aceptada")) return tr("La está atendiendo. La app verificará automáticamente.");
    if (e == QLatin1String("EnProceso")) return tr("La app verifica el estado periódicamente. No necesitas hacer nada.");
    if (e == QLatin1String("Terminada")) {
        const ConteoPaquetes c = contar(m_detalle->paquetes);
        if (c.total == 0) return tr("No hay paquetes que descargar para estos filtros.");
        if (c.descargados == c.total) return tr("%1 de %1 paquetes en este equipo.").arg(c.total);
        QStringList partes{tr("%1 de %2 paquetes descargados").arg(c.descargados).arg(c.total)};
        if (c.disponibles > 0) {
            partes.append(c.disponibles == 1 ? tr("1 disponible") : tr("%1 disponibles").arg(c.disponibles));
        }
        if (c.conError > 0) partes.append(tr("%1 con error").arg(c.conError));
        if (c.vencidos > 0) {
            partes.append(c.vencidos == 1 ? tr("1 vencido") : tr("%1 vencidos").arg(c.vencidos));
        }
        return partes.join(QStringLiteral(" · "));
    }
    if (e == QLatin1String("EnvioFallido")) {
        return tr("No se registró en el SAT. Si necesitas repetirla, crea una solicitud nueva.");
    }
    if (e == QLatin1String("EnvioIncierto")) return tr("No se reenviará automáticamente; revisa antes de crear otra.");
    if (e == QLatin1String("ErrorSat") || e == QLatin1String("Rechazada")) {
        const QString m = mensajeEstado();
        return m.isEmpty() ? ultimoError() : m;
    }
    if (e == QLatin1String("Vencida")) return tr("Los paquetes pueden ya no estar disponibles.");
    return {};
}

void SolicitudDetailViewModel::enviar()
{
    if (!puedeEnviar()) {
        return;
    }
    m_acciones->enviar(m_detalle->resumen.id);
    m_accionSolicitada = tr("Envío solicitado.");
    emit accionSolicitadaChanged();
}

bool SolicitudDetailViewModel::puedeVerificar() const
{
    if (!m_acciones || !m_detalle || m_detalle->resumen.estadoLocal != EstadoLocal::Enviada) {
        return false;
    }
    const auto& sat = m_detalle->resumen.estadoSat;
    return !sat || *sat == EstadoSolicitudSat::Aceptada || *sat == EstadoSolicitudSat::EnProceso;
}

bool SolicitudDetailViewModel::puedeReintentarDescarga() const
{
    if (!m_acciones || !m_detalle) {
        return false;
    }
    for (const PaqueteResumen& p : m_detalle->paquetes) {
        // T009: un paquete con 5008 (maximo de descargas) no se reintenta.
        if (p.estadoDescarga == EstadoDescarga::Disponible
            || (p.estadoDescarga == EstadoDescarga::Error && !catalogo::esMaximoDescargas(p))) {
            return true;
        }
    }
    return false;
}

void SolicitudDetailViewModel::verificarAhora()
{
    if (!puedeVerificar()) {
        return;
    }
    m_acciones->verificarAhora(m_detalle->resumen.id);
    m_accionSolicitada = tr("Verificación solicitada. Si el monitoreo está pausado, queda pendiente.");
    emit accionSolicitadaChanged();
}

void SolicitudDetailViewModel::reintentarDescarga()
{
    if (!puedeReintentarDescarga()) {
        return;
    }
    m_acciones->reintentarDescarga(m_detalle->resumen.id);
    m_accionSolicitada = tr("Descarga solicitada. Si el monitoreo está pausado, queda pendiente.");
    emit accionSolicitadaChanged();
}

void SolicitudDetailViewModel::reintentarDescargaPaquete(const QString& idPaqueteSat)
{
    if (!m_acciones || !m_detalle) {
        return;
    }
    const auto it = std::find_if(m_detalle->paquetes.cbegin(), m_detalle->paquetes.cend(),
                                 [&](const PaqueteResumen& p) { return p.idPaqueteSat == idPaqueteSat; });
    if (it == m_detalle->paquetes.cend() || !it->puedeReintentar) {
        return;
    }
    m_acciones->reintentarDescargaPaquete(m_detalle->resumen.id, idPaqueteSat);
    m_accionSolicitada = tr("Descarga del paquete solicitada. Si el monitoreo está pausado, queda pendiente.");
    emit accionSolicitadaChanged();
}

void SolicitudDetailViewModel::setDetalle(std::optional<SolicitudDetalle> detalle)
{
    if (!detalle && !m_detalle) {
        return;
    }
    m_detalle = std::move(detalle);
    consultarExistencias();
    recalcularPreparacion();
    emit datosChanged();
}

void SolicitudDetailViewModel::setEliminacion(bool eliminando, const QString& error)
{
    if (m_eliminando == eliminando && m_errorEliminacion == error) {
        return;
    }
    m_eliminando = eliminando;
    m_errorEliminacion = error;
    emit eliminacionChanged();
}

} // namespace satcfdi
