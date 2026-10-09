#include "infrastructure/os/macos/MacOSMapeos.h"

#include <QCoreApplication>

namespace satcfdi::macos {

namespace {
// Textos visibles del menu bar (T013 D6): UTF-8 con ortografia completa y
// traducibles en el contexto "MenuBar".
QString tr(const char* texto)
{
    return QCoreApplication::translate("MenuBar", texto);
}
} // namespace

using LoginItemStatus = OSIntegration::LoginItemStatus;
using NotificationStatus = OSIntegration::NotificationStatus;
using NotificationSendResult = OSIntegration::NotificationSendResult;
using LaunchContext = OSIntegration::LaunchContext;

LoginItemStatus mapearEstadoLoginItem(long smStatus)
{
    switch (smStatus) {
    case kSmStatusNotRegistered:
    case kSmStatusNotFound:
        return LoginItemStatus::Disabled;
    case kSmStatusEnabled:
        return LoginItemStatus::Enabled;
    case kSmStatusRequiresApproval:
        return LoginItemStatus::RequiresApproval;
    default:
        return LoginItemStatus::Unavailable;
    }
}

LoginItemStatus mapearResultadoLoginItem(bool habilitar, ErrorLoginItem error, long smStatus)
{
    switch (error) {
    case ErrorLoginItem::RechazadoPorUsuario:
        return LoginItemStatus::Rejected;
    case ErrorLoginItem::NoDisponible:
        return LoginItemStatus::Unavailable;
    case ErrorLoginItem::Ninguno:
    case ErrorLoginItem::YaEnEstadoSolicitado:
        return mapearEstadoLoginItem(smStatus);
    case ErrorLoginItem::Otro:
        break;
    }
    const LoginItemStatus leido = mapearEstadoLoginItem(smStatus);
    if (habilitar && leido != LoginItemStatus::Enabled && leido != LoginItemStatus::RequiresApproval) {
        return LoginItemStatus::Rejected;
    }
    return leido;
}

NotificationStatus mapearAutorizacionNotificaciones(long unStatus)
{
    switch (unStatus) {
    case kUnAuthNotDetermined:
        return NotificationStatus::NotDetermined;
    case kUnAuthDenied:
        return NotificationStatus::Denied;
    case kUnAuthAuthorized:
    case kUnAuthProvisional:
    case kUnAuthEphemeral:
        return NotificationStatus::Granted;
    default:
        return NotificationStatus::Unavailable;
    }
}

NotificationSendResult resultadoEnvioSegunEstado(NotificationStatus estado)
{
    switch (estado) {
    case NotificationStatus::Granted:
        return NotificationSendResult::Sent;
    case NotificationStatus::Denied:
        return NotificationSendResult::PermissionDenied;
    case NotificationStatus::NotDetermined:
        return NotificationSendResult::PermissionNotDetermined;
    case NotificationStatus::Unavailable:
        return NotificationSendResult::Unavailable;
    }
    return NotificationSendResult::Unavailable;
}

LaunchContext launchContextDesdeEventoApertura(bool hayEvento,
                                               std::uint32_t claseEvento,
                                               std::uint32_t idEvento,
                                               std::uint32_t valorPropData)
{
    if (hayEvento && claseEvento == kAeClaseCore && idEvento == kAeAbrirAplicacion
        && valorPropData == kAeLanzadoComoLoginItem) {
        return LaunchContext::LoginItem;
    }
    return LaunchContext::Manual;
}

QString textoEstadoLoginItem(LoginItemStatus estado)
{
    switch (estado) {
    case LoginItemStatus::Disabled:
        return tr("Estado en macOS: deshabilitado");
    case LoginItemStatus::Enabled:
        return tr("Estado en macOS: habilitado");
    case LoginItemStatus::RequiresApproval:
        return tr("Estado en macOS: pendiente de aprobación");
    case LoginItemStatus::Rejected:
        return tr("Estado en macOS: rechazado");
    case LoginItemStatus::Unavailable:
        return tr("Estado en macOS: no disponible");
    }
    return tr("Estado en macOS: no disponible");
}

QString textoEstadoNotificaciones(NotificationStatus estado)
{
    switch (estado) {
    case NotificationStatus::NotDetermined:
        return tr("Notificaciones: sin permiso solicitado");
    case NotificationStatus::Granted:
        return tr("Notificaciones: permitidas");
    case NotificationStatus::Denied:
        return tr("Notificaciones: deshabilitadas");
    case NotificationStatus::Unavailable:
        return tr("Notificaciones: no disponibles");
    }
    return tr("Notificaciones: no disponibles");
}

QString textoAccionMonitoreo(bool pausado)
{
    return pausado ? tr("Reanudar monitoreo") : tr("Pausar monitoreo");
}

QString textoEstadoMonitoreo(const OSIntegration::EstadoMonitoreo& estado)
{
    using Fase = OSIntegration::EstadoMonitoreo::Fase;
    using Actividad = OSIntegration::EstadoMonitoreo::Actividad;
    switch (estado.fase) {
    case Fase::Pausado:
        return tr("Monitoreo en pausa");
    case Fase::ActivoEnEspera:
        return tr("Monitoreo activo");
    case Fase::Ejecutando:
        switch (estado.actividad) {
        case Actividad::Enviando:
            return tr("Trabajando: enviando…");
        case Actividad::Verificando:
            return tr("Trabajando: verificando…");
        case Actividad::Descargando:
            return tr("Trabajando: descargando…");
        case Actividad::Ninguna:
        case Actividad::Otra:
            break;
        }
        return tr("Trabajando…");
    case Fase::Deteniendo:
        return tr("Deteniendo monitoreo…");
    case Fase::Detenido:
        break;
    }
    return tr("Monitoreo detenido");
}

QString textoPendientes(int pendientes)
{
    return pendientes > 0 ? tr("Pendientes: %1").arg(pendientes) : QString();
}

} // namespace satcfdi::macos
