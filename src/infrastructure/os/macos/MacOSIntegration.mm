// MacOSIntegration: adaptador nativo de OSIntegration (T004, corte C).
// Objective-C++ con ARC. Ver contrato en ports/OSIntegration.h y requisitos
// de orden en MacOSIntegration.h.

#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>
#import <UserNotifications/UserNotifications.h>

#include "infrastructure/os/macos/MacOSIntegration.h"
#include "infrastructure/os/macos/MacOSMapeos.h"

#include <QAction>
#include <QUuid>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QIcon>
#include <QLoggingCategory>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWindow>

#include <cerrno>
#include <utility>

#if !__has_feature(objc_arc)
#error "MacOSIntegration.mm requiere ARC (-fobjc-arc)"
#endif

namespace mapeos = satcfdi::macos;

// Los mapeos puros usan los valores numericos del SDK; si Apple los cambia,
// la compilacion falla aqui en lugar de mapear mal en runtime.
static_assert(SMAppServiceStatusNotRegistered == mapeos::kSmStatusNotRegistered);
static_assert(SMAppServiceStatusEnabled == mapeos::kSmStatusEnabled);
static_assert(SMAppServiceStatusRequiresApproval == mapeos::kSmStatusRequiresApproval);
static_assert(SMAppServiceStatusNotFound == mapeos::kSmStatusNotFound);
static_assert(UNAuthorizationStatusNotDetermined == mapeos::kUnAuthNotDetermined);
static_assert(UNAuthorizationStatusDenied == mapeos::kUnAuthDenied);
static_assert(UNAuthorizationStatusAuthorized == mapeos::kUnAuthAuthorized);
static_assert(UNAuthorizationStatusProvisional == mapeos::kUnAuthProvisional);
// UNAuthorizationStatusEphemeral (4) no existe en macOS; el mapeo lo acepta igual.
static_assert(kCoreEventClass == mapeos::kAeClaseCore);
static_assert(kAEOpenApplication == mapeos::kAeAbrirAplicacion);
static_assert(keyAELaunchedAsLogInItem == mapeos::kAeLanzadoComoLoginItem);

Q_LOGGING_CATEGORY(lcMacOS, "satcfdi.os.macos")

// --- Objetos Objective-C auxiliares --------------------------------------

// Recibe kAEReopenApplication y actua como delegado de
// UNUserNotificationCenter (presentar notificaciones con la app al frente).
@interface SatCfdiManejadorNativo : NSObject <UNUserNotificationCenterDelegate>
@property (nonatomic, copy) void (^alReabrir)(void);
- (void)manejarReapertura:(NSAppleEventDescriptor*)evento respuesta:(NSAppleEventDescriptor*)respuesta;
@end

@implementation SatCfdiManejadorNativo

- (void)manejarReapertura:(NSAppleEventDescriptor*)evento respuesta:(NSAppleEventDescriptor*)respuesta
{
    (void)evento;
    (void)respuesta;
    // Los Apple Events se despachan en el hilo principal (hilo grafico).
    if (self.alReabrir) {
        self.alReabrir();
    }
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
{
    (void)center;
    (void)notification;
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList
                      | UNNotificationPresentationOptionSound);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    didReceiveNotificationResponse:(UNNotificationResponse*)response
             withCompletionHandler:(void (^)(void))completionHandler
{
    (void)center;
    (void)response;
    completionHandler();
}

@end

namespace satcfdi {

namespace {

using LaunchContext = OSIntegration::LaunchContext;
using LoginItemStatus = OSIntegration::LoginItemStatus;
using NotificationStatus = OSIntegration::NotificationStatus;
using NotificationSendResult = OSIntegration::NotificationSendResult;

constexpr int kEsperaMaximaArranqueMs = 3000;
constexpr int kEsperaMaximaPermisoInicialMs = 1000;

// Ejecuta `funcion` en el hilo grafico. Seguro desde hilos de
// UserNotifications/ServiceManagement: el contexto es qApp (vive mas que el
// adaptador) y el adaptador se valida con QPointer ya en el hilo grafico.
template <typename Funcion>
void enHiloGrafico(QPointer<MacOSIntegration> guardia, Funcion funcion)
{
    QCoreApplication* app = QCoreApplication::instance();
    if (app == nullptr) {
        return;
    }
    QMetaObject::invokeMethod(
        app,
        [guardia, funcion = std::move(funcion)]() mutable {
            if (MacOSIntegration* adaptador = guardia.data()) {
                funcion(adaptador);
            }
        },
        Qt::QueuedConnection);
}

bool hayBundleIdentificado()
{
    NSString* identificador = [NSBundle mainBundle].bundleIdentifier;
    return identificador != nil && identificador.length > 0;
}

// UNUserNotificationCenter lanza NSInternalInconsistencyException sin bundle;
// se valida antes y se protege con @try por robustez.
UNUserNotificationCenter* centroNotificaciones(bool hayBundle)
{
    if (!hayBundle) {
        return nil;
    }
    @try {
        return [UNUserNotificationCenter currentNotificationCenter];
    } @catch (NSException* excepcion) {
        qCWarning(lcMacOS) << "UNUserNotificationCenter no disponible:"
                           << QString::fromNSString(excepcion.reason != nil ? excepcion.reason : @"");
        return nil;
    }
}

// Lee el Apple Event de apertura. AppKit entrega 'oapp' (o 'odoc'/'GURL')
// durante finishLaunching, que con Qt ocurre en el primer procesamiento de
// eventos; NSApplicationDidFinishLaunchingNotification se publica mientras
// ese evento es el currentAppleEvent. Se fuerza ese primer ciclo aqui, de
// forma acotada, para que launchContext() sea valido desde la construccion.
LaunchContext determinarLaunchContext()
{
    if (NSApp == nil) {
        qCWarning(lcMacOS) << "NSApp no existe; LaunchContext::Manual";
        return LaunchContext::Manual;
    }
    if (NSRunningApplication.currentApplication.finishedLaunching) {
        qCWarning(lcMacOS) << "AppKit ya termino de arrancar antes de crear MacOSIntegration;"
                              " no se puede leer el Apple Event de apertura. LaunchContext::Manual";
        return LaunchContext::Manual;
    }

    __block bool visto = false;
    __block LaunchContext contexto = LaunchContext::Manual;
    id observador = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSApplicationDidFinishLaunchingNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification*) {
                    NSAppleEventDescriptor* evento = [[NSAppleEventManager sharedAppleEventManager] currentAppleEvent];
                    NSAppleEventDescriptor* prop = [evento paramDescriptorForKeyword:keyAEPropData];
                    contexto = mapeos::launchContextDesdeEventoApertura(
                        evento != nil, evento != nil ? evento.eventClass : 0, evento != nil ? evento.eventID : 0,
                        prop != nil ? prop.enumCodeValue : 0);
                    visto = true;
                }];

    // Temporizador solo para despertar WaitForMoreEvents si no llega nada.
    QTimer despertador;
    despertador.start(10);
    QElapsedTimer reloj;
    reloj.start();
    while (!visto && reloj.elapsed() < kEsperaMaximaArranqueMs) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents);
    }
    despertador.stop();
    [[NSNotificationCenter defaultCenter] removeObserver:observador];

    if (!visto) {
        qCWarning(lcMacOS) << "No llego NSApplicationDidFinishLaunching en" << kEsperaMaximaArranqueMs
                           << "ms; LaunchContext::Manual";
    }
    return contexto;
}

LoginItemStatus leerEstadoLoginItem(bool hayBundle)
{
    if (!hayBundle) {
        return LoginItemStatus::Unavailable;
    }
    if (@available(macOS 13.0, *)) {
        return mapeos::mapearEstadoLoginItem(static_cast<long>(SMAppService.mainAppService.status));
    }
    return LoginItemStatus::Unavailable;
}

// Clasifica errores de SMAppService register/unregister. Los codigos
// kSMError* solo se interpretan fuera de los dominios POSIX/OSStatus (donde
// los mismos numeros significan otra cosa); ahi solo EPERM es rechazo.
mapeos::ErrorLoginItem clasificarErrorLoginItem(NSError* error, bool habilitar)
{
    if (error == nil) {
        return mapeos::ErrorLoginItem::Otro;
    }
    const NSInteger codigo = error.code;
    if ([error.domain isEqualToString:NSPOSIXErrorDomain] || [error.domain isEqualToString:NSOSStatusErrorDomain]) {
        return codigo == EPERM ? mapeos::ErrorLoginItem::RechazadoPorUsuario : mapeos::ErrorLoginItem::Otro;
    }
    switch (codigo) {
    case kSMErrorAlreadyRegistered:
        return habilitar ? mapeos::ErrorLoginItem::YaEnEstadoSolicitado : mapeos::ErrorLoginItem::Otro;
    case kSMErrorJobNotFound:
        return habilitar ? mapeos::ErrorLoginItem::Otro : mapeos::ErrorLoginItem::YaEnEstadoSolicitado;
    case kSMErrorLaunchDeniedByUser:
    case kSMErrorAuthorizationFailure:
    case EPERM:
        return mapeos::ErrorLoginItem::RechazadoPorUsuario;
    case kSMErrorInvalidSignature:
    case kSMErrorServiceUnavailable:
    case kSMErrorToolNotValid:
    case kSMErrorJobPlistNotFound:
    case kSMErrorInvalidPlist:
        return mapeos::ErrorLoginItem::NoDisponible;
    default:
        return mapeos::ErrorLoginItem::Otro;
    }
}

NotificationStatus leerEstadoNotificacionesSincrono(UNUserNotificationCenter* centro)
{
    if (centro == nil) {
        return NotificationStatus::Unavailable;
    }
    // El completion handler llega en una cola de UserNotifications (no en el
    // hilo principal), asi que esperar aqui no produce interbloqueo.
    dispatch_semaphore_t listo = dispatch_semaphore_create(0);
    __block long estado = mapeos::kUnAuthNotDetermined;
    [centro getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* ajustes) {
        estado = static_cast<long>(ajustes.authorizationStatus);
        dispatch_semaphore_signal(listo);
    }];
    const auto limite = dispatch_time(DISPATCH_TIME_NOW,
                                      static_cast<int64_t>(kEsperaMaximaPermisoInicialMs) * NSEC_PER_MSEC);
    if (dispatch_semaphore_wait(listo, limite) != 0) {
        qCWarning(lcMacOS) << "Sin respuesta de UNUserNotificationCenter al arrancar; se refrescara despues";
        return NotificationStatus::NotDetermined;
    }
    return mapeos::mapearAutorizacionNotificaciones(estado);
}

bool esNotificacionNoPermitida(NSError* error)
{
    return error != nil && [error.domain isEqualToString:UNErrorDomain]
        && error.code == UNErrorCodeNotificationsNotAllowed;
}

// Icono plantilla monocromo (documento con flecha de descarga). isMask ->
// NSImage template: macOS lo tine segun apariencia clara/oscura.
QIcon crearIconoPlantilla()
{
    QIcon icono;
    for (const int lado : {18, 36}) {
        QPixmap mapa(lado, lado);
        mapa.fill(Qt::transparent);
        QPainter pintor(&mapa);
        pintor.setRenderHint(QPainter::Antialiasing);
        const qreal e = lado / 18.0;
        QPen pluma(Qt::black, 1.4 * e);
        pluma.setJoinStyle(Qt::RoundJoin);
        pluma.setCapStyle(Qt::RoundCap);
        pintor.setPen(pluma);
        // Contorno del documento con esquina doblada.
        QPainterPath hoja;
        hoja.moveTo(4 * e, 2 * e);
        hoja.lineTo(11 * e, 2 * e);
        hoja.lineTo(14.5 * e, 5.5 * e);
        hoja.lineTo(14.5 * e, 16 * e);
        hoja.lineTo(4 * e, 16 * e);
        hoja.closeSubpath();
        pintor.drawPath(hoja);
        // Flecha de descarga.
        pintor.drawLine(QPointF(9.25 * e, 6 * e), QPointF(9.25 * e, 12.5 * e));
        pintor.drawLine(QPointF(6.75 * e, 10 * e), QPointF(9.25 * e, 12.5 * e));
        pintor.drawLine(QPointF(11.75 * e, 10 * e), QPointF(9.25 * e, 12.5 * e));
        pintor.end();
        mapa.setDevicePixelRatio(lado / 18.0);
        icono.addPixmap(mapa);
    }
    icono.setIsMask(true);
    return icono;
}

} // namespace

// --- Estado privado --------------------------------------------------------

struct MacOSIntegration::Impl {
    bool hayBundle = false;
    bool inicializado = false;
    bool saliendo = false;
    bool manejadorReaperturaInstalado = false;

    LaunchContext launchContext = LaunchContext::Manual;
    LoginItemStatus loginItem = LoginItemStatus::Unavailable;
    NotificationStatus notificaciones = NotificationStatus::Unavailable;

    // Estado confirmado reflejado por el controlador.
    bool preferenciaLoginItem = false;
    bool monitoreoPausado = false;

    std::unique_ptr<QMenu> menu;
    QSystemTrayIcon* bandeja = nullptr; // hijo del adaptador
    QAction* accionMonitoreo = nullptr;
    QAction* estadoMonitoreo = nullptr;    // T007: linea no seleccionable
    QAction* pendientesMonitoreo = nullptr; // "Pendientes: N" (oculta si 0)
    OSIntegration::EstadoMonitoreo monitoreo;
    QAction* accionInicioAutomatico = nullptr;
    QAction* estadoLoginItem = nullptr;
    QAction* abrirAjustesLoginItem = nullptr;
    QAction* estadoNotificaciones = nullptr;
    QAction* solicitarPermiso = nullptr;
    QAction* enviarPrueba = nullptr;
    QAction* abrirAjustesNotificaciones = nullptr;

    SatCfdiManejadorNativo* manejador = nil;
    id observadorActivacion = nil;
    // Serializa register/unregister/status: resultados en orden de llamada.
    dispatch_queue_t colaLoginItem = nil;
};

// --- Construccion / destruccion -------------------------------------------

MacOSIntegration::MacOSIntegration(QObject* parent)
    : OSIntegration(parent)
    , d(std::make_unique<Impl>())
{
    d->hayBundle = hayBundleIdentificado();
    d->manejador = [[SatCfdiManejadorNativo alloc] init];
    d->colaLoginItem = dispatch_queue_create("mx.adenium.satcfdi.loginitem", DISPATCH_QUEUE_SERIAL);

    d->launchContext = determinarLaunchContext();
    if (d->launchContext == LaunchContext::LoginItem && NSApp != nil) {
        // Arranque silencioso: sin Dock ni App Switcher hasta mostrar ventana.
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    }

    d->loginItem = leerEstadoLoginItem(d->hayBundle);
    d->notificaciones = leerEstadoNotificacionesSincrono(centroNotificaciones(d->hayBundle));

    // Detecta ventanas que se muestran (politica Regular) y QEvent::Quit.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }

    qCInfo(lcMacOS) << "MacOSIntegration: launchContext" << d->launchContext << "loginItem" << d->loginItem
                    << "notificaciones" << d->notificaciones << "bundle" << d->hayBundle;
}

MacOSIntegration::~MacOSIntegration()
{
    prepararSalida();
    delete d->bandeja;
    d->bandeja = nullptr;
    d->menu.reset();
}

// --- Consultas -------------------------------------------------------------

OSIntegration::LaunchContext MacOSIntegration::launchContext() const
{
    return d->launchContext;
}

OSIntegration::LoginItemStatus MacOSIntegration::loginItemStatus() const
{
    return d->loginItem;
}

OSIntegration::NotificationStatus MacOSIntegration::notificationStatus() const
{
    return d->notificaciones;
}

// --- Menu bar --------------------------------------------------------------

void MacOSIntegration::inicializar()
{
    if (d->inicializado || d->saliendo) {
        return;
    }
    d->inicializado = true;

    d->menu = std::make_unique<QMenu>();
    QMenu* menu = d->menu.get();

    // Las acciones solo emiten intenciones (nunca tras prepararSalida()).
    const auto intencion = [this](auto senal) {
        return [this, senal] {
            if (!d->saliendo) {
                emit(this->*senal)();
            }
        };
    };

    connect(menu->addAction(QStringLiteral("Mostrar ventana")), &QAction::triggered, this,
            intencion(&OSIntegration::mostrarVentanaSolicitada));
    connect(menu->addAction(QStringLiteral("Nueva solicitud")), &QAction::triggered, this,
            intencion(&OSIntegration::nuevaSolicitudSolicitada));
    menu->addSeparator();

    d->estadoMonitoreo = menu->addAction(QString());
    d->estadoMonitoreo->setEnabled(false);
    d->pendientesMonitoreo = menu->addAction(QString());
    d->pendientesMonitoreo->setEnabled(false);
    d->accionMonitoreo = menu->addAction(mapeos::textoAccionMonitoreo(d->monitoreoPausado));
    connect(d->accionMonitoreo, &QAction::triggered, this, [this] {
        if (!d->saliendo) {
            emit cambioMonitoreoSolicitado(!d->monitoreoPausado);
        }
    });
    menu->addSeparator();

    d->accionInicioAutomatico = menu->addAction(QStringLiteral("Iniciar al iniciar sesion"));
    d->accionInicioAutomatico->setCheckable(true);
    d->accionInicioAutomatico->setChecked(d->preferenciaLoginItem);
    // `triggered` no se emite con setChecked() programatico. El check solo
    // muestra estado confirmado: se revierte y se emite la intencion.
    connect(d->accionInicioAutomatico, &QAction::triggered, this, [this](bool marcado) {
        d->accionInicioAutomatico->setChecked(d->preferenciaLoginItem);
        if (!d->saliendo) {
            emit cambioInicioAutomaticoSolicitado(marcado);
        }
    });
    d->estadoLoginItem = menu->addAction(QString());
    d->estadoLoginItem->setEnabled(false);
    d->abrirAjustesLoginItem = menu->addAction(QStringLiteral("Abrir ajustes de inicio de sesion..."));
    connect(d->abrirAjustesLoginItem, &QAction::triggered, this, [] {
        if (@available(macOS 13.0, *)) {
            [SMAppService openSystemSettingsLoginItems];
        }
    });
    menu->addSeparator();

    d->estadoNotificaciones = menu->addAction(QString());
    d->estadoNotificaciones->setEnabled(false);
    d->solicitarPermiso = menu->addAction(QStringLiteral("Solicitar permiso de notificaciones"));
    connect(d->solicitarPermiso, &QAction::triggered, this,
            intencion(&OSIntegration::permisoNotificacionesSolicitado));
    d->enviarPrueba = menu->addAction(QStringLiteral("Enviar notificacion de prueba"));
    connect(d->enviarPrueba, &QAction::triggered, this, intencion(&OSIntegration::notificacionPruebaSolicitada));
    d->abrirAjustesNotificaciones = menu->addAction(QStringLiteral("Abrir ajustes de notificaciones..."));
    connect(d->abrirAjustesNotificaciones, &QAction::triggered, this, [] {
        NSString* bundle = [NSBundle mainBundle].bundleIdentifier;
        if (bundle == nil) {
            bundle = @"";
        }
        NSString* url = [@"x-apple.systempreferences:com.apple.Notifications-Settings.extension?id="
            stringByAppendingString:bundle];
        [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:url]];
    });
    menu->addSeparator();

    connect(menu->addAction(QStringLiteral("Salir")), &QAction::triggered, this,
            intencion(&OSIntegration::salirSolicitado));

    actualizarMenu();

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qCWarning(lcMacOS) << "QSystemTrayIcon no disponible";
    }
    d->bandeja = new QSystemTrayIcon(crearIconoPlantilla(), this);
    d->bandeja->setToolTip(QCoreApplication::applicationName());
    d->bandeja->setContextMenu(menu);
    d->bandeja->show();

    // Reapertura: clic en el Dock o abrir el bundle con el proceso vivo.
    // Se registra despues de finishLaunching para reemplazar el de AppKit.
    QPointer<MacOSIntegration> guardia(this);
    d->manejador.alReabrir = ^{
        MacOSIntegration* adaptador = guardia.data();
        if (adaptador != nullptr && !adaptador->d->saliendo) {
            emit adaptador->activarVentanaSolicitada();
        }
    };
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler:d->manejador
                                                       andSelector:@selector(manejarReapertura:respuesta:)
                                                     forEventClass:kCoreEventClass
                                                        andEventID:kAEReopenApplication];
    d->manejadorReaperturaInstalado = true;

    if (UNUserNotificationCenter* centro = centroNotificaciones(d->hayBundle)) {
        centro.delegate = d->manejador; // referencia debil: d->manejador la retiene
    }

    // El usuario puede aprobar/rechazar en Ajustes del Sistema: refrescar al
    // volver a la app.
    d->observadorActivacion = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSApplicationDidBecomeActiveNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification*) {
                    if (MacOSIntegration* adaptador = guardia.data()) {
                        adaptador->refrescarEstadosDelSistema();
                    }
                }];
}

void MacOSIntegration::actualizarMenu()
{
    if (!d->menu) {
        return;
    }
    d->accionMonitoreo->setText(mapeos::textoAccionMonitoreo(d->monitoreoPausado));
    d->estadoMonitoreo->setText(mapeos::textoEstadoMonitoreo(d->monitoreo));
    d->pendientesMonitoreo->setText(mapeos::textoPendientes(d->monitoreo.pendientes));
    d->pendientesMonitoreo->setVisible(d->monitoreo.pendientes > 0);
    d->accionInicioAutomatico->setChecked(d->preferenciaLoginItem);
    d->accionInicioAutomatico->setEnabled(d->loginItem != LoginItemStatus::Unavailable || d->preferenciaLoginItem);
    d->estadoLoginItem->setText(mapeos::textoEstadoLoginItem(d->loginItem));
    d->abrirAjustesLoginItem->setVisible(d->loginItem == LoginItemStatus::RequiresApproval
                                         || d->loginItem == LoginItemStatus::Rejected);

    d->estadoNotificaciones->setText(mapeos::textoEstadoNotificaciones(d->notificaciones));
    d->solicitarPermiso->setVisible(d->notificaciones == NotificationStatus::NotDetermined);
    d->enviarPrueba->setVisible(d->notificaciones == NotificationStatus::Granted
                                || d->notificaciones == NotificationStatus::Denied);
    d->abrirAjustesNotificaciones->setVisible(d->notificaciones == NotificationStatus::Denied);
}

void MacOSIntegration::reflejarPreferenciaLoginItem(bool habilitado)
{
    d->preferenciaLoginItem = habilitado;
    actualizarMenu();
}

void MacOSIntegration::reflejarMonitoreoPausado(bool pausado)
{
    d->monitoreoPausado = pausado;
    actualizarMenu();
}

void MacOSIntegration::reflejarEstadoMonitoreo(const EstadoMonitoreo& estado)
{
    if (d->monitoreo == estado) {
        return;
    }
    d->monitoreo = estado;
    actualizarMenu();
}

// --- Estado observable -----------------------------------------------------

void MacOSIntegration::actualizarLoginItemStatus(LoginItemStatus estado)
{
    if (d->loginItem == estado) {
        return;
    }
    d->loginItem = estado;
    actualizarMenu();
    emit loginItemStatusChanged(estado);
}

void MacOSIntegration::actualizarNotificationStatus(NotificationStatus estado)
{
    if (d->notificaciones == estado) {
        return;
    }
    d->notificaciones = estado;
    actualizarMenu();
    emit notificationStatusChanged(estado);
}

void MacOSIntegration::refrescarEstadosDelSistema()
{
    const QPointer<MacOSIntegration> guardia(this);
    if (d->hayBundle) {
        if (@available(macOS 13.0, *)) {
            dispatch_async(d->colaLoginItem, ^{
                const long estado = static_cast<long>(SMAppService.mainAppService.status);
                enHiloGrafico(guardia, [estado](MacOSIntegration* a) {
                    a->actualizarLoginItemStatus(mapeos::mapearEstadoLoginItem(estado));
                });
            });
        }
    }
    if (UNUserNotificationCenter* centro = centroNotificaciones(d->hayBundle)) {
        [centro getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* ajustes) {
            const long estado = static_cast<long>(ajustes.authorizationStatus);
            enHiloGrafico(guardia, [estado](MacOSIntegration* a) {
                a->actualizarNotificationStatus(mapeos::mapearAutorizacionNotificaciones(estado));
            });
        }];
    }
}

// --- Login Item (SMAppService.mainApp) -------------------------------------

void MacOSIntegration::configurarLoginItem(bool habilitar)
{
    const QPointer<MacOSIntegration> guardia(this);
    bool disponible = d->hayBundle;
    if (@available(macOS 13.0, *)) {
    } else {
        disponible = false;
    }
    if (!disponible) {
        enHiloGrafico(guardia, [habilitar](MacOSIntegration* a) {
            a->actualizarLoginItemStatus(LoginItemStatus::Unavailable);
            emit a->loginItemConfigurado(habilitar, LoginItemStatus::Unavailable);
        });
        return;
    }

    if (@available(macOS 13.0, *)) {
        // register/unregister son sincronos (XPC): fuera del hilo grafico.
        dispatch_async(d->colaLoginItem, ^{
            SMAppService* servicio = SMAppService.mainAppService;
            NSError* error = nil;
            const BOOL ok = habilitar ? [servicio registerAndReturnError:&error]
                                      : [servicio unregisterAndReturnError:&error];
            const long estadoLeido = static_cast<long>(servicio.status);
            const mapeos::ErrorLoginItem tipo =
                ok ? mapeos::ErrorLoginItem::Ninguno : clasificarErrorLoginItem(error, habilitar);
            if (!ok) {
                qCWarning(lcMacOS) << "SMAppService" << (habilitar ? "register" : "unregister")
                                   << "fallo: dominio" << QString::fromNSString(error.domain != nil ? error.domain : @"") << "codigo"
                                   << static_cast<long>(error.code) << "status" << estadoLeido;
            }
            enHiloGrafico(guardia, [habilitar, tipo, estadoLeido](MacOSIntegration* a) {
                const LoginItemStatus estado = mapeos::mapearResultadoLoginItem(habilitar, tipo, estadoLeido);
                a->actualizarLoginItemStatus(estado);
                emit a->loginItemConfigurado(habilitar, estado);
            });
        });
    }
}

// --- Notificaciones (UNUserNotificationCenter) ------------------------------

void MacOSIntegration::solicitarPermisoNotificaciones()
{
    const QPointer<MacOSIntegration> guardia(this);
    UNUserNotificationCenter* centro = centroNotificaciones(d->hayBundle);
    if (centro == nil) {
        enHiloGrafico(guardia, [](MacOSIntegration* a) {
            a->actualizarNotificationStatus(NotificationStatus::Unavailable);
            emit a->permisoNotificacionesResuelto(NotificationStatus::Unavailable);
        });
        return;
    }

    const UNAuthorizationOptions opciones =
        UNAuthorizationOptionAlert | UNAuthorizationOptionSound | UNAuthorizationOptionBadge;
    [centro requestAuthorizationWithOptions:opciones
                          completionHandler:^(BOOL concedido, NSError* error) {
                              (void)concedido;
                              const bool noPermitido = esNotificacionNoPermitida(error);
                              if (error != nil && !noPermitido) {
                                  qCWarning(lcMacOS) << "requestAuthorization fallo: codigo"
                                                     << static_cast<long>(error.code);
                              }
                              // El estado real se relee siempre de los ajustes.
                              [centro getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* ajustes) {
                                  const long leido = static_cast<long>(ajustes.authorizationStatus);
                                  enHiloGrafico(guardia, [leido, noPermitido](MacOSIntegration* a) {
                                      NotificationStatus estado = mapeos::mapearAutorizacionNotificaciones(leido);
                                      if (noPermitido && estado == NotificationStatus::NotDetermined) {
                                          estado = NotificationStatus::Denied;
                                      }
                                      a->actualizarNotificationStatus(estado);
                                      emit a->permisoNotificacionesResuelto(estado);
                                  });
                              }];
                          }];
}

void MacOSIntegration::enviarNotificacionPrueba(const QString& titulo, const QString& cuerpo)
{
    entregarNotificacion(QStringLiteral("mx.adenium.satcfdi.prueba.") + QUuid::createUuid().toString(QUuid::WithoutBraces),
                         QStringLiteral("prueba"), titulo, cuerpo,
                         [](MacOSIntegration* a, NotificationSendResult r) { emit a->notificacionPruebaTerminada(r); });
}

void MacOSIntegration::notificar(const NotificacionLocal& n)
{
    const QString id = n.id;
    // Mismo identificador -> el SO reemplaza (dedupe adicional al del servicio).
    entregarNotificacion(QStringLiteral("mx.adenium.satcfdi.") + n.id, n.tipo, n.titulo, n.cuerpo,
                         [id](MacOSIntegration* a, NotificationSendResult r) { emit a->notificacionTerminada(id, r); });
}

void MacOSIntegration::entregarNotificacion(const QString& identificadorQt, const QString& hilo, const QString& titulo,
                                            const QString& cuerpo,
                                            std::function<void(MacOSIntegration*, NotificationSendResult)> alTerminar)
{
    const QPointer<MacOSIntegration> guardia(this);
    UNUserNotificationCenter* centro = centroNotificaciones(d->hayBundle);
    if (centro == nil) {
        enHiloGrafico(guardia, [alTerminar](MacOSIntegration* a) { alTerminar(a, NotificationSendResult::Unavailable); });
        return;
    }

    NSString* tituloNativo = titulo.toNSString();
    NSString* cuerpoNativo = cuerpo.toNSString();
    NSString* identificador = identificadorQt.toNSString();
    NSString* hiloNativo = hilo.toNSString();
    // Nunca solicita permiso: decide con el estado vigente del SO.
    [centro getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* ajustes) {
        const long leido = static_cast<long>(ajustes.authorizationStatus);
        const NotificationStatus estado = mapeos::mapearAutorizacionNotificaciones(leido);
        if (estado != NotificationStatus::Granted) {
            enHiloGrafico(guardia, [estado, alTerminar](MacOSIntegration* a) {
                a->actualizarNotificationStatus(estado);
                alTerminar(a, mapeos::resultadoEnvioSegunEstado(estado));
            });
            return;
        }

        UNMutableNotificationContent* contenido = [[UNMutableNotificationContent alloc] init];
        contenido.title = tituloNativo;
        contenido.body = cuerpoNativo;
        contenido.threadIdentifier = hiloNativo;
        contenido.sound = [UNNotificationSound defaultSound];
        UNNotificationRequest* peticion = [UNNotificationRequest requestWithIdentifier:identificador
                                                                              content:contenido
                                                                              trigger:nil];
        [centro addNotificationRequest:peticion
                 withCompletionHandler:^(NSError* error) {
                     NotificationSendResult resultado = NotificationSendResult::Sent;
                     if (esNotificacionNoPermitida(error)) {
                         resultado = NotificationSendResult::PermissionDenied;
                     } else if (error != nil) {
                         qCWarning(lcMacOS) << "addNotificationRequest fallo: codigo" << static_cast<long>(error.code);
                         resultado = NotificationSendResult::Failed;
                     }
                     enHiloGrafico(guardia, [resultado, alTerminar](MacOSIntegration* a) {
                         a->actualizarNotificationStatus(resultado == NotificationSendResult::PermissionDenied
                                                             ? NotificationStatus::Denied
                                                             : NotificationStatus::Granted);
                         alTerminar(a, resultado);
                     });
                 }];
    }];
}

// --- Activacion y salida ---------------------------------------------------

void MacOSIntegration::asegurarModoRegular()
{
    if (NSApp == nil) {
        return;
    }
    if (NSApp.activationPolicy != NSApplicationActivationPolicyRegular) {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    }
    if (@available(macOS 14.0, *)) {
        [NSApp activate];
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
    }
}

bool MacOSIntegration::eventFilter(QObject* objeto, QEvent* evento)
{
    switch (evento->type()) {
    case QEvent::Quit:
        // Cmd-Q, Salir del Dock o menu de la app: AppKit -> applicationShouldTerminate
        // -> QEvent::Quit a qApp. Se cancela (ignore) y se emite la intencion;
        // tras prepararSalida() el filtro ya no esta instalado.
        if (objeto == QCoreApplication::instance() && d->inicializado && !d->saliendo) {
            evento->ignore();
            QTimer::singleShot(0, this, [this] {
                if (!d->saliendo) {
                    emit salirSolicitado();
                }
            });
            return true;
        }
        break;
    case QEvent::Show:
        if (auto* ventana = qobject_cast<QWindow*>(objeto)) {
            const Qt::WindowType tipo = ventana->type();
            if (ventana->isTopLevel() && (tipo == Qt::Window || tipo == Qt::Dialog)) {
                asegurarModoRegular();
            }
        }
        break;
    default:
        break;
    }
    return OSIntegration::eventFilter(objeto, evento);
}

void MacOSIntegration::prepararSalida()
{
    if (d->saliendo) {
        return;
    }
    d->saliendo = true;

    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->removeEventFilter(this);
    }
    if (d->bandeja != nullptr) {
        d->bandeja->hide();
        d->bandeja->setContextMenu(nullptr);
    }
    if (d->manejadorReaperturaInstalado) {
        [[NSAppleEventManager sharedAppleEventManager] removeEventHandlerForEventClass:kCoreEventClass
                                                                          andEventID:kAEReopenApplication];
        d->manejadorReaperturaInstalado = false;
    }
    d->manejador.alReabrir = nil;
    if (d->observadorActivacion != nil) {
        [[NSNotificationCenter defaultCenter] removeObserver:d->observadorActivacion];
        d->observadorActivacion = nil;
    }
    if (UNUserNotificationCenter* centro = centroNotificaciones(d->hayBundle)) {
        if (centro.delegate == d->manejador) {
            centro.delegate = nil;
        }
    }
}

} // namespace satcfdi
