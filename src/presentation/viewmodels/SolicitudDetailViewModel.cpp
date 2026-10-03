#include "SolicitudDetailViewModel.h"

#include "application/requests/SolicitudesService.h"
#include "domain/solicitudes/EstadoResumen.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QFuture>
#include <QVariantMap>

#include <utility>

namespace satcfdi {

SolicitudDetailViewModel::SolicitudDetailViewModel(SolicitudesService* servicio, QObject* parent)
    : QObject(parent)
    , m_servicio(servicio)
{
    Q_ASSERT(servicio != nullptr);
    connect(servicio, &SolicitudesService::solicitudActualizada, this,
            [this](const SolicitudId& id) {
                if (!m_solicitudId.isEmpty() && id.texto() == m_solicitudId) {
                    cargar(m_solicitudId);
                }
            });
}

QString SolicitudDetailViewModel::perfilRfc() const
{
    return m_detalle ? m_detalle->resumen.perfilRfc : QString();
}

QString SolicitudDetailViewModel::rfcContraparte() const
{
    return m_detalle ? m_detalle->resumen.rfcContraparte.value_or(QString()) : QString();
}

QString SolicitudDetailViewModel::tipoDescarga() const
{
    return m_detalle ? claveEstable(m_detalle->resumen.tipoDescarga) : QString();
}

QString SolicitudDetailViewModel::fechaInicial() const
{
    return m_detalle ? m_detalle->resumen.fechaInicial.toString(Qt::ISODate) : QString();
}

QString SolicitudDetailViewModel::fechaFinal() const
{
    return m_detalle ? m_detalle->resumen.fechaFinal.toString(Qt::ISODate) : QString();
}

QDateTime SolicitudDetailViewModel::creadaEn() const
{
    return m_detalle ? m_detalle->resumen.creadaEn : QDateTime();
}

QString SolicitudDetailViewModel::estadoLocal() const
{
    return m_detalle ? claveEstable(m_detalle->resumen.estadoLocal) : QString();
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
    if (!m_detalle) {
        return {};
    }
    return claveEstable(
        derivarEstadoResumen(m_detalle->resumen.estadoLocal, m_detalle->resumen.estadoSat));
}

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
        mapa.insert(QStringLiteral("descargadoEn"),
                    p.descargadoEn ? QVariant(*p.descargadoEn) : QVariant::fromValue(nullptr));
        lista.append(mapa);
    }
    return lista;
}

int SolicitudDetailViewModel::totalPaquetes() const
{
    return m_detalle ? int(m_detalle->paquetes.size()) : 0;
}

void SolicitudDetailViewModel::cargar(const QString& id)
{
    if (m_solicitudId != id) {
        m_solicitudId = id;
        emit solicitudIdChanged();
        setDetalle(std::nullopt);
    }
    const quint64 generacion = ++m_generacion;
    const std::optional<SolicitudId> solicitudId = SolicitudId::desdeTexto(id);
    if (!solicitudId || !m_servicio) {
        setCargando(false);
        setErrorMessage(tr("La solicitud seleccionada no es valida."));
        return;
    }
    setCargando(true);
    m_servicio->obtener(*solicitudId)
        .then(this, [this, generacion](SolicitudesService::ResultadoDetalle r) {
            if (generacion != m_generacion) {
                return; // respuesta de una seleccion anterior
            }
            setCargando(false);
            if (r.esExito()) {
                setErrorMessage(QString());
                setDetalle(std::move(r).valor());
            } else {
                setDetalle(std::nullopt);
                setErrorMessage(r.error().tipo == ErrorObtener::Tipo::NoEncontrada
                                    ? tr("La solicitud ya no existe.")
                                    : tr("No se pudo cargar la solicitud."));
            }
        });
}

void SolicitudDetailViewModel::limpiar()
{
    ++m_generacion;
    if (!m_solicitudId.isEmpty()) {
        m_solicitudId.clear();
        emit solicitudIdChanged();
    }
    setCargando(false);
    setErrorMessage(QString());
    setDetalle(std::nullopt);
}

void SolicitudDetailViewModel::setCargando(bool valor)
{
    if (m_cargando != valor) {
        m_cargando = valor;
        emit cargandoChanged();
    }
}

void SolicitudDetailViewModel::setErrorMessage(const QString& mensaje)
{
    if (m_errorMessage != mensaje) {
        m_errorMessage = mensaje;
        emit errorMessageChanged();
    }
}

void SolicitudDetailViewModel::setDetalle(std::optional<SolicitudDetalle> detalle)
{
    if (!detalle && !m_detalle) {
        return;
    }
    m_detalle = std::move(detalle);
    emit datosChanged();
}

} // namespace satcfdi
