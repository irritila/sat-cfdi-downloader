#include "SolicitudesListModel.h"

#include "application/requests/SolicitudesService.h"
#include "domain/solicitudes/EstadoResumen.h"

#include <QFuture>

#include <utility>

namespace satcfdi {

SolicitudesListModel::SolicitudesListModel(SolicitudesService* servicio, QObject* parent)
    : QAbstractListModel(parent)
    , m_servicio(servicio)
{
    Q_ASSERT(servicio != nullptr);
    connect(servicio, &SolicitudesService::listaCambiada, this, &SolicitudesListModel::refrescar);
    connect(servicio, &SolicitudesService::solicitudActualizada, this,
            [this](const SolicitudId&) { refrescar(); });
    refrescar();
}

int SolicitudesListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_filas.size());
}

QVariant SolicitudesListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const SolicitudResumen& s = m_filas.at(index.row());
    switch (role) {
    case IdRole:
        return s.id.texto();
    case PerfilRfcRole:
        return s.perfilRfc;
    case RfcContraparteRole:
        return s.rfcContraparte.value_or(QString());
    case TipoDescargaRole:
        return claveEstable(s.tipoDescarga);
    case FechaInicialRole:
        return s.fechaInicial.toString(Qt::ISODate);
    case FechaFinalRole:
        return s.fechaFinal.toString(Qt::ISODate);
    case EstadoLocalRole:
        return claveEstable(s.estadoLocal);
    case EstadoSatRole:
        // Nulo explicito (no undefined) cuando SAT aun no devolvio estado.
        return s.estadoSat ? QVariant(claveEstable(*s.estadoSat)) : QVariant::fromValue(nullptr);
    case EstadoResumenRole:
        return claveEstable(derivarEstadoResumen(s.estadoLocal, s.estadoSat));
    case CreadaEnRole:
        return s.creadaEn;
    case TotalPaquetesRole:
        return s.totalPaquetes;
    default:
        return {};
    }
}

QHash<int, QByteArray> SolicitudesListModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {PerfilRfcRole, "perfilRfc"},
        {RfcContraparteRole, "rfcContraparte"},
        {TipoDescargaRole, "tipoDescarga"},
        {FechaInicialRole, "fechaInicial"},
        {FechaFinalRole, "fechaFinal"},
        {EstadoLocalRole, "estadoLocal"},
        {EstadoSatRole, "estadoSat"},
        {EstadoResumenRole, "estadoResumen"},
        {CreadaEnRole, "creadaEn"},
        {TotalPaquetesRole, "totalPaquetes"},
    };
}

int SolicitudesListModel::filaDe(const QString& id) const
{
    for (qsizetype i = 0; i < m_filas.size(); ++i) {
        if (m_filas.at(i).id.texto() == id) {
            return int(i);
        }
    }
    return -1;
}

QString SolicitudesListModel::idEn(int fila) const
{
    return (fila >= 0 && fila < m_filas.size()) ? m_filas.at(fila).id.texto() : QString();
}

SolicitudesListModel::Estado SolicitudesListModel::estado() const
{
    if (!m_errorMessage.isEmpty()) {
        return m_cargando ? Estado::Cargando : Estado::Error;
    }
    if (!m_cargado) {
        return Estado::Cargando;
    }
    return m_filas.isEmpty() ? Estado::Vacia : Estado::ConDatos;
}

template <typename F>
void SolicitudesListModel::notificar(F&& cambio)
{
    const Estado estadoPrevio = estado();
    const int conteoPrevio = count();
    const bool vacioPrevio = vacio();
    std::forward<F>(cambio)();
    if (conteoPrevio != count()) {
        emit countChanged();
    }
    if (vacioPrevio != vacio()) {
        emit vacioChanged();
    }
    if (estadoPrevio != estado()) {
        emit estadoChanged();
    }
}

void SolicitudesListModel::refrescar()
{
    if (!m_servicio) {
        return;
    }
    const quint64 generacion = ++m_generacion;
    notificar([&] { setCargando(true); });
    m_servicio->listar().then(this, [this, generacion](SolicitudesService::ResultadoLista r) {
        if (generacion != m_generacion) {
            return; // llego (o llegara) una respuesta mas reciente
        }
        notificar([&] {
            if (r.esExito()) {
                beginResetModel();
                m_filas = std::move(r).valor();
                m_cargado = true;
                endResetModel();
                setErrorMessage(QString());
            } else {
                setErrorMessage(tr("No se pudo cargar la lista de solicitudes."));
            }
            setCargando(false);
        });
    });
}

void SolicitudesListModel::setCargando(bool valor)
{
    if (m_cargando != valor) {
        m_cargando = valor;
        emit cargandoChanged();
    }
}

void SolicitudesListModel::setErrorMessage(const QString& mensaje)
{
    if (m_errorMessage != mensaje) {
        m_errorMessage = mensaje;
        emit errorMessageChanged();
    }
}

} // namespace satcfdi
