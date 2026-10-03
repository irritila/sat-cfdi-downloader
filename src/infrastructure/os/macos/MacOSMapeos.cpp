#include "infrastructure/os/macos/MacOSMapeos.h"

namespace satcfdi::macos {

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
        return QStringLiteral("Estado en macOS: deshabilitado");
    case LoginItemStatus::Enabled:
        return QStringLiteral("Estado en macOS: habilitado");
    case LoginItemStatus::RequiresApproval:
        return QStringLiteral("Estado en macOS: pendiente de aprobacion");
    case LoginItemStatus::Rejected:
        return QStringLiteral("Estado en macOS: rechazado");
    case LoginItemStatus::Unavailable:
        return QStringLiteral("Estado en macOS: no disponible");
    }
    return QStringLiteral("Estado en macOS: no disponible");
}

QString textoEstadoNotificaciones(NotificationStatus estado)
{
    switch (estado) {
    case NotificationStatus::NotDetermined:
        return QStringLiteral("Notificaciones: sin permiso solicitado");
    case NotificationStatus::Granted:
        return QStringLiteral("Notificaciones: permitidas");
    case NotificationStatus::Denied:
        return QStringLiteral("Notificaciones: deshabilitadas");
    case NotificationStatus::Unavailable:
        return QStringLiteral("Notificaciones: no disponibles");
    }
    return QStringLiteral("Notificaciones: no disponibles");
}

QString textoAccionMonitoreo(bool pausado)
{
    return pausado ? QStringLiteral("Reanudar monitoreo") : QStringLiteral("Pausar monitoreo");
}

} // namespace satcfdi::macos
