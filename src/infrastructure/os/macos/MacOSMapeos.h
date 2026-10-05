#pragma once

// Mapeos puros del adaptador MacOSIntegration (T004, corte C). C++ portable,
// sin AppKit/ServiceManagement/UserNotifications: reciben los valores
// numericos crudos del SDK (las equivalencias se verifican con static_assert
// en MacOSIntegration.mm) y devuelven el vocabulario de OSIntegration. Se
// separan para probarlos sin permisos, bundle firmado ni sesion grafica.

#include "ports/OSIntegration.h"

#include <QString>

#include <cstdint>

namespace satcfdi::macos {

// Valores de SMAppServiceStatus (ServiceManagement, macOS 13+).
inline constexpr long kSmStatusNotRegistered = 0;
inline constexpr long kSmStatusEnabled = 1;
inline constexpr long kSmStatusRequiresApproval = 2;
inline constexpr long kSmStatusNotFound = 3;

// Valores de UNAuthorizationStatus (UserNotifications).
inline constexpr long kUnAuthNotDetermined = 0;
inline constexpr long kUnAuthDenied = 1;
inline constexpr long kUnAuthAuthorized = 2;
inline constexpr long kUnAuthProvisional = 3;
inline constexpr long kUnAuthEphemeral = 4; // solo iOS; se acepta por robustez

// Codigos de cuatro caracteres del Apple Event de apertura.
inline constexpr std::uint32_t kAeClaseCore = 0x61657674;          // 'aevt' kCoreEventClass
inline constexpr std::uint32_t kAeAbrirAplicacion = 0x6F617070;    // 'oapp' kAEOpenApplication
inline constexpr std::uint32_t kAeLanzadoComoLoginItem = 0x6C676974; // 'lgit' keyAELaunchedAsLogInItem

// Clasificacion de un NSError devuelto por SMAppService register/unregister.
// La clasificacion por dominio/codigo vive en el .mm; aqui solo la semantica.
enum class ErrorLoginItem {
    Ninguno,              // La llamada tuvo exito.
    YaEnEstadoSolicitado, // kSMErrorAlreadyRegistered / kSMErrorJobNotFound al dar de baja.
    RechazadoPorUsuario,  // kSMErrorLaunchDeniedByUser, kSMErrorAuthorizationFailure, EPERM.
    NoDisponible,         // Firma invalida, servicio no disponible, plist/bundle invalido.
    Otro,                 // Cualquier otro error.
};

// SMAppServiceStatus -> LoginItemStatus. notFound/notRegistered -> Disabled;
// valores desconocidos -> Unavailable.
OSIntegration::LoginItemStatus mapearEstadoLoginItem(long smStatus);

// Estado efectivo tras configurarLoginItem(habilitar). `smStatus` es el
// status leido despues de la llamada (con o sin error).
// - RechazadoPorUsuario -> Rejected; NoDisponible -> Unavailable.
// - Ninguno / YaEnEstadoSolicitado -> mapearEstadoLoginItem(smStatus).
// - Otro: si se pidio habilitar y el SO no quedo Enabled/RequiresApproval,
//   el SO rechazo la peticion -> Rejected; en otro caso, el status leido.
OSIntegration::LoginItemStatus mapearResultadoLoginItem(bool habilitar, ErrorLoginItem error, long smStatus);

// UNAuthorizationStatus -> NotificationStatus. Provisional y ephemeral
// permiten entregar notificaciones -> Granted; desconocido -> Unavailable.
OSIntegration::NotificationStatus mapearAutorizacionNotificaciones(long unStatus);

// Resultado de enviarNotificacionPrueba() derivado del estado vigente cuando
// no se llega a pedir la entrega al SO (Granted devuelve Sent: el .mm solo lo
// usa para decidir si entrega).
OSIntegration::NotificationSendResult resultadoEnvioSegunEstado(OSIntegration::NotificationStatus estado);

// LaunchContext a partir del Apple Event vigente durante
// NSApplicationDidFinishLaunchingNotification. LoginItem solo si es
// 'aevt'/'oapp' con keyAEPropData == 'lgit'; cualquier otro caso -> Manual.
OSIntegration::LaunchContext launchContextDesdeEventoApertura(bool hayEvento,
                                                              std::uint32_t claseEvento,
                                                              std::uint32_t idEvento,
                                                              std::uint32_t valorPropData);

// Textos visibles del menu bar (sin acentos, como el resto de la UI; ajustar
// a la guia visual queda como pendiente no bloqueante de T004).
QString textoEstadoLoginItem(OSIntegration::LoginItemStatus estado);
QString textoEstadoNotificaciones(OSIntegration::NotificationStatus estado);
QString textoAccionMonitoreo(bool pausado);
// T007 D2: linea de estado del worker y de pendientes (vacia si 0).
QString textoEstadoMonitoreo(const OSIntegration::EstadoMonitoreo& estado);
QString textoPendientes(int pendientes);

} // namespace satcfdi::macos
