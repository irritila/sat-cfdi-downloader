#pragma once

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <memory>

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

    // Estado del monitoreo local para el menu bar (T007 D2/D11). Vocabulario
    // propio del puerto (no depende de application): app_core traduce
    // InstantaneaWorker a esta forma.
    struct EstadoMonitoreo {
        enum class Fase { Pausado, ActivoEnEspera, Ejecutando, Deteniendo, Detenido };
        enum class Actividad { Ninguna, Enviando, Verificando, Descargando, Otra };
        Fase fase = Fase::Detenido;
        Actividad actividad = Actividad::Ninguna; // solo en Ejecutando
        int pendientes = 0;                       // solicitudes con intencion pendiente

        friend bool operator==(const EstadoMonitoreo&, const EstadoMonitoreo&) = default;
    };

    // T009.1 D5: resultado de mostrarEnFinder/abrirCarpetaEnFinder (y de
    // abrirEnVistaPrevia, T015.4; Mostrado = Vista Previa acepto abrirlo).
    // NoEncontrado: el destino no existe (o no es del tipo esperado) al
    // revalidarlo justo antes de llamar a Finder; Fallido: Finder/NSWorkspace
    // reporto un fallo o la plataforma no lo soporta.
    enum class ResultadoFinder { Mostrado, NoEncontrado, Fallido };
    Q_ENUM(ResultadoFinder)

    // T014.4 D1: accion elegida al responder una notificacion. Abrir = pulsar
    // el cuerpo; MostrarEnFinder / AbrirPerfiles = botones de la notificacion.
    enum class AccionNotificacion { Abrir, MostrarEnFinder, AbrirPerfiles };
    Q_ENUM(AccionNotificacion)

    // T014.4 D1: destino interno de una notificacion. `id` es SOLO el UUID
    // canonico interno (minusculas, sin llaves) de la solicitud o del perfil;
    // nunca RFC, rutas, tokens ni datos SAT. Ninguno = solo activar la app.
    struct DestinoNotificacion {
        enum class Tipo { Ninguno, Solicitud, Perfil };
        Tipo tipo = Tipo::Ninguno;
        QString id;

        friend bool operator==(const DestinoNotificacion&, const DestinoNotificacion&) = default;
    };

    // T014.4 D2: estado visual agregado del icono del menu bar (estado de
    // presentacion; no es el estado del worker). Prioridad decidida por
    // app_core: Atencion > Trabajando > Pausado > Normal.
    enum class EstadoIcono { Normal, Trabajando, Pausado, Atencion };
    Q_ENUM(EstadoIcono)

    // Notificacion local (T009 D1, D9). `id`: clave estable de dedupe que
    // decide el servicio de aplicacion (p. ej. "<solicitud>:terminada"); el SO
    // reemplaza una notificacion con el mismo id en lugar de duplicarla.
    // `tipo`: clave estable de la categoria ("terminada", "descarga_completa",
    // "error_sat", "rechazada", "vencida", "credencial"). Titulo y cuerpo ya
    // vienen del catalogo D10: sin RFC completo, Ids, token, rutas ni Mensaje
    // SAT crudo.
    // T014.4 D1: `destino` viaja en la notificacion (userInfo) y vuelve en
    // notificacionActivada(). `accionesExtra`: [MostrarEnFinder] para
    // "descarga_completa"; [AbrirPerfiles] para "credencial" y
    // "efirma_por_vencer"; vacio en el resto. El adaptador solo muestra
    // MostrarEnFinder con destino Solicitud y AbrirPerfiles con destino Perfil.
    struct NotificacionLocal {
        QString id;
        QString tipo;
        QString titulo;
        QString cuerpo;
        DestinoNotificacion destino;
        QList<AccionNotificacion> accionesExtra;

        friend bool operator==(const NotificacionLocal&, const NotificacionLocal&) = default;
    };

    // T025.4 (ADR 0009): motivo de una actividad de sistema. El adaptador lo
    // traduce a un texto fijo sin datos personales (visible en
    // `pmset -g assertions`).
    enum class MotivoActividad { Respaldo };
    Q_ENUM(MotivoActividad)

    // T025.4: token RAII de una actividad de sistema. Mientras vive, el SO no
    // aplica App Nap ni el reposo por inactividad al proceso (macOS:
    // NSProcessInfo beginActivityWithOptions:NSActivityUserInitiated); al
    // destruirse la termina. Move-only por unique_ptr; puede destruirse en
    // cualquier hilo y vivir mas que el adaptador. La clase base es el token
    // no-op (plataformas sin soporte).
    class ActividadSistema {
    public:
        ActividadSistema() = default;
        virtual ~ActividadSistema() = default;
        ActividadSistema(const ActividadSistema&) = delete;
        ActividadSistema& operator=(const ActividadSistema&) = delete;
    };

    // T025.5: resultado de solicitarRelanzamiento(). Programado = el
    // relanzamiento quedo en manos de un proceso auxiliar que espera a que
    // este proceso termine; no garantiza que la nueva instancia arranque.
    enum class ResultadoRelanzamiento { Programado, NoSoportado, Fallido };
    Q_ENUM(ResultadoRelanzamiento)

    // T025.6 C4: resultado de imprimirTexto(). Enviado = el usuario confirmo
    // el dialogo del sistema (imprimir o su "Guardar como PDF"); Cancelado =
    // lo cerro sin imprimir o la impresion fallo (AppKit no los distingue);
    // NoSoportado = plataforma sin impresion nativa; Fallido = no se pudo
    // preparar la impresion (sin NSApp, texto vacio).
    enum class ResultadoImpresion { Enviado, Cancelado, NoSoportado, Fallido };
    Q_ENUM(ResultadoImpresion)

    // T025.6 H1/M1: resultado de copiarTextoSecreto(). Copiado = el texto
    // esta en el portapapeles del sistema; NoSoportado = plataforma sin copia
    // segura (el llamador NO debe recurrir a QClipboard); Fallido = el SO
    // rechazo la escritura o el texto esta vacio.
    enum class ResultadoCopiaSecreta { Copiado, NoSoportado, Fallido };
    Q_ENUM(ResultadoCopiaSecreta)
    // Plazo tras el que se borra el texto copiado si sigue en el portapapeles.
    static constexpr int kSegundosBorradoPortapapeles = 90;

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

    // T009 D9: entrega una notificacion local. Asincrono: emite SIEMPRE
    // notificacionTerminada(id, resultado). Nunca solicita permiso: con
    // NotDetermined/Denied/Unavailable solo reporta el resultado (no falla ni
    // lanza). El llamador no debe cambiar estados, logs ni reintentos por el
    // resultado.
    virtual void notificar(const NotificacionLocal& notificacion) = 0;

    // T009.1 D5: abre Finder con el ARCHIVO `rutaAbsoluta` seleccionado.
    // Asincrono: emite SIEMPRE finderTerminado(peticionId, resultado). Revalida
    // que existe y es un archivo regular (no enlace) antes de llamar a Finder;
    // nunca lee el archivo. No cambia estados ni logs.
    virtual void mostrarEnFinder(const QString& peticionId, const QString& rutaAbsoluta) = 0;
    // T009.1 D5: abre en Finder la CARPETA `rutaAbsoluta`. Mismas reglas
    // (revalida que es un directorio, no enlace).
    virtual void abrirCarpetaEnFinder(const QString& peticionId, const QString& rutaAbsoluta) = 0;
    // T015.4: abre el ARCHIVO `rutaAbsoluta` (PDF de la cache o temporal) con
    // Vista Previa (com.apple.Preview), no con la app por defecto. Asincrono:
    // emite SIEMPRE finderTerminado(peticionId, resultado). Revalida que existe
    // y es un archivo regular (no enlace) -> si no, NoEncontrado; si Vista
    // Previa no se encuentra o la apertura falla -> Fallido. Nunca lee el
    // archivo ni registra la ruta. El llamador (AccesoFinder) ya restringio
    // la ruta a los directorios permitidos.
    virtual void abrirEnVistaPrevia(const QString& peticionId, const QString& rutaAbsoluta) = 0;

    // Refleja en el menu bar estado ya confirmado por persistencia (check de
    // preferencia de inicio automatico; Pausar/Reanudar). No emite
    // intenciones ni llama al SO para cambiar el Login Item.
    virtual void reflejarPreferenciaLoginItem(bool habilitado) = 0;
    virtual void reflejarMonitoreoPausado(bool pausado) = 0;

    // T007 (D2, D11): refleja el estado del worker en una linea NO
    // seleccionable del menu bar ("Monitoreo activo", "Monitoreo en pausa",
    // "Trabajando: enviando…/verificando…/descargando…") y "Pendientes: N"
    // cuando N > 0. Sin errores detallados. No emite intenciones.
    virtual void reflejarEstadoMonitoreo(const EstadoMonitoreo& estado) = 0;

    // T014.4 D2: cambia SOLO el icono plantilla del menu bar y su
    // tooltip/nombre accesible ("Archivo Fiscal", "...: trabajando",
    // "...: en pausa", "...: requiere atención"). No emite intenciones ni
    // cambia el menu. Puede llamarse antes de inicializar() (se aplica al
    // crear el icono).
    virtual void reflejarEstadoIcono(EstadoIcono estado) = 0;

    // T025.4: inicia una actividad de sistema que dura lo que viva el token
    // devuelto (nunca nulo). EXCEPCION a la regla de hilo: se puede llamar
    // desde cualquier hilo (el respaldo corre fuera del hilo grafico) y no
    // toca estado del adaptador. Implementacion por omision: token no-op.
    virtual std::unique_ptr<ActividadSistema> iniciarActividadSistema(MotivoActividad motivo);

    // T025.5: pide al SO que, cuando ESTE proceso termine, vuelva a abrir la
    // app una sola vez con los mismos argumentos (p. ej. --data-dir). Solo
    // programa: el llamador debe salir despues por el flujo ordenado
    // (AppLifecycleController::solicitarSalida). La nueva instancia arranca
    // cuando el pid actual ya no existe, asi que encuentra libre el canal de
    // SingleInstanceCoordinator; si para entonces otra instancia ya es la
    // primaria (apertura manual), la relanzada queda como secundaria y sale,
    // nunca hay dos primarias. Si el proceso no termina dentro de un plazo
    // (macOS: 120 s), no se relanza.
    // Uso en la restauracion: el marcador debe ser durable ANTES de llamar;
    // si el relanzamiento falla o no ocurre, el marcador se aplica en el
    // siguiente arranque manual.
    // Hilo grafico. Idempotente: una segunda llamada devuelve el resultado de
    // la primera sin programar otro relanzamiento. Implementacion por
    // omision: NoSoportado. macOS tambien devuelve NoSoportado, sin programar
    // nada, si la app corre trasladada (App Translocation, R6): la UI debe
    // pedir que el usuario la mueva (p. ej. a Aplicaciones) y la reabra; el
    // marcador se aplica en ese arranque manual.
    virtual ResultadoRelanzamiento solicitarRelanzamiento();

    // T025.6 C4 (D6 "imprimible"): imprime `texto` (p. ej. la clave de
    // recuperacion) bajo `titulo` con el dialogo de impresion del sistema.
    // Reglas: la app no escribe ningun archivo (ni temporal) ni registra el
    // texto o el titulo; la vista vive solo en memoria y su contenido se
    // vacia al terminar (best effort: las copias inmutables de NSString/
    // QString no se pueden sobrescribir). Lo que haga el sistema despues
    // (cola de impresion, "Guardar como PDF") lo decide el usuario en el
    // dialogo nativo.
    // Hilo grafico. SINCRONO: muestra el panel de impresion app-modal
    // (NSPrintOperation runOperation, patron nativo como QMessageBox::exec) y
    // vuelve al cerrarlo; mientras tanto el bucle de eventos sigue atendiendo
    // eventos (no congela la app, pero el llamador debe tolerar reentrada).
    // Implementacion por omision: NoSoportado.
    virtual ResultadoImpresion imprimirTexto(const QString& titulo, const QString& texto);

    // T025.6 H1/M1: copia un texto secreto (la clave de recuperacion) al
    // portapapeles SIN salir del equipo: no se publica en el Portapapeles
    // universal (Handoff), se marca oculto y transitorio para los gestores de
    // portapapeles (convencion nspasteboard.org) y se borra a los
    // kSegundosBorradoPortapapeles si nadie copio otra cosa despues. Al salir
    // la app (prepararSalida) se borra con la misma regla. Una copia nueva
    // reinicia el plazo. Nunca registra el texto.
    // Hilo grafico; no bloquea. Implementacion por omision: NoSoportado.
    virtual ResultadoCopiaSecreta copiarTextoSecreto(const QString& texto);

    // Retira el menu bar y deja de emitir intenciones antes de terminar el
    // proceso. Idempotente. Despues solo se permite destruir el adaptador.
    virtual void prepararSalida() = 0;

signals:
    // --- Intenciones (menu bar, Dock, reapertura, segunda instancia). ---
    void mostrarVentanaSolicitada();
    void ocultarVentanaSolicitada();
    void enfocarVentanaSolicitada();
    // T023.5: "Ver facturas" del menu bar (muestra la ventana en Facturas).
    void verFacturasSolicitada();
    // T009.1 D1: "Abrir carpeta de paquetes" desde el menu bar.
    void abrirCarpetaPaquetesSolicitada();
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
    // T025.4 (D4): el sistema desperto del reposo (macOS:
    // NSWorkspaceDidWakeNotification). Hilo grafico; solo entre inicializar()
    // y prepararSalida(). Puede llegar mas de una vez seguida: quien la
    // consume decide si hay algo vencido y evita ejecuciones duplicadas.
    void sistemaDesperto();
    // T014.4 D1: el usuario respondio a una notificacion. Pulsar el cuerpo
    // -> Abrir; botones -> MostrarEnFinder / AbrirPerfiles. Siempre en el
    // hilo grafico. El destino se reconstruye desde la notificacion y se
    // valida (tipo conocido + UUID canonico); si no es valido llega
    // Destino Ninguno con Abrir (solo activar la app). Respuestas recibidas
    // antes de inicializar() (arranque por notificacion) se encolan y se
    // emiten, en orden y de forma diferida (en cola, no dentro de la llamada),
    // despues de inicializar(). Tras prepararSalida() no se emite.
    void notificacionActivada(const satcfdi::OSIntegration::DestinoNotificacion& destino,
                              satcfdi::OSIntegration::AccionNotificacion accion);

    // --- Estado observable. ---
    void loginItemStatusChanged(satcfdi::OSIntegration::LoginItemStatus estado);
    void notificationStatusChanged(satcfdi::OSIntegration::NotificationStatus estado);

    // --- Resultados de comandos asincronos (uno por llamada). ---
    void loginItemConfigurado(bool habilitar, satcfdi::OSIntegration::LoginItemStatus estado);
    void permisoNotificacionesResuelto(satcfdi::OSIntegration::NotificationStatus estado);
    void notificacionPruebaTerminada(satcfdi::OSIntegration::NotificationSendResult resultado);
    // Resultado de notificar() (uno por llamada).
    void notificacionTerminada(const QString& id, satcfdi::OSIntegration::NotificationSendResult resultado);
    // Resultado de mostrarEnFinder()/abrirCarpetaEnFinder()/abrirEnVistaPrevia()
    // (uno por llamada).
    void finderTerminado(const QString& peticionId, satcfdi::OSIntegration::ResultadoFinder resultado);
};

} // namespace satcfdi

// Para conexiones en cola y QSignalSpy (los enums Q_ENUM ya se registran).
Q_DECLARE_METATYPE(satcfdi::OSIntegration::DestinoNotificacion)
