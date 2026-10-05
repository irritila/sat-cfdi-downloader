# ADR 0018: Frontera de operaciones SAT y ciclo de vida del worker

## Estado

Accepted

## Contexto

T007 incorpora el monitoreo local y las acciones manuales que pueden operar
sobre una misma solicitud o paquete. `SatGateway` y `PackageStorage` aun no
fijan metodos para esos flujos, y el material de firma, el token y los bytes
ZIP no deben salir de sus adaptadores. A la vez, `PersistenceDispatcher` ya
tiene un hilo propio para persistencia general y no puede ser la frontera que
serialice llamadas SAT, archivos y transiciones operativas.

Se necesita una frontera verificable con fakes que permita a T009 conectar los
adaptadores reales sin cambiar la coordinacion de T007. Tambien se deben hacer
observables el ciclo del worker, su salida y la prioridad entre trabajo manual
y automatico.

## Decision

### Frontera `OperacionesSat`

Se define `OperacionesSat` en `application`. Solo `OperacionExecutor` la
consume; `WorkerLocal` y las acciones manuales solo encolan trabajo en el
ejecutor.

El puerto expone `enviar`, `verificar`, `descargar`, `existeArchivoFinal` y
`obtenerEstadoCredencial(perfil)`. Sus contextos contienen solo los datos
necesarios para la operacion y una senal de cancelacion segura entre hilos.
Devuelve resultados semanticos o
`FallaOperacion{fase, codigo?, diagnosticoSanitizado, cancelada}`. Las fases
son `Preparacion`, `Autenticacion`, `AntesDeEnvio`, `DespuesDeEnvio`,
`RespuestaExplicita` y `Almacenamiento`.

El puerto no expone SOAP, token, `MaterialFirma`, bytes ZIP ni detalles de los
adaptadores. T009 implementara esta frontera sobre `SatGateway`,
`PackageStorage` y `SecretStore`, sin cambiar la cola, las transacciones, la
recuperacion ni los disparadores del ejecutor. `existeArchivoFinal` comprueba
solo el archivo final; una falla al comprobarlo no se interpreta como ausencia.

### Ejecucion, persistencia y cancelacion

`OperacionExecutor` tiene su propio `QThread`, cola serial y conexion SQLite.
No reutiliza `PersistenceDispatcher`; cada uno crea, usa, cierra y retira su
propia conexion en su hilo propietario, conforme ADR 0016.

Cada operacion critica sigue tres pasos:

1. Una transaccion breve reclama el trabajo y marca `Enviando` o
   `Descargando` cuando corresponda.
2. La llamada a `OperacionesSat` ocurre sin una transaccion SQLite abierta.
3. Una nueva transaccion `BEGIN IMMEDIATE` aplica el resultado solo si
   `eliminado_en IS NULL`.

Las fallas se aplican segun su fase. Para envio, `Preparacion`,
`Autenticacion` y `AntesDeEnvio` regresan atomica y manualmente reenviable a
`Creada`, tal como fija ADR 0017. Una cancelacion cooperativa produce
`FallaOperacion{cancelada=true, fase}` y se aplica con la misma regla de la
fase. Si el puerto no retorna durante el cierre, el proceso sale y la
recuperacion posterior resuelve el estado interrumpido.

Al solicitar salida se detienen los timers y no se aceptan operaciones nuevas.
La operacion actual puede terminar durante un maximo de 10 s; al vencerlo se
pide cancelacion cooperativa. El ejecutor termina antes que
`PersistenceDispatcher` y el menu bar. Un cierre normal no crea logs por
solicitud; una interrupcion crea exactamente uno por operacion afectada.

### Worker, agenda y publicacion

`WorkerLocal` publica `Pausado`, `ActivoEnEspera`, `Ejecutando(tipo)`,
`Deteniendo` o `Detenido`; no existe un estado global `Error`. Los resultados
de operaciones y estos cambios cruzan al hilo grafico mediante senales
encoladas. `OSIntegration` recibe el estado y el numero de intenciones
pendientes para mostrar `Monitoreo activo`, `Monitoreo pausado` o
`Trabajando: ...`, mas `Pendientes: N` cuando aplique.

La recuperacion corre antes de cualquier ciclo. Con monitoreo activo, cada
ciclo de seleccion acotada procesa vencimientos estimados, intenciones
pendientes, verificaciones debidas y descargas automaticas, en ese orden. Lo
manual precede a lo automatico y la verificacion precede a la descarga.

La agenda es por solicitud con `siguiente_verificacion_en`. Una verificacion
sin cambio repite `estado_solicitud_sat`, `codigo_estado_solicitud`,
`numero_cfdi` y el conjunto de `idsPaquetes`: la tercera consecutiva mueve la
siguiente a 30 min; un cambio reinicia el contador y programa 10 min. Una falla
no cuenta como sin cambio y programa 30 min. Tres fallas consecutivas iguales
con codigo `300`, `302`, `303` o `5004` suspenden la verificacion automatica
hasta `Verificar ahora`; red, `404`, Fault y `5011` no la suspenden.

Antes de seleccionar trabajo de un perfil, el ejecutor consulta una vez
`obtenerEstadoCredencial(perfil)`. Si no esta `Lista`, omite ese perfil sin
llamar a SAT; un cambio se publica como evento de aplicacion para la UI. Esta
consulta no registra `LogSolicitud`, porque el evento pertenece al perfil.

Esta decision precisa y amplia las reglas aun vigentes de ejecutor,
transacciones y recuperacion de ADR 0014. No modifica las dimensiones de
estado, eliminacion local ni vencimientos de ADR 0015, y mantiene la enmienda
de ADR 0017 para los fallos previos al envio.

## Consecuencias

- T007 puede probar coordinacion, cancelacion y transiciones con
  `FakeOperacionesSat`, sin fingir exitos en la app productiva.
- T009 debe adaptar recursos y respuestas reales a `OperacionesSat`; los
  secretos y datos sensibles quedan encapsulados en ese adaptador.
- Existen dos hilos de escritura SQLite con conexiones distintas; sus
  transacciones breves, `busy_timeout` y `BEGIN IMMEDIATE` deben manejar la
  contencion sin compartir objetos SQL.
- `Descargando` se reclama antes de invocar el puerto e incluye preparacion y
  autenticacion; las reglas operativas se actualizan en consecuencia.
- Reloj y programador son inyectables para verificar agenda y plazo de salida
  sin depender de tiempo real.

## Referencias

- `docs/tasks/T007-worker-ejecutor-serial.md` (D1-D16)
- `docs/meetings/T007-refinamiento/`
- ADR 0007, ADR 0014, ADR 0015, ADR 0016 y ADR 0017
- `docs/design/operational-rules.md`
- `docs/design/qt-project-structure.md`
