# T007: Worker y ejecutor serial

## Estado

Pendiente

## Prioridad y tamano

- Prioridad: Alta.
- Tamano: Grande.

## Objetivo

Implementar el monitoreo local y el ejecutor serial que coordinara operaciones automaticas y acciones manuales fuera del hilo grafico.

## Contexto

El worker debe revisar solicitudes y paquetes pendientes mientras la app esta abierta, pero no debe consultar ni descargar cuando el monitoreo esta pausado. La UI y el worker pueden solicitar operaciones sobre los mismos registros; un ejecutor serial debe evitar carreras y aplicar las reglas de recuperacion.

## Alcance

### Incluye

- `WorkerLocal` dentro del proceso de la app.
- `OperacionExecutor` fuera del hilo grafico.
- Intervalo inicial fijo de 10 minutos.
- Backoff a 30 minutos despues de tres verificaciones consecutivas sin cambio.
- Pausa persistente en `ConfiguracionApp`.
- Acciones pendientes `verificacion_pendiente` y `descarga_pendiente`.
- Recuperacion de estados `Enviando` y `Descargando` al arrancar.
- Coordinacion con `FakeSatGateway` y `FakePackageStorage`.
- Resultados y errores publicados hacia view models mediante signals/slots seguros.

### No incluye

- Llamadas SAT reales.
- Reenvio de `EnvioFallido` o `EnvioIncierto`.
- Cola persistente independiente.
- Daemon, LaunchAgent o proceso separado.
- Notificaciones de negocio.
- Escritura y reconciliacion real de ZIP; corresponde a `T008`.

## Dependencias

- `T003-persistencia-local.md`.
- `T004-ciclo-vida-macos.md`.
- `T002-shell-qt-qml.md` para consumir los contratos iniciales de `satcfdi_ports`.
- ADR 0007 y ADR 0014.
- `docs/design/operational-rules.md`.

Esta tarea puede ejecutarse con fakes y no depende de `T006`. La integracion con `SatGateway` real se realiza en `T009`.

Los fakes deben implementar los contratos definidos en `T002`; los DTOs concretos de `SatGateway` pueden ajustarse despues de `T006`.

## Trabajo esperado

1. Definir los tipos de operacion que puede recibir `OperacionExecutor`.
2. Crear el hilo o mecanismo de ejecucion serial fuera del hilo grafico.
3. Implementar seleccion de solicitudes verificables y paquetes descargables simulados.
4. Implementar el intervalo de 10 minutos y el backoff por solicitud.
5. Implementar pausa/reanudacion y persistencia de la configuracion.
6. Implementar acciones manuales que se ejecutan o quedan pendientes segun la pausa.
7. Consumir y limpiar las banderas pendientes despues de cada operacion.
8. Implementar recuperacion al arrancar para estados de envio y descarga.
9. Verificar `eliminado_en` antes de aplicar resultados asincronos.
10. Agregar tests de aplicacion con reloj y scheduler controlables.

## Reglas de ejecucion

- Worker y acciones manuales solo solicitan operaciones; `OperacionExecutor` ejecuta llamadas, transacciones y cambios criticos.
- Solo una operacion critica se ejecuta a la vez.
- El ejecutor usa su propia conexion SQLite.
- La UI recibe resultados en el hilo grafico y no comparte objetos de persistencia.
- Si el monitoreo esta pausado, verificar y reintentar descarga solo establecen su bandera pendiente.
- `verificacion_pendiente` y `descarga_pendiente` pueden coexistir.
- `EnvioFallido`, `EnvioIncierto` y errores de paquete no se reintentan automaticamente.
- Los paquetes `Descargado` se ignoran.
- Una solicitud o paquete eliminado durante una operacion no debe recibir resultados posteriores.

## Decisiones que debe cerrar esta tarea

- Abstraccion del reloj para no esperar diez minutos en tests.
- Politica de prioridad cuando coinciden verificacion y descarga pendientes.
- Forma de detener el worker al salir de la app.
- Estado que se publica al menu bar cuando el worker esta pausado, ejecutando o detenido.
- Como registrar interrupciones de envio y descarga sin duplicar logs.

## Criterios de aceptacion

- [ ] El worker se ejecuta dentro del proceso principal y fuera del hilo grafico.
- [ ] Una solicitud pendiente se selecciona para verificacion con el intervalo de 10 minutos.
- [ ] Despues de tres verificaciones sin cambio, la siguiente programacion usa 30 minutos.
- [ ] Una verificacion con cambio reinicia el intervalo definido.
- [ ] Al pausar el monitoreo no se ejecutan verificaciones ni descargas automaticas.
- [ ] Las acciones manuales solicitadas durante pausa quedan en las banderas correspondientes.
- [ ] Las dos banderas pueden coexistir y se limpian despues de procesarse.
- [ ] Worker y accion manual no ejecutan dos operaciones criticas concurrentes.
- [ ] `EnvioFallido` y `EnvioIncierto` no se reenvian.
- [ ] Un paquete `Descargado` no vuelve a seleccionarse.
- [ ] Un estado `Enviando` se recupera como `EnvioIncierto` al reiniciar.
- [ ] Un estado `Descargando` sin evidencia de archivo final se recupera como `Disponible`.
- [ ] Un resultado asincrono no modifica una solicitud o paquete eliminado.
- [ ] Las operaciones SQLite del ejecutor usan una conexion propia del hilo.
- [ ] La UI permanece responsiva durante operaciones simuladas lentas.
- [ ] Los tests no esperan tiempo real y cubren scheduler, pausa, backoff y recuperacion.

## Verificacion

1. Usar `FakeSatGateway` para simular solicitudes sin cambio, cambio de estado, error y timeout.
2. Usar un reloj controlable para avanzar 10 y 30 minutos sin esperar.
3. Probar pausa, reanudacion y ambas acciones pendientes.
4. Lanzar simultaneamente una accion manual y un ciclo del worker y verificar serializacion.
5. Marcar una solicitud eliminada mientras una operacion esta pendiente y verificar que el resultado se descarte.
6. Simular reinicio con estados `Enviando` y `Descargando`.
7. Ejecutar `ctest --test-dir build`.

## Definicion de terminado

- El worker y el ejecutor serial funcionan con fakes.
- Las reglas de pausa, backoff, acciones pendientes y recuperacion estan cubiertas por tests.
- Ninguna operacion bloquea el hilo grafico ni comparte conexiones SQLite entre hilos.
- Las acciones manuales y automaticas siguen una unica frontera de ejecucion.
- `T009` puede reemplazar los fakes por adaptadores reales sin cambiar la coordinacion.

## Resultado

Pendiente.

## Riesgos y notas

- Esta tarea no prueba la firma ni el comportamiento real del SAT.
- El intervalo fijo no debe probarse esperando tiempo real; se debe inyectar reloj.
- Para uso personal, un ejecutor serial global es suficiente y evita introducir una cola persistente.

## Referencias

- `T003-persistencia-local.md`
- `T004-ciclo-vida-macos.md`
- `docs/design/operational-rules.md`
- `docs/design/qt-project-structure.md`
- ADR 0007
- ADR 0014
