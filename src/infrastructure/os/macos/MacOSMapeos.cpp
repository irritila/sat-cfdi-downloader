#include "infrastructure/os/macos/MacOSMapeos.h"

#include <QCoreApplication>
#include <QLatin1StringView>

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

// --- T014.4 ----------------------------------------------------------------

using Destino = OSIntegration::DestinoNotificacion;
using AccionNotificacion = OSIntegration::AccionNotificacion;

namespace {
constexpr QLatin1StringView kTipoSolicitud("solicitud");
constexpr QLatin1StringView kTipoPerfil("perfil");
} // namespace

bool esUuidCanonico(const QString& id)
{
    if (id.size() != 36) {
        return false;
    }
    for (qsizetype i = 0; i < id.size(); ++i) {
        const QChar c = id.at(i);
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != QLatin1Char('-')) {
                return false;
            }
        } else if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                     || (c >= QLatin1Char('a') && c <= QLatin1Char('f')))) {
            return false;
        }
    }
    return true;
}

QString claveTipoDestino(Destino::Tipo tipo)
{
    switch (tipo) {
    case Destino::Tipo::Solicitud:
        return kTipoSolicitud;
    case Destino::Tipo::Perfil:
        return kTipoPerfil;
    case Destino::Tipo::Ninguno:
        break;
    }
    return {};
}

bool destinoTransportable(const Destino& destino)
{
    return destino.tipo != Destino::Tipo::Ninguno && esUuidCanonico(destino.id);
}

Destino destinoDesdeUserInfo(const QString& tipo, const QString& id)
{
    if (!esUuidCanonico(id)) {
        return {};
    }
    if (tipo == kTipoSolicitud) {
        return Destino{Destino::Tipo::Solicitud, id};
    }
    if (tipo == kTipoPerfil) {
        return Destino{Destino::Tipo::Perfil, id};
    }
    return {};
}

QString categoriaNotificacion(const Destino& destino, const QList<AccionNotificacion>& acciones)
{
    if (!destinoTransportable(destino)) {
        return {};
    }
    if (destino.tipo == Destino::Tipo::Solicitud && acciones.contains(AccionNotificacion::MostrarEnFinder)) {
        return QString::fromLatin1(kCategoriaMostrarEnFinder);
    }
    if (destino.tipo == Destino::Tipo::Perfil && acciones.contains(AccionNotificacion::AbrirPerfiles)) {
        return QString::fromLatin1(kCategoriaAbrirPerfiles);
    }
    return {};
}

QString textoAccionNotificacion(AccionNotificacion accion)
{
    switch (accion) {
    case AccionNotificacion::MostrarEnFinder:
        return tr("Mostrar en Finder");
    case AccionNotificacion::AbrirPerfiles:
        return tr("Abrir Perfiles SAT");
    case AccionNotificacion::Abrir:
        break;
    }
    return {};
}

std::optional<ActivacionNotificacion> activacionDesdeRespuesta(const QString& tipoDestino,
                                                                const QString& idDestino,
                                                                bool esAccionPorDefecto,
                                                                const QString& identificadorAccion)
{
    AccionNotificacion accion = AccionNotificacion::Abrir;
    if (esAccionPorDefecto) {
        accion = AccionNotificacion::Abrir;
    } else if (identificadorAccion == QLatin1StringView(kAccionMostrarEnFinder)) {
        accion = AccionNotificacion::MostrarEnFinder;
    } else if (identificadorAccion == QLatin1StringView(kAccionAbrirPerfiles)) {
        accion = AccionNotificacion::AbrirPerfiles;
    } else {
        return std::nullopt;
    }

    const Destino destino = destinoDesdeUserInfo(tipoDestino, idDestino);
    if (destino.tipo == Destino::Tipo::Ninguno) {
        return ActivacionNotificacion{Destino{}, AccionNotificacion::Abrir};
    }
    if ((accion == AccionNotificacion::MostrarEnFinder && destino.tipo != Destino::Tipo::Solicitud)
        || (accion == AccionNotificacion::AbrirPerfiles && destino.tipo != Destino::Tipo::Perfil)) {
        accion = AccionNotificacion::Abrir;
    }
    return ActivacionNotificacion{destino, accion};
}

QString textoEstadoIcono(OSIntegration::EstadoIcono estado)
{
    using E = OSIntegration::EstadoIcono;
    switch (estado) {
    case E::Trabajando:
        return tr("SAT CFDI Downloader: trabajando");
    case E::Pausado:
        return tr("SAT CFDI Downloader: en pausa");
    case E::Atencion:
        return tr("SAT CFDI Downloader: requiere atención");
    case E::Normal:
        break;
    }
    return tr("SAT CFDI Downloader");
}

} // namespace satcfdi::macos
