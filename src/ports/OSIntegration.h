#pragma once

#include <QObject>
#include <QString>

namespace satcfdi {

// Puerto de integracion con el sistema operativo (ADR 0009, T004): menu bar,
// contexto de arranque, Login Item, activacion y notificaciones nativas.
//
// Responsabilidades:
// - El adaptador (MacOSIntegration en produccion, FakeOSIntegration en
//   pruebas) solo traduce eventos del SO/menu bar a SENALES DE INTENCION y
//   ejecuta COMANDOS de SO. No navega, no manipula la ventana QML, no modifica
//   solicitudes y no toca SQLite ni repositorios.
// - AppLifecycleController (app_core) conecta las intenciones con servicios de
//   aplicacion y navegacion, y refleja en el adaptador solo estado CONFIRMADO
//   (reflejarPreferenciaLoginItem / reflejarMonitoreoPausado).
// - La preferencia local de inicio automatico (SQLite, ConfiguracionApp) y el
//   estado efectivo de Login Item (loginItemStatus(), runtime) son datos
//   separados: el adaptador nunca persiste el estado efectivo.
//
// Hilo y ownership:
// - QObject con afinidad al hilo grafico. Todos los comandos se invocan desde
//   el hilo grafico y todas las senales se emiten en el hilo grafico (el
//   adaptador reencamina callbacks nativos de otros hilos antes de emitir).
// - Lo crea y posee el composition root/main; vive mas que el controlador.
// - Ningun comando bloquea el hilo grafico: los resultados asincronos
//   (Login Item, permiso, envio de notificacion) llegan por senal.
//
// Los enums son vocabulario del contrato (no del dominio ni de macOS).
class OSIntegration : public QObject {
    Q_OBJECT

public:
    // Origen del arranque del proceso; lo determina el adaptador (Apple Event
    // kAELaunchedAsLogInItem), nunca la preferencia local, PID o argumentos.
    enum class LaunchContext { Manual, LoginItem };
    Q_ENUM(LaunchContext)

    // Estado efectivo del Login Item reportado por el SO.
    enum class LoginItemStatus { Disabled, Enabled, RequiresApproval, Rejected, Unavailable };
    Q_ENUM(LoginItemStatus)

    // Estado de autorizacion de notificaciones reportado por el SO.
    enum class NotificationStatus { NotDetermined, Granted, Denied, Unavailable };
    Q_ENUM(NotificationStatus)

    // Resultado de enviarNotificacionPrueba(). `Sent` significa que el SO
    // acepto la peticion; no garantiza que el usuario la haya visto.
    enum class NotificationSendResult { Sent, PermissionDenied, PermissionNotDetermined, Unavailable, Failed };
    Q_ENUM(NotificationSendResult)

    using QObject::QObject;
    ~OSIntegration() override;

    OSIntegration(const OSIntegration&) = delete;
    OSIntegration& operator=(const OSIntegration&) = delete;

    // Crea el icono/menu de menu bar con el estado inicial y empieza a emitir
    // intenciones. Llamar una vez, en el hilo grafico, tras conectar las
    // senales. No muestra la ventana (lo decide el controlador).
    virtual void inicializar() = 0;

    // Valido desde la construccion del adaptador (antes de inicializar()).
    virtual LaunchContext launchContext() const = 0;
    // Ultimo estado conocido; los cambios se notifican por senal.
    virtual LoginItemStatus loginItemStatus() const = 0;
    virtual NotificationStatus notificationStatus() const = 0;

    // Registra (true) o da de baja (false) el Login Item en el SO. Asincrono:
    // al terminar emite SIEMPRE loginItemConfigurado(habilitar, estado) y,
    // si el estado efectivo cambio, loginItemStatusChanged(estado).
    // No persiste nada; el controlador decide cuando llamarlo.
    virtual void configurarLoginItem(bool habilitar) = 0;

    // Solicita permiso de notificaciones al SO. Solo debe llamarse como
    // respuesta a una accion explicita del usuario. Asincrono: emite SIEMPRE
    // permisoNotificacionesResuelto(estado) y, si cambio,
    // notificationStatusChanged(estado). Un estado Denied no es un error del
    // proceso.
    virtual void solicitarPermisoNotificaciones() = 0;

    // Envia una notificacion local de prueba. Asincrono: emite SIEMPRE
    // notificacionPruebaTerminada(resultado). Nunca solicita permiso por si
    // mismo: con NotDetermined responde PermissionNotDetermined.
    virtual void enviarNotificacionPrueba(const QString& titulo, const QString& cuerpo) = 0;

    // Refleja en el menu bar estado ya confirmado por persistencia (check de
    // preferencia de inicio automatico; Pausar/Reanudar). No emite
    // intenciones ni llama al SO para cambiar el Login Item.
    virtual void reflejarPreferenciaLoginItem(bool habilitado) = 0;
    virtual void reflejarMonitoreoPausado(bool pausado) = 0;

    // Retira el menu bar y deja de emitir intenciones antes de terminar el
    // proceso. Idempotente. Despues solo se permite destruir el adaptador.
    virtual void prepararSalida() = 0;

signals:
    // --- Intenciones (menu bar, Dock, reapertura, segunda instancia). ---
    void mostrarVentanaSolicitada();
    void ocultarVentanaSolicitada();
    void enfocarVentanaSolicitada();
    void nuevaSolicitudSolicitada();
    // true = pausar monitoreo; false = reanudar.
    void cambioMonitoreoSolicitado(bool pausar);
    // Preferencia de inicio automatico pedida por el usuario (check del menu).
    void cambioInicioAutomaticoSolicitado(bool habilitar);
    // Accion explicita "Solicitar permiso" del usuario.
    void permisoNotificacionesSolicitado();
    // Accion explicita "Enviar notificacion de prueba" del usuario.
    void notificacionPruebaSolicitada();
    // Salida explicita (menu Salir, Cmd-Q). El adaptador no termina el
    // proceso por si mismo.
    void salirSolicitado();
    // Reapertura (kAEReopenApplication) o ActivateWindow de segunda instancia.
    void activarVentanaSolicitada();

    // --- Estado observable. ---
    void loginItemStatusChanged(satcfdi::OSIntegration::LoginItemStatus estado);
    void notificationStatusChanged(satcfdi::OSIntegration::NotificationStatus estado);

    // --- Resultados de comandos asincronos (uno por llamada). ---
    void loginItemConfigurado(bool habilitar, satcfdi::OSIntegration::LoginItemStatus estado);
    void permisoNotificacionesResuelto(satcfdi::OSIntegration::NotificationStatus estado);
    void notificacionPruebaTerminada(satcfdi::OSIntegration::NotificationSendResult resultado);
};

} // namespace satcfdi
