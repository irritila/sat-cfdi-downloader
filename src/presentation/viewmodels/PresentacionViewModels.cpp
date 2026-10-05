#include "PresentacionViewModels.h"
#include "application/profiles/PerfilesSatService.h"
#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/ConsultaPreparacionPerfiles.h"

#include "AppViewModel.h"
#include "EFirmaFormViewModel.h"
#include "NuevaSolicitudViewModel.h"
#include "PerfilesSatViewModel.h"
#include "SolicitudDetailViewModel.h"
#include "SolicitudesListModel.h"

namespace satcfdi {

PresentacionViewModels::PresentacionViewModels(SolicitudesService* solicitudes,
                                               PerfilesSatService* perfiles,
                                               CredencialesSatService* credenciales,
                                               QObject* parent)
    : QObject(parent)
    , m_solicitudes(new SolicitudesListModel(solicitudes, this))
    , m_nuevaSolicitud(new NuevaSolicitudViewModel(solicitudes, perfiles, credenciales, this))
    , m_detalle(new SolicitudDetailViewModel(solicitudes, this))
    , m_perfiles(new PerfilesSatViewModel(perfiles, credenciales, this))
    , m_eFirma(new EFirmaFormViewModel(credenciales, this))
    , m_app(new AppViewModel(m_nuevaSolicitud, m_detalle, m_perfiles, m_eFirma, this))
{
    // Tras registrar/reemplazar (exito o fallo) se reverifica el estado real
    // del perfil: un reemplazo fallido muestra la credencial anterior.
    connect(m_eFirma, &EFirmaFormViewModel::operacionTerminada, m_perfiles,
            [this](const QString& perfilId, bool) { m_perfiles->reintentarEstado(perfilId); });
    conectarPreparacionDetalle(perfiles, credenciales);
}

void PresentacionViewModels::conectarPreparacionDetalle(PerfilesSatService* perfiles, CredencialesSatService* credenciales)
{
    // T009: el detalle consulta la credencial del perfil para habilitar Enviar
    // y la reconsulta cuando cambia una credencial o la lista de perfiles.
    auto* consulta = new ConsultaPreparacionPerfiles(*perfiles, *credenciales, this);
    m_detalle->setConsultaPreparacion(consulta);
    connect(credenciales, &CredencialesSatService::credencialCambio, m_detalle,
            &SolicitudDetailViewModel::recalcularPreparacion);
    connect(perfiles, &PerfilesSatService::perfilesCambiaron, m_detalle,
            &SolicitudDetailViewModel::recalcularPreparacion);
}

void PresentacionViewModels::setAccionesSolicitud(AccionesSolicitud* acciones)
{
    m_nuevaSolicitud->setAccionesSolicitud(acciones);
    m_detalle->setAccionesSolicitud(acciones);
}

void PresentacionViewModels::setConsultaExistencia(ConsultaExistenciaPaquetes* consulta)
{
    m_detalle->setConsultaExistencia(consulta);
}

QVariantMap PresentacionViewModels::initialProperties() const
{
    return {
        {QStringLiteral("appViewModel"), QVariant::fromValue(m_app)},
        {QStringLiteral("solicitudesModel"), QVariant::fromValue(m_solicitudes)},
        {QStringLiteral("nuevaSolicitudViewModel"), QVariant::fromValue(m_nuevaSolicitud)},
        {QStringLiteral("detalleViewModel"), QVariant::fromValue(m_detalle)},
        {QStringLiteral("perfilesViewModel"), QVariant::fromValue(m_perfiles)},
        {QStringLiteral("eFirmaViewModel"), QVariant::fromValue(m_eFirma)},
    };
}

} // namespace satcfdi
