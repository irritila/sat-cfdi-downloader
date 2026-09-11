# T004: Ciclo de vida macOS y menu bar

## Estado

Pendiente

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

## Trabajo esperado

1. Implementar el adaptador macOS detras del contrato `OSIntegration`.
2. Crear el icono y menu de menu bar desde C++.
3. Conectar mostrar/ocultar ventana y salida explicita con la ventana QML.
4. Implementar la señal o mecanismo de activacion cuando se abre una segunda vez la app.
5. Integrar el Login Item sin crear un proceso auxiliar.
6. Persistir la preferencia solicitada y consultar el estado efectivo reportado por macOS.
7. Integrar solicitudes de permiso y estado de notificaciones.
8. Crear un fake o harness de `OSIntegration` para pruebas de aplicacion.

## Decisiones que debe cerrar esta tarea

- Version minima de macOS soportada por el MVP.
- Politica de activacion de la app al iniciar automaticamente y al abrirse manualmente.
- Mecanismo de instancia unica y reenvio de activacion.
- Comportamiento de la app cuando macOS rechaza el Login Item.
- Comportamiento visible cuando las notificaciones estan denegadas.

## Restricciones tecnicas

- La app sigue siendo un solo proceso local.
- `OSIntegration` es el contrato; `MacOSIntegration` contiene detalles macOS.
- La UI no llama directamente APIs de AppKit, Login Item o notificaciones.
- El menu bar no modifica directamente estados de solicitudes; emite comandos a servicios o view models.
- La denegacion de notificaciones no debe impedir abrir la app ni ejecutar el worker.

## Criterios de aceptacion

- [ ] La app muestra un icono en el menu bar al iniciar manualmente.
- [ ] Abrir manualmente la app muestra la ventana principal.
- [ ] Abrir manualmente la app cuando ya existe el proceso enfoca la ventana existente y no crea otra instancia funcional.
- [ ] Cerrar la ventana la oculta y mantiene vivo el proceso.
- [ ] El menu bar permite mostrar la ventana y salir explicitamente.
- [ ] Salir desde menu bar termina el proceso.
- [ ] El Login Item esta apagado por defecto.
- [ ] Al habilitar el Login Item, la preferencia local y el estado efectivo de macOS pueden distinguirse.
- [ ] Un inicio automatico muestra solo el icono de menu bar y no la ventana principal.
- [ ] Un Login Item rechazado o no disponible se muestra como estado explicito y no rompe la app.
- [ ] La app puede solicitar permiso de notificaciones y conserva el funcionamiento si el permiso es denegado.
- [ ] Una notificacion de prueba puede emitirse cuando el permiso esta concedido.
- [ ] La implementacion no crea daemon, LaunchAgent ni helper independiente.
- [ ] Las pruebas no dependen de un worker SAT ni de credenciales reales.

## Verificacion

1. Compilar y abrir el bundle macOS.
2. Probar apertura manual con ventana visible.
3. Cerrar y reabrir desde menu bar.
4. Intentar abrir una segunda vez y verificar el foco de la instancia existente.
5. Probar salida explicita y confirmar que el proceso termina.
6. Habilitar y deshabilitar Login Item, incluyendo rechazo o estado no disponible.
7. Probar arranque automatico sin abrir ventana.
8. Probar notificaciones con permiso concedido y denegado.

## Definicion de terminado

- El ciclo de vida macOS funciona en una build empaquetada.
- El contrato `OSIntegration` esta conectado al adaptador macOS.
- Menu bar, ventana, Login Item y notificaciones tienen estados observables.
- Los escenarios de permiso denegado y proceso existente tienen comportamiento definido.
- El worker futuro puede usar la integracion sin conocer APIs macOS concretas.

## Resultado

Pendiente.

## Riesgos y notas

- El comportamiento de Login Item y notificaciones depende de permisos y configuracion del sistema.
- Esta tarea no prueba el flujo SAT; solo la frontera del sistema operativo.
- El worker y las notificaciones de negocio se conectaran en tareas posteriores.

## Referencias

- `T002-shell-qt-qml.md`
- `T003-persistencia-local.md`
- `docs/architecture.md`
- ADR 0003
- ADR 0009

