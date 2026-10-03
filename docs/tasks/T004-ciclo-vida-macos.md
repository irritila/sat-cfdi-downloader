# T004: Ciclo de vida macOS y menu bar

## Estado

Completada (criterio de notificacion con permiso concedido bloqueado por firma)

## Prioridad y tamano

- Prioridad: Alta.
- Tamano: Mediana.

## Objetivo

Implementar la integracion macOS para mantener una sola instancia local de la app, controlar la ventana desde menu bar, configurar Login Item y exponer notificaciones nativas.

## Contexto

La app debe permanecer ejecutandose para que el worker pueda monitorear solicitudes, pero no debe abrir una ventana al iniciar automaticamente. Cuando el usuario la abre manualmente, debe mostrar la ventana principal aunque el proceso ya este activo en menu bar.

## Alcance

### Incluye

- Implementacion inicial de `OSIntegration` y `MacOSIntegration`.
- Icono de menu bar mediante `QSystemTrayIcon`.
- Menu bar con mostrar ventana, nueva solicitud, pausar/reanudar y salir.
- Mostrar y ocultar ventana principal.
- Cerrar ventana hacia menu bar sin terminar el proceso.
- Salida explicita desde menu bar.
- Login Item opcional, apagado por defecto.
- Estados del Login Item: habilitado, pendiente, rechazado y no disponible.
- Solicitud de permiso y estado de notificaciones nativas.
- Notificacion de prueba y manejo de permiso denegado.
- Reapertura/foco de la ventana cuando ya existe un proceso de la app.

### No incluye

- Worker SAT real.
- Ejecucion de operaciones SAT.
- Persistencia de solicitudes o paquetes.
- Keychain.
- Reglas de negocio de notificaciones.
- Soporte Windows.
- Daemon, LaunchAgent o helper independiente.

## Dependencias

- `T002-shell-qt-qml.md`.
- `T003-persistencia-local.md` para guardar preferencias y estado de cierre.
- ADR 0003 y ADR 0009.
- `docs/design/qt-project-structure.md`.
- `ConfiguracionAppRepository` de T003 debe permitir actualizar
  `inicio_automatico_habilitado`, `monitoreo_pausado` y `ultimo_cierre_en` a
  traves de `PersistenceDispatcher`.

## Trabajo esperado

1. Implementar el adaptador macOS detras del contrato `OSIntegration`.
2. Crear el icono y menu de menu bar desde C++.
3. Conectar mostrar/ocultar ventana y salida explicita con la ventana QML.
4. Implementar la señal o mecanismo de activacion cuando se abre una segunda vez la app.
5. Integrar el Login Item sin crear un proceso auxiliar.
6. Persistir la preferencia solicitada y consultar el estado efectivo reportado por macOS.
7. Integrar solicitudes de permiso y estado de notificaciones.
8. Crear un fake o harness de `OSIntegration` para pruebas de aplicacion.

## Decisiones cerradas del refinamiento

- El MVP soporta macOS 13.0 o superior y usa `SMAppService.mainApp` para el
  Login Item. No se agrega `LaunchAgent`, daemon ni helper.
- El contexto de arranque se obtiene de AppKit mediante el Apple Event de
  apertura (`kAELaunchedAsLogInItem`), no de la preferencia local, PID o
  argumentos.
- La instancia unica usa `QLocalServer`/`QLocalSocket` por usuario y
  `CFBundleIdentifier`, complementado por el evento `kAEReopenApplication`.
  Una segunda apertura envia `ActivateWindow` a la instancia primaria y
  termina.
- El identificador recomendado del bundle es
  `mx.adenium.satcfdi-downloader`. La prueba real de Login Item requiere un
  bundle macOS firmado con identidad estable.
- `OSIntegration` expone estados observables y no permite que QML llame APIs
  de AppKit, ServiceManagement o UserNotifications directamente.
- La preferencia local de inicio automatico y el estado efectivo reportado por
  macOS se almacenan y muestran por separado.
- Solicitar permiso de notificaciones requiere una accion explicita del
  usuario. Un permiso denegado no impide usar ventana, menu bar o worker.

### Estados del contrato

```text
LaunchContext: Manual | LoginItem
LoginItemStatus: Disabled | Enabled | RequiresApproval | Rejected | Unavailable
NotificationStatus: NotDetermined | Granted | Denied | Unavailable
```

El adaptador emite intenciones de mostrar, ocultar, enfocar, crear una nueva
solicitud, pausar/reanudar y salir. `AppLifecycleController` las conecta con
servicios de aplicacion y navegacion; `MacOSIntegration` no modifica solicitudes
ni escribe SQLite directamente.

## Pendientes no bloqueantes

- Confirmar en la configuracion de distribucion la identidad de firma y el
  `CFBundleIdentifier` definitivo antes de la prueba empaquetada de
  `SMAppService`.
- Ajustar textos visibles del menu bar y de los estados de permiso a la guia
  visual de la aplicacion.

## Restricciones tecnicas

- La app sigue siendo un solo proceso local.
- `OSIntegration` es el contrato; `MacOSIntegration` contiene detalles macOS.
- La UI no llama directamente APIs de AppKit, Login Item o notificaciones.
- El menu bar no modifica directamente estados de solicitudes; emite comandos a servicios o view models.
- La denegacion de notificaciones no debe impedir abrir la app ni ejecutar el worker.

## Criterios de aceptacion

- [x] Dado un arranque manual, cuando inicia la app, entonces crea el menu bar,
  cambia a foreground y muestra/enfoca la ventana principal.
- [x] Dado un arranque por Login Item, cuando inicia la app, entonces crea el
  menu bar, mantiene el proceso activo y no muestra la ventana.
- [x] Dado un proceso primario activo, cuando se abre el mismo bundle otra vez,
  entonces la segunda instancia envia `ActivateWindow` y termina, y la primaria
  muestra/enfoca su ventana sin crear un segundo grafo funcional.
- [x] Dado un canal de instancia obsoleto, cuando se confirma que no existe el
  proceso primario, entonces la nueva instancia puede asumir el rol primario.
- [x] Dada una ventana visible, cuando el usuario la cierra, entonces se oculta
  y el proceso, menu bar y worker permanecen activos.
- [x] Dado el menu bar, cuando se elige Mostrar ventana o Nueva solicitud,
  entonces la ventana se muestra/enfoca y Nueva solicitud navega a la ruta QML
  existente.
- [x] Dado el menu bar, cuando se elige Pausar/Reanudar, entonces se emite el
  comando al servicio de aplicacion y el estado persistido se actualiza tras
  confirmar el resultado.
- [x] Dado el menu bar, cuando se elige Salir, entonces se detienen worker y
  menu bar y termina el proceso; cerrar la ventana por si solo no lo termina.
- [x] Dada una instalacion nueva, entonces el Login Item permanece deshabilitado
  por defecto y la preferencia local es `false`.
- [x] Dado cualquier estado efectivo de Login Item, entonces la UI distingue la
  preferencia solicitada de `Disabled`, `Enabled`, `RequiresApproval`,
  `Rejected` o `Unavailable`.
- [x] Dado un permiso de notificaciones `NotDetermined`, cuando el usuario
  solicita permiso, entonces se consulta a macOS y se expone el estado real.
- [x] Dado un permiso `Denied`, cuando se intenta enviar una notificacion,
  entonces se muestra que esta deshabilitada y la app sigue operativa.
- [ ] Dado un permiso `Granted`, cuando se emite la notificacion de prueba,
  entonces macOS recibe el titulo y cuerpo esperados.
- [x] Dado `FakeOSIntegration`, entonces las pruebas cubren arranque manual,
  Login Item, segunda apertura, cierre de ventana, salida, estados de permiso
  y fallos sin SAT, Keychain ni credenciales reales.
- [x] La implementacion no crea daemon, LaunchAgent ni helper independiente.

## Verificacion

1. Compilar y abrir el bundle macOS.
2. Probar apertura manual con ventana visible.
3. Cerrar y reabrir desde menu bar.
4. Intentar abrir una segunda vez y verificar el foco de la instancia existente.
5. Probar salida explicita y confirmar que el proceso termina.
6. Habilitar y deshabilitar Login Item, incluyendo rechazo o estado no disponible.
7. Probar arranque automatico sin abrir ventana.
8. Probar notificaciones con permiso concedido y denegado.
9. Ejecutar las pruebas automatizadas con `FakeOSIntegration` y `QSignalSpy` sin
   esperas no deterministas.
10. Repetir la apertura concurrente del bundle para verificar que no se duplique
    el proceso funcional ni el menu bar.

## Definicion de terminado

- El ciclo de vida macOS funciona en una build empaquetada.
- El contrato `OSIntegration` esta conectado al adaptador macOS.
- Menu bar, ventana, Login Item y notificaciones tienen estados observables.
- Los escenarios de permiso denegado y proceso existente tienen comportamiento definido.
- El worker futuro puede usar la integracion sin conocer APIs macOS concretas.

## Resultado

Completada el 2026-10-04, con un criterio bloqueado por firma de codigo.

- `OSIntegration` como `QObject` en ports; `MacOSIntegration` (Objective-C++) en el target `satcfdi_os_macos`.
- Instancia unica con `QLocalServer` y `QLockFile`, resuelta antes del bootstrap SQLite; probada con arranques simultaneos.
- `AppLifecycleController`: cerrar ventana oculta, menu bar, pausa persistida, salida ordenada con `ultimo_cierre_en`.
- Login Item con `SMAppService.mainApp`; preferencia y estado efectivo separados en el menu bar.
- Bundle id `mx.adenium.satcfdi-downloader`, con migracion automatica del valor anterior en cache.
- `ctest`: os_macos, unit, infrastructure, integration y presentation pasan.
- Prueba manual: ventana, menu bar, segunda apertura, pausa, salida, Login Item habilitado y arranque por Login Item sin ventana.
- Bloqueado: entrega de notificacion con permiso concedido; con firma ad-hoc macOS no registra la app en notificaciones. Requiere firma estable (distribucion).
- Riesgos: logout puede abortarse al interceptar la salida; Login Item ad-hoc ligado al build; la segunda apertura no recibe acuse si la primaria falla en bootstrap.

## Riesgos y notas

- El comportamiento de Login Item y notificaciones depende de permisos y configuracion del sistema.
- Esta tarea no prueba el flujo SAT; solo la frontera del sistema operativo.
- El worker y las notificaciones de negocio se conectaran en tareas posteriores.
- La prueba real de Login Item requiere un bundle firmado y una identidad de
  aplicacion estable; los estados rechazado/no disponible se cubren de forma
  determinista con el fake cuando macOS no permita inducirlos.

## Referencias

- `T002-shell-qt-qml.md`
- `T003-persistencia-local.md`
- `docs/architecture.md`
- ADR 0003
- ADR 0009
