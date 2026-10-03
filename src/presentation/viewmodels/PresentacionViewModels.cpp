#include "PresentacionViewModels.h"

#include "AppViewModel.h"
#include "NuevaSolicitudViewModel.h"
#include "SolicitudDetailViewModel.h"
#include "SolicitudesListModel.h"

namespace satcfdi {

PresentacionViewModels::PresentacionViewModels(SolicitudesService* solicitudes,
                                               PerfilesSatService* perfiles,
                                               QObject* parent)
    : QObject(parent)
    , m_solicitudes(new SolicitudesListModel(solicitudes, this))
    , m_nuevaSolicitud(new NuevaSolicitudViewModel(solicitudes, perfiles, this))
    , m_detalle(new SolicitudDetailViewModel(solicitudes, this))
    , m_app(new AppViewModel(m_nuevaSolicitud, m_detalle, this))
{
}

QVariantMap PresentacionViewModels::initialProperties() const
{
    return {
        {QStringLiteral("appViewModel"), QVariant::fromValue(m_app)},
        {QStringLiteral("solicitudesModel"), QVariant::fromValue(m_solicitudes)},
        {QStringLiteral("nuevaSolicitudViewModel"), QVariant::fromValue(m_nuevaSolicitud)},
        {QStringLiteral("detalleViewModel"), QVariant::fromValue(m_detalle)},
    };
}

} // namespace satcfdi
