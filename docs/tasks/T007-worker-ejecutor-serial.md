# T007: Worker y ejecutor serial

## Estado

Pendiente (refinada 2026-10-04; lista para implementar).

## Prioridad y tamano

- Prioridad: Alta.
- Tamano: Grande.

## Objetivo

Implementar el monitoreo local (`WorkerLocal`) y el ejecutor serial (`OperacionExecutor`) que coordinan envio, verificacion, descarga, vencimiento estimado, acciones pendientes y recuperacion fuera del hilo grafico. Las operaciones externas pasan por un puerto de aplicacion propio, de modo que `T009` sustituya los fakes por adaptadores reales sin cambiar la coordinacion.

## Contexto

El worker revisa solicitudes y paquetes mientras la app esta abierta, incluso con la ventana cerrada, y no consulta ni descarga con el monitoreo pausado (`ADR 0007`). La UI y el worker pueden pedir operaciones sobre los mismos registros; el ejecutor serial evita carreras y aplica transacciones y recuperacion (`ADR 0014`, `ADR 0015`, `ADR 0016`).

`SatGateway` y `PackageStorage` siguen sin metodos: `T006` fija la forma SAT con evidencia real y `T008` el almacenamiento ZIP. Por eso T007 define el puerto `OperacionesSat`, que expresa resultados ya clasificados y que `T009` implementa sobre esos contratos.

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | Al salir con una operacion en curso: detener timers, no aceptar operaciones nuevas, esperar hasta 10 s a la operacion actual, luego pedir cancelacion cooperativa y salir. La recuperacion al arrancar resuelve lo interrumpido. | Aprobada por usuario | Refinamiento 2026-10-04. Salida casi inmediata, sin dialogos. Arquitectura proponia 30 s. |
| D2 | El menu bar muestra una linea de estado (`Monitoreo activo`, `Monitoreo pausado`, `Trabajando: enviando/verificando/descargando...`) y `Pendientes: N` si hay intenciones por pausa. Los errores detallados no van al menu bar. | Aprobada por usuario | Refinamiento 2026-10-04. |
| D3 | Puerto de aplicacion `OperacionesSat`, consumido solo por `OperacionExecutor`: `enviar`, `verificar`, `descargar`, `existeArchivoFinal` y `obtenerEstadoCredencial(perfil)` (este ultimo agregado por el refinamiento de T009 para el gate por perfil). Devuelve resultados semanticos o `FallaOperacion{fase, codigo?, diagnosticoSanitizado, cancelada}` con fase `Preparacion`, `Autenticacion`, `AntesDeEnvio`, `DespuesDeEnvio`, `RespuestaExplicita` o `Almacenamiento` (fallos locales de guardado del ZIP; agregada por el refinamiento de T008, D12). No expone SOAP, token, bytes ZIP ni `MaterialFirma`. `SatGateway.h` y `PackageStorage.h` no cambian. | Recomendacion tecnica | `qt-architecture-lead`. Agregar metodos provisionales contradice T006, y esperar a T006/T008 deja a T007 sin una unidad verificable. |
| D4 | El envio de una solicitud `Creada` es un tipo de operacion de T007 y lo dispara solo el usuario despues de `crearLocal()`. El worker nunca selecciona `Creada`. | Recomendacion tecnica | `qt-architecture-lead`, `requirements.md` §8. |
| D5 | `OperacionExecutor` tiene su propio `QThread`, cola serial y conexion SQLite; no reutiliza `PersistenceDispatcher`. Cada operacion sigue tres pasos: una transaccion breve que marca `Enviando` o `Descargando`; la operacion externa, sin transaccion abierta; y una nueva transaccion `BEGIN IMMEDIATE` que aplica el resultado solo si `eliminado_en IS NULL`. | Recomendacion tecnica | `qt-architecture-lead`, `ADR 0014`, `ADR 0016`. |
| D6 | `Descargando` significa "descarga reclamada o en curso, incluida la preparacion y la autenticacion": el ejecutor lo marca antes de invocar el puerto. Una falla en `Preparacion` o `Autenticacion` lleva a `Error`. Se corrigen las frases literales de `operational-rules.md`. | Recomendacion tecnica | `qt-architecture-lead`, ronda 02. El token vive dentro del adaptador. |
| D7 | Desenlaces del envio: `5000` con `IdSolicitud` lleva a `Enviada`. Una falla en `Preparacion`, `Autenticacion` o `AntesDeEnvio` regresa atomicamente a `Creada` (limpia `envio_iniciado_en`, `cod_estatus_solicitud` y `mensaje_solicitud_sat`; guarda `ultimo_error`; log `envio_no_iniciado`) y solo se reenvia manualmente (ADR 0017, enmienda del refinamiento de T009). Un rechazo documentado sin `IdSolicitud` lleva a `EnvioFallido`. Una falla `DespuesDeEnvio`, un `5000` sin `IdSolicitud`, un `5006` o un codigo no documentado llevan a `EnvioIncierto`. Nunca hay reenvio automatico. | Recomendacion tecnica; regreso a `Creada` aprobado por usuario (T009) | `ADR 0015`, `ADR 0017`, `operational-rules.md`, T006 D5. |
| D8 | Reloj (`ahoraUtc()`) y programador inyectables. La programacion es por solicitud, con `siguiente_verificacion_en`. Una verificacion es "sin cambio" si se repiten `estado_solicitud_sat`, `codigo_estado_solicitud`, `numero_cfdi` y el conjunto de `idsPaquetes`. Un cambio pone el contador en 0 y el intervalo en 10 min; la tercera verificacion sin cambio consecutiva programa la siguiente a 30 min; una falla no cuenta como sin cambio y programa la siguiente verificacion a 30 min. Tres fallas consecutivas iguales con codigo `300`, `302`, `303` o `5004` suspenden la verificacion automatica (`siguiente_verificacion_en = NULL`, log `verificacion_suspendida`) hasta un `Verificar ahora`; las transitorias (red, `404`, Fault, `5011`) no suspenden. La racha se persiste en columnas nuevas `ultima_clave_falla_verificacion` y `fallas_verificacion_iguales` (migracion 003). Solo se crea un `LogSolicitud` nuevo cuando cambia la clave (fase, codigo); las repeticiones solo actualizan `ultimo_error`. | Recomendacion tecnica (fallas repetidas: enmienda del refinamiento de T009) | `qt-architecture-lead`, `ADR 0007`, `especialista-sat-seguridad`. |
| D9 | Orden: la recuperacion corre al arrancar, antes de cualquier ciclo. Cada ciclo procesa, en este orden, vencimientos estimados, intenciones pendientes (si el monitoreo esta activo), verificaciones debidas y descargas automaticas. Lo manual va antes que lo automatico; la verificacion antes que la descarga. Cada ciclo toma una seleccion acotada. Gate por perfil: antes de seleccionar trabajo, el ciclo consulta `obtenerEstadoCredencial(perfil)` (expuesto por `OperacionesSat`) una vez por perfil; si no esta `Lista`, omite las operaciones de ese perfil sin llamar a SAT; cuando el estado de la credencial cambia, emite un diagnostico de app y publica el evento para la UI (la notificacion nativa la agrega T009). `log_solicitud` no se usa porque el evento es por perfil. | Recomendacion tecnica | `qt-architecture-lead`. Evita que muchas verificaciones retrasen indefinidamente las descargas. |
| D10 | `vencimiento_estimado_en` se fija en `ahoraUtc() + 72 h` al insertar cada `IdPaquete` por primera vez y no se desplaza despues. Es una estimacion local, no el vencimiento SAT exacto. | Recomendacion tecnica | `qt-architecture-lead`; `web-service.md` §6.1. SAT no entrega la hora de generacion del paquete. |
| D11 | Estados del worker: `Pausado`, `ActivoEnEspera`, `Ejecutando(tipo)`, `Deteniendo`, `Detenido`. No existe un `Error` global. `OSIntegration` agrega un metodo para reflejar el estado del worker y los pendientes, y lo mapea a los textos de D2. | Recomendacion tecnica | `qt-architecture-lead`, D2. Un error pertenece a una operacion o a su log. |
| D12 | Cancelacion cooperativa: cada contexto de operacion lleva una senal de cancelacion segura entre hilos. Una operacion cancelada termina con `FallaOperacion{cancelada=true, fase}`, que se aplica como cualquier falla de esa fase (por ejemplo, `DespuesDeEnvio` en un envio lleva a `EnvioIncierto`). Si el puerto no regresa, el proceso sale y la recuperacion resuelve. | Recomendacion tecnica | Coordinador, ante el riesgo 2 de `qt-quality-engineer`. Permite probar el limite de 10 s de forma determinista. |
| D13 | Al consumir una intencion pendiente, la limpieza es condicional al `accion_pendiente_en` capturado al reclamarla, para no perder una intencion registrada mientras otra se procesa. | Recomendacion tecnica | `qt-architecture-lead`. |
| D14 | `existeArchivoFinal` solo verifica el archivo final. Si falla, el paquete conserva `Descargando` y se registra una falla de reconciliacion; nunca se infiere que el archivo no existe. | Recomendacion tecnica | `qt-architecture-lead`. Evita una descarga duplicada. |
| D15 | T007 no obtiene `MaterialFirma` ni token: quedan dentro del adaptador de `T009`. | Recomendacion tecnica | `qt-architecture-lead`, `ADR 0010`, T006 D8. |
| D16 | Se crea un ADR nuevo para la frontera `OperacionesSat` y el ciclo de vida y publicacion del worker. | Recomendacion tecnica | `qt-architecture-lead`. |

## Alcance

### Incluye

- `WorkerLocal` y `OperacionExecutor` en el proceso de la app, fuera del hilo grafico.
- El puerto `OperacionesSat` (D3) y `FakeOperacionesSat` para pruebas.
- Operaciones de envio (D4, D7), verificacion con registro de paquetes, descarga, vencimiento estimado y recuperacion al arrancar.
- Programacion de 10 y 30 min por solicitud (D8).
- Pausa persistente en `ConfiguracionApp`, ya reflejada por T004.
- Acciones manuales `Verificar ahora` y `Reintentar descarga`: se ejecutan, o quedan como `verificacion_pendiente`/`descarga_pendiente` si hay pausa.
- Logs `LogSolicitud` sanitizados de cada operacion, con `origen` igual a `worker`, `usuario` o `recuperacion`.
- Publicacion de resultados hacia los view models mediante senales encoladas, y del estado del worker al menu bar (D2, D11).
- Salida con plazo de 10 s (D1, D12) dentro del ciclo de vida de T004.
- ADR nuevo (D16) y correcciones en `docs/design/operational-rules.md` (D6, D10) y `docs/design/qt-project-structure.md`.

### No incluye

- Llamadas SAT reales, `SecretStore`, token o `MaterialFirma` (`T009`).
- Metodos en `SatGateway` (`T006`/`T009`) o en `PackageStorage` (`T008`), y la escritura o reconciliacion real de ZIP y archivos huerfanos (`T008`).
- Reenvio de `EnvioFallido` o `EnvioIncierto`; reintento automatico de paquetes en `Error`.
- Una cola persistente independiente, un daemon, un LaunchAgent o un proceso separado.
- Notificaciones de negocio (`T009`).
- Rediseno de pantallas: la UI solo conecta las acciones existentes y muestra los resultados publicados.

## Dependencias

- Migracion 003: catalogo `tipo_evento` con `envio_no_iniciado` y `verificacion_suspendida`, y columnas de racha de fallas de verificacion (D7, D8).
- `T002`, `T003` (repositorios, `UnitOfWork`, `PersistenceDispatcher`, `LogSanitizer`) y `T004` (`OSIntegration`, ciclo de vida, pausa). Las tres estan completadas.
- `ADR 0007`, `ADR 0014`, `ADR 0015`, `ADR 0016`; `docs/design/operational-rules.md`.
- No depende de `T006` ni de `T008`: el puerto D3 los desacopla. `T009` implementa el adaptador real.

## Reglas de ejecucion

- El worker y las acciones manuales solo encolan operaciones. `OperacionExecutor` ejecuta las llamadas, las transacciones y los cambios criticos.
- Solo una operacion critica se ejecuta a la vez.
- Ninguna transaccion queda abierta durante una llamada al puerto.
- Antes de aplicar un resultado, se revisa `eliminado_en` dentro de la misma transaccion.
- Solo la verificacion escribe `estado_solicitud_sat` (`ADR 0015`). Los estados `ErrorSat`, `Rechazada`, `Vencida`, `EnvioFallido` y `EnvioIncierto` son terminales para el worker.
- Los paquetes `Descargado` y `Vencido` se ignoran. Un paquete en `Error` solo se reintenta por accion manual.
- Con pausa, el worker no llama al puerto. Las acciones manuales que requieren SAT solo activan su bandera, y ambas banderas pueden coexistir.
- Una intencion que ya no aplica se limpia y deja un log `accion_pendiente_descartada`.
- Una interrupcion produce exactamente un log por operacion afectada. Un cierre normal no genera logs por solicitud.

## Trabajo esperado

1. Redactar el ADR de la frontera `OperacionesSat` y del ciclo de vida del worker. Actualizar `operational-rules.md` (significado de `Descargando`, secuencia de descarga, `vencimiento_estimado_en`) y la seccion de hilos de `qt-project-structure.md`.
2. Definir en `application` el puerto `OperacionesSat` con sus contextos, resultados, `FallaOperacion` y la cancelacion cooperativa.
3. Implementar `OperacionExecutor` con su hilo, cola, conexion SQLite, transacciones en tres pasos y publicacion de resultados.
4. Implementar `WorkerLocal` con reloj y programador inyectables, la seleccion acotada y el orden del ciclo.
5. Implementar las politicas de transicion de envio, verificacion (incluido el registro de paquetes y de vencimiento SAT en la misma transaccion), descarga, vencimiento estimado y recuperacion.
6. Implementar las acciones manuales y las intenciones pendientes con limpieza condicional.
7. Agregar a `OSIntegration` el reflejo del estado del worker y los pendientes (adaptador macOS y `FakeOSIntegration`), e integrar la salida con plazo en `AppLifecycleController`.
8. Conectar en el composition root `FakeOperacionesSat` o un adaptador nulo seguro mientras no exista `T009`. El adaptador nulo devuelve `FallaOperacion` en `Preparacion`, para que la app real nunca simule exito.
9. Escribir las pruebas descritas en Verificacion.

## Criterios de aceptacion

### Programacion y pausa

- [ ] Dada una solicitud verificable con `siguiente_verificacion_en` vencida, cuando el reloj logico avanza al instante debido, entonces se encola una sola verificacion.
- [ ] Dadas tres verificaciones consecutivas sin cambio (D8), cuando termina la tercera, entonces `siguiente_verificacion_en` queda a 30 min. Dado ese backoff, cuando una verificacion cambia alguno de los cuatro campos, entonces el contador vuelve a 0 y la siguiente queda a 10 min.
- [ ] Dadas verificaciones sin cambio acumuladas, cuando una verificacion falla, entonces el contador no se incrementa y la siguiente queda a 30 min.
- [ ] Dadas tres fallas consecutivas iguales `300`, `302`, `303` o `5004`, cuando ocurre la tercera, entonces la verificacion automatica se suspende hasta `Verificar ahora`, aun tras reiniciar la app. Las fallas repetidas con la misma clave no crean logs nuevos.
- [ ] Dado un perfil con credencial no `Lista`, cuando corre un ciclo con varias solicitudes de ese perfil, entonces se consulta una sola vez y ninguna operacion de ese perfil llega al puerto.
- [ ] Dado el monitoreo pausado, cuando vence la agenda automatica, entonces el puerto no recibe llamadas.
- [ ] Dado el monitoreo pausado, cuando el usuario pide `Verificar ahora` y `Reintentar descarga`, entonces ambas banderas persisten a la vez con `accion_pendiente_en`. Cuando se reanuda, se consumen en el orden verificar y luego descargar, y cada una se limpia solo despues de su operacion, o con un log de descarte si ya no aplica.
- [ ] Dada una intencion registrada mientras se procesa otra de la misma solicitud, cuando termina la primera, entonces la nueva intencion no se pierde.

### Operaciones y transiciones

- [ ] Dada una solicitud `Creada` enviada por el usuario, cuando el fake responde con cada caso de D7, entonces el estado final es `Enviada`, `Creada` (con `envio_iniciado_en` nulo y CHECKs vigentes), `EnvioFallido` o `EnvioIncierto` segun corresponda, con su log, y nunca se reenvia automaticamente.
- [ ] Dado el ciclo del worker, cuando existen solicitudes `Creada`, entonces el worker no las selecciona.
- [ ] Dada una verificacion `Terminada`, cuando se aplica, entonces la solicitud y todos sus paquetes nuevos (con `vencimiento_estimado_en` segun D10) se registran en la misma transaccion. Dada una verificacion `Vencida`, entonces los paquetes no descargados pasan a `Vencido` con origen `SAT`.
- [ ] Dado un paquete descargable, cuando se descarga, entonces: con el archivo final confirmado pasa a `Descargado`; con una falla en `Preparacion`, `Autenticacion` u otra fase pasa a `Error`; con `5007` pasa a `Vencido` (origen SAT, motivo `paquete_expirado`); con `5008` pasa a `Error`; con una falla `Almacenamiento` (incluida `ColisionDestino`) pasa a `Error` con un log `descarga_fallida` saneado; con exito y `advertenciaDurabilidad` pasa a `Descargado` y su log registra la advertencia. La solicitud no cambia su estado SAT.
- [ ] Dado un paquete `Disponible`, `Error` o `Descargando` con `vencimiento_estimado_en` vencido, cuando corre el ciclo, entonces queda `Vencido` con origen `estimacion_local` y un log.

### Recuperacion y eliminacion

- [ ] Dada una solicitud en `Enviando` al arrancar, cuando corre la recuperacion, entonces queda `EnvioIncierto` con un solo log.
- [ ] Dado un paquete en `Descargando` al arrancar: si no hay archivo final, vuelve a `Disponible` con un log `descarga_interrumpida`; si `existeArchivoFinal` confirma el archivo, queda `Descargado` con un log `paquete_reconciliado`; si `existeArchivoFinal` falla, conserva `Descargando` y registra la falla.
- [ ] Dada una solicitud eliminada mientras su operacion esta bloqueada en el fake, cuando el fake responde, entonces ni la solicitud ni sus paquetes cambian y no se registra un resultado posterior.

### Concurrencia, hilos y ciclo de vida

- [ ] Dadas una accion manual y un ciclo del worker encolados a la vez, cuando se procesan, entonces el fake nunca observa mas de una operacion activa y la manual se ejecuta primero.
- [ ] Dadas escrituras simultaneas de `OperacionExecutor` y `PersistenceDispatcher` sobre SQLite real, cuando coinciden, entonces cada una usa su propia conexion y la base queda consistente (sin `database is locked` no manejado).
- [ ] Dado el puerto bloqueado, cuando se encola un evento en el hilo grafico, entonces se procesa antes de liberar el puerto.
- [ ] Dada una salida con una operacion activa, cuando el reloj logico llega a 9.999 s, entonces el sistema sigue esperando y rechaza operaciones nuevas. A los 10 s, pide la cancelacion cooperativa, aplica la falla resultante segun D12 y termina el ejecutor antes que el dispatcher y el menu bar.
- [ ] Dada una interrupcion por salida, aunque la senal de cierre se repita, entonces se registra exactamente un log por operacion interrumpida. Un cierre sin operaciones no genera logs por solicitud.
- [ ] Dados los cambios de estado del worker (`Pausado`, `ActivoEnEspera`, `Ejecutando(tipo)`, `Deteniendo`, `Detenido`) y de pendientes, cuando ocurren, entonces `OSIntegration` recibe el estado y el conteo correspondientes, y el menu bar muestra los textos de D2.
- [ ] Sin `T009`, cuando la app real intenta una operacion, entonces el adaptador nulo produce una falla `Preparacion` visible y nunca un exito simulado.

### Documentacion

- [ ] El ADR nuevo existe y `operational-rules.md` y `qt-project-structure.md` reflejan D6, D10 y el hilo del ejecutor, sin contradicciones con ADR 0014 y 0015.

## Verificacion

1. `ctest --test-dir build --output-on-failure`, con las pruebas repartidas en los labels existentes:
   - `unit`: politicas de agenda, transiciones, prioridades, limpieza condicional y cola, con reloj y programador falsos.
   - `infrastructure`: SQLite real en `QTemporaryDir` con conexiones del ejecutor y del dispatcher.
   - `integration` y `presentation`: ciclo de vida, salida, responsividad y menu bar con `FakeOSIntegration` y `QSignalSpy`.
2. `FakeOperacionesSat` registra el orden, la concurrencia y las fases, y admite respuestas bloqueantes con barrera y cancelacion. No se usa `PromesasPendientes` entre hilos, porque es exclusivo del hilo grafico.
3. Ninguna prueba espera tiempo real (ni `sleep` ni `qWait` para intervalos de 10 o 30 min, ni para el plazo de 10 s).
4. Prueba manual: abrir la app con datos de ejemplo y el adaptador nulo, pausar y reanudar desde el menu bar, y observar los textos de estado y la ausencia de bloqueo de la UI.

## Definicion de terminado

- El worker y el ejecutor funcionan con fakes, y las reglas de agenda, pausa, intenciones, transiciones, recuperacion, eliminacion y salida estan cubiertas por pruebas.
- Ninguna operacion bloquea el hilo grafico ni comparte conexiones SQLite entre hilos.
- Las acciones manuales y automaticas pasan por una unica frontera de ejecucion.
- `T009` solo debe implementar `OperacionesSat` sobre `SatGateway`, `PackageStorage` y `SecretStore`, sin cambiar la coordinacion.

## Resultado

Pendiente.

## Riesgos y notas

- Esta tarea no prueba la firma ni el comportamiento real del SAT. La clasificacion de fases y codigos puede requerir ajustes menores en el adaptador de `T009` tras la evidencia de `T006`, no en el ejecutor.
- `vencimiento_estimado_en` es una aproximacion: puede marcar `Vencido` un paquete que SAT aun conserve (o al reves). La verificacion o la descarga SAT siguen siendo la fuente autoritativa.
- Agregar un metodo a `OSIntegration` cambia un contrato de T004; el adaptador macOS y el fake deben actualizarse juntos.
- Un ejecutor serial global es suficiente para el uso personal; si limita, puede evolucionar a serializacion por solicitud con un ADR.

## Referencias

- `docs/design/operational-rules.md`, `docs/design/qt-project-structure.md`, `docs/requirements.md` §8
- `ADR 0007`, `ADR 0014`, `ADR 0015`, `ADR 0016`
- `T003`, `T004`, `T006` (D5, D8), `T008`, `T009`
- `docs/meetings/T007-refinamiento/` (bitacora y rondas del refinamiento)
