#include "PresentacionViewModels.h"
#include "application/profiles/PerfilesSatService.h"
#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/ConsultaPreparacionPerfiles.h"
#include "application/requests/SolicitudesService.h"

#include "AppViewModel.h"
#include "EFirmaFormViewModel.h"
#include "ExploradorCfdiViewModel.h"
#include "FacturasViewModel.h"
#include "HistorialViewModel.h"
#include "NuevaSolicitudViewModel.h"
#include "PerfilesSatViewModel.h"
#include "SolicitudDetailViewModel.h"
#include "SolicitudesListModel.h"

namespace satcfdi {

PresentacionViewModels::PresentacionViewModels(SolicitudesService* solicitudes,
                                               PerfilesSatService* perfiles,
                                               CredencialesSatService* credenciales,
                                               QObject* parent, RelojUtc reloj)
    : QObject(parent)
    , m_solicitudes(new SolicitudesListModel(solicitudes, this))
    , m_nuevaSolicitud(new NuevaSolicitudViewModel(solicitudes, perfiles, credenciales, this))
    , m_detalle(new SolicitudDetailViewModel(solicitudes, this))
    , m_perfiles(new PerfilesSatViewModel(perfiles, credenciales, this, reloj))
    , m_eFirma(new EFirmaFormViewModel(credenciales, this))
    , m_explorador(new ExploradorCfdiViewModel(this))
    , m_facturas(new FacturasViewModel(this))
    , m_historial(new HistorialViewModel(this))
    , m_app(new AppViewModel(m_detalle, m_perfiles, m_eFirma, this))
    , m_avisoRaizPaquetes(new AvisoRaizPaquetesViewModel(this))
{
    // Tras registrar/reemplazar (exito o fallo) se reverifica el estado real
    // del perfil: un reemplazo fallido muestra la credencial anterior.
    connect(m_eFirma, &EFirmaFormViewModel::operacionTerminada, m_perfiles,
            [this](const QString& perfilId, bool) { m_perfiles->reintentarEstado(perfilId); });
    conectarPreparacionDetalle(perfiles, credenciales, reloj);
    // T014.3 D1: los pasos del primer uso siguen a la lista, los perfiles y
    // las credenciales.
    connect(solicitudes, &SolicitudesService::listaCambiada, m_app, &AppViewModel::refrescarPrimerUso);
    connect(perfiles, &PerfilesSatService::perfilesCambiaron, m_app, &AppViewModel::refrescarPrimerUso);
    connect(credenciales, &CredencialesSatService::credencialCambio, m_app, &AppViewModel::refrescarPrimerUso);
}

void PresentacionViewModels::conectarPreparacionDetalle(PerfilesSatService* perfiles, CredencialesSatService* credenciales,
                                                        RelojUtc reloj)
{
    // T009: el detalle consulta la credencial del perfil para habilitar Enviar
    // y la reconsulta cuando cambia una credencial o la lista de perfiles.
    auto* consulta = new ConsultaPreparacionPerfiles(*perfiles, *credenciales, this, std::move(reloj));
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

void PresentacionViewModels::setAccionesFinder(AccionesFinder* acciones)
{
    m_detalle->setAccionesFinder(acciones);
    m_explorador->setAccionesFinder(acciones);
    m_facturas->visor()->setAccionesFinder(acciones);
    m_app->setAccionesFinder(acciones);
}

void PresentacionViewModels::setConsultaPrimerUso(ConsultaPrimerUso* consulta)
{
    m_app->setConsultaPrimerUso(consulta);
}

void PresentacionViewModels::setConsultaCatalogo(ConsultaCatalogoCfdi* consulta)
{
    m_explorador->setConsulta(consulta);
    m_detalle->setConsultaCatalogo(consulta);
    m_app->setExplorador(consulta ? m_explorador : nullptr);
}

void PresentacionViewModels::setPrevisualizacionCfdi(PrevisualizacionCfdi* previsualizacion)
{
    m_explorador->setPrevisualizacion(previsualizacion);
    m_facturas->setPrevisualizacion(previsualizacion);
}

void PresentacionViewModels::setFuenteFacturas(FuenteFacturasMes* fuente, PreferenciasFacturas* preferencias)
{
    m_facturas->setFuente(fuente);
    m_facturas->setPreferencias(preferencias);
    m_app->setFacturas(fuente ? m_facturas : nullptr);
    if (fuente) {
        m_app->mostrarFacturas(); // T023.4: pagina por omision
    }
}

void PresentacionViewModels::setFuenteHistorial(FuenteHistorial* fuente)
{
    m_historial->setFuente(fuente);
    m_app->setHistorial(fuente ? m_historial : nullptr);
}

void PresentacionViewModels::setResolucionDestinos(ResolucionDestinoNotificacion* resolucion)
{
    m_app->setResolucionDestinos(resolucion);
}

QVariantMap PresentacionViewModels::initialProperties() const
{
    return {
        {QStringLiteral("appViewModel"), QVariant::fromValue(m_app)},
        {QStringLiteral("solicitudesModel"), QVariant::fromValue(m_solicitudes)},
        {QStringLiteral("detalleViewModel"), QVariant::fromValue(m_detalle)},
        {QStringLiteral("perfilesViewModel"), QVariant::fromValue(m_perfiles)},
        {QStringLiteral("eFirmaViewModel"), QVariant::fromValue(m_eFirma)},
        {QStringLiteral("exploradorViewModel"), QVariant::fromValue(m_explorador)},
        {QStringLiteral("avisoRaizPaquetesViewModel"), QVariant::fromValue(m_avisoRaizPaquetes)},
        {QStringLiteral("facturasViewModel"), QVariant::fromValue(m_facturas)},
        {QStringLiteral("historialViewModel"), QVariant::fromValue(m_historial)},
    };
}

} // namespace satcfdi
