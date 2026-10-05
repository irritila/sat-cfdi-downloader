# Reglas operativas

Estado: actualizado tras refinamiento T001.

## Objetivo

Cerrar las reglas que afectan persistencia, concurrencia y recuperacion antes de fijar tablas SQLite.

Este documento no agrega funcionalidades al MVP. Solo precisa como se ejecutan y recuperan los flujos ya definidos: crear solicitud, verificar estado, registrar paquetes, descargar ZIP, ejecutar acciones manuales y eliminar registros locales.

## Principios

- Uso personal local-first: un solo proceso de app y un solo usuario local.
- Todas las operaciones criticas pasan por un ejecutor serial.
- La UI nunca aplica directamente respuestas SAT ni escribe estados persistidos.
- Los codigos SAT se guardan sin mezclar respuesta de creacion, estado de verificacion y descarga.
- El estado SAT y el ciclo de vida local se persisten por separado.
- Las transacciones locales deben mantener coherentes `SolicitudMasiva`, `PaqueteSolicitud` y `LogSolicitud`.
- Los ZIP se escriben con archivo temporal y rename atomico antes de marcar `Descargado`.

## Operaciones SAT del MVP

| UI | Operacion SAT | Regla |
| --- | --- | --- |
| `emitidos` | `SolicitaDescargaEmitidos` | `RfcEmisor = PerfilSat.rfc`, `RfcSolicitante = PerfilSat.rfc`. |
| `emitidos` + RFC contraparte | `SolicitaDescargaEmitidos` | Agrega `RfcReceptores/RfcReceptor = RFC contraparte`. |
| `recibidos` | `SolicitaDescargaRecibidos` | `RfcReceptor = PerfilSat.rfc`, `RfcSolicitante = PerfilSat.rfc`. |
| `recibidos` + RFC contraparte | `SolicitaDescargaRecibidos` | Agrega `RfcEmisor = RFC contraparte`. |

Valores fijos del MVP:

- `TipoSolicitud = CFDI`.
- `EstadoComprobante = Vigente`.

No se implementan `SolicitaDescargaFolio`, `Metadata`, `RfcACuentaTerceros` ni XML cancelados.

## Codigos SAT separados

`SolicitudMasiva` debe separar:

| Campo logico | Origen SAT | Uso |
| --- | --- | --- |
| `cod_estatus_solicitud` | Respuesta de crear solicitud | Resultado de `SolicitaDescargaEmitidos` o `SolicitaDescargaRecibidos`. |
| `mensaje_solicitud_sat` | Respuesta de crear solicitud | Mensaje asociado a creacion/rechazo inicial. |
| `estado_solicitud_sat` | Verificacion | Valor `1..6` de `EstadoSolicitud`. |
| `codigo_estado_solicitud` | Verificacion | Codigo operativo de verificacion, por ejemplo `5000`, `5001`, `5002`, `5005`. |
| `mensaje_verificacion_sat` | Verificacion | Mensaje asociado a la ultima verificacion. |

`PaqueteSolicitud` debe separar:

| Campo logico | Origen SAT | Uso |
| --- | --- | --- |
| `codigo_descarga_sat` | Respuesta de descarga | Resultado de `Descargar`. |
| `mensaje_descarga_sat` | Respuesta de descarga | Mensaje asociado a descarga. |

`LogSolicitud.codigo_sat` puede guardar cualquiera de esos codigos, siempre con `tipo_evento`, `origen` y `origen_codigo_sat` para indicar si corresponde a creacion, verificacion o descarga.

## Estados de SolicitudMasiva

`SolicitudMasiva` conserva dos dimensiones distintas:

- `estado_local`: ciclo previo o posterior a la llamada SAT (`Creada`, `Enviando`, `Enviada`, `EnvioFallido` o `EnvioIncierto`).
- `estado_solicitud_sat`: estado devuelto por SAT (`Aceptada`, `EnProceso`, `Terminada`, `Error`, `Rechazada` o `Vencida`). Es nulo mientras SAT no haya devuelto un estado.

La UI puede mostrar un estado resumido, pero no debe persistir una sola columna que mezcle ambas dimensiones.

En las tablas siguientes, los estados mostrados como `Aceptada`, `EnProceso`, `Terminada`, `ErrorSat`, `Rechazada` y `Vencida` son valores de presentacion derivados de `estado_solicitud_sat`; `Creada`, `Enviando`, `Enviada`, `EnvioFallido` y `EnvioIncierto` provienen de `estado_local`.

| Estado | Significado | Tiene `IdSolicitud` SAT | Worker automatico | Acciones manuales validas |
| --- | --- | --- | --- | --- |
| `Creada` | Registro local creado antes de enviar a SAT. | No | Puede enviar si no hay intento iniciado. | Eliminar local. |
| `Enviando` | La app inicio envio a SAT y espera resultado. | No | No iniciar otro envio concurrente. | Eliminar local, con advertencia. |
| `Enviada` | SAT devolvio `IdSolicitud` con `CodEstatus=5000`. | Si | Verificar. | Verificar ahora, eliminar local. |
| `Aceptada` | SAT reporto `EstadoSolicitud=1`. | Si | Verificar. | Verificar ahora, eliminar local. |
| `EnProceso` | SAT reporto `EstadoSolicitud=2`. | Si | Verificar. | Verificar ahora, eliminar local. |
| `Terminada` | SAT reporto `EstadoSolicitud=3`; paquetes deben estar registrados. | Si | Descargar paquetes pendientes; verificar solo si hace falta reconciliar. | Descargar, reintentar descarga, eliminar local. |
| `ErrorSat` | SAT reporto `EstadoSolicitud=4`. | Si | Terminal para worker automatico. | Eliminar local. |
| `Rechazada` | SAT reporto `EstadoSolicitud=5` en verificacion. | Si | Terminal para worker automatico. | Eliminar local. |
| `Vencida` | SAT reporto `EstadoSolicitud=6`. | Si | Terminal para verificacion/descarga automatica. | Eliminar local; crear nueva solicitud manualmente. |
| `EnvioFallido` | Rechazo explicito y documentado de la operacion de creacion sin `IdSolicitud` (ADR 0017). Los fallos locales o de autenticacion previos al envio regresan a `Creada`. | No | Terminal para worker automatico. | Eliminar local; crear nueva solicitud manualmente. |
| `EnvioIncierto` | Timeout/interrupcion despues de iniciar envio, `5000` sin `IdSolicitud`, `5006` o codigo de creacion no documentado; no se sabe si SAT creo solicitud. | No | No reintentar automaticamente. | Eliminar local; crear nueva solicitud manualmente con advertencia. |

Notas:

- `ErrorSat` no debe mezclarse con `EnvioFallido`.
- `EnvioIncierto` existe para evitar reintentos automaticos que puedan gastar cupo SAT o topar duplicados.
- `Vencida` solo representa `EstadoSolicitud=6` de SAT. La expiracion local de un paquete se registra en `PaqueteSolicitud` con su motivo y origen, y no cambia por si sola el estado SAT de la solicitud.
- `eliminado_en` es la unica fuente de eliminacion logica. La UI puede derivar una etiqueta de eliminacion local, pero no se persiste como estado operativo.

## Estados de PaqueteSolicitud

| Estado | Significado | Worker automatico | Acciones manuales validas |
| --- | --- | --- | --- |
| `Disponible` | SAT devolvio `IdPaquete`; ZIP no descargado. | Descargar si monitoreo activo. | Descargar paquete, eliminar local via solicitud. |
| `Descargando` | Descarga reclamada o en curso, incluida su preparacion y autenticacion; no implica que ya exista token valido. | No iniciar otra descarga concurrente. | Eliminar local, con advertencia. |
| `Descargado` | Archivo final existe porque el temporal fue renombrado correctamente. | Ignorar. | Eliminar local via solicitud. |
| `Error` | Error SAT, red, autenticacion, archivo o interrupcion. | No reintentar automaticamente. | Reintentar descarga, eliminar local via solicitud. |
| `Vencido` | El paquete ya no es descargable. La causa se conserva en `motivo_vencimiento` y `origen_vencimiento`. | Ignorar. | Eliminar local via solicitud. |
Transiciones adicionales:

- `Disponible -> Error`: fallo de autenticacion o preparacion antes de descargar.
- `Descargando -> Vencido`: SAT devuelve paquete inexistente/expirado.
- `Descargando -> Disponible`: recuperacion de arranque tras cierre inesperado
  cuando se confirma que no existe archivo final.
- `eliminado_en` es la unica fuente de eliminacion logica del paquete; no se persiste como estado de descarga.

## Deteccion de vencimiento

La app detecta vencimiento de solicitudes o paquetes por tres caminos:

- Verificacion SAT devuelve `EstadoSolicitud = 6`: la solicitud pasa a `Vencida` y sus paquetes no descargados pasan a `Vencido` en la misma transaccion.
- Descarga SAT devuelve `5007` para un paquete: el paquete pasa a `Vencido` con origen `SAT` y motivo `paquete_expirado`. Esto no cambia el estado SAT de la solicitud.
- El scheduler detecta `vencimiento_estimado_en` vencido para paquetes `Disponible`, `Error` o `Descargando`: esos paquetes pasan a `Vencido` con origen `estimacion_local`, motivo `vencimiento_estimado` y se registra `LogSolicitud`. Esto tampoco cambia el estado SAT de la solicitud.

La deteccion por fecha estimada no borra archivos ni recrea solicitudes. Solo evita seguir presentando paquetes como descargables cuando la ventana de SAT probablemente expiro.

`vencimiento_estimado_en` se fija una sola vez al insertar por primera vez cada
`IdPaquete` recibido en una verificacion `Terminada`: `ahoraUtc() + 72 h`. No
se desplaza si una verificacion posterior vuelve a reportar el paquete. Es una
estimacion local basada en la primera observacion, no la hora exacta de
generacion ni el vencimiento confirmado por SAT.

## Duplicados locales

La app calcula una `dedup_key` normalizada antes de llamar a SAT.

Campos de la clave:

- `operacion_sat`: `SolicitaDescargaEmitidos` o `SolicitaDescargaRecibidos`.
- `rfc_solicitante`.
- `rfc_emisor`, si aplica.
- `rfc_receptor`, si aplica.
- `rfc_receptores`, normalizados y ordenados si aplica.
- `fecha_inicial_sat` y `fecha_final_sat` en hora Centro de Mexico.
- `tipo_solicitud = CFDI`.
- `estado_comprobante = Vigente`.
- `tipo_comprobante`, si aplica.
- `complemento`, si aplica.

Estados que bloquean crear otra solicitud con la misma `dedup_key`:

- `Creada`
- `Enviando`
- `Enviada`
- `Aceptada`
- `EnProceso`

Estados que no bloquean automaticamente, pero deben mostrar advertencia antes de reenviar:

- `EnvioFallido`
- `EnvioIncierto`
- `ErrorSat`
- `Rechazada`
- `Vencida`
- `Terminada` cuando no tenga paquetes no eliminados
- solicitudes eliminadas localmente

La eliminacion local no borra la historia SAT ni garantiza que el SAT permita otra solicitud identica.

Una solicitud `Terminada` con todos sus paquetes no eliminados en `Descargado`
sigue bloqueando una solicitud equivalente: ya existe una descarga completa para
ese rango y filtros. La excepcion D008 solo aplica cuando no hay paquetes no
eliminados.

Tambien se permite una nueva solicitud equivalente manual, con advertencia y
confirmacion, cuando exista al menos un paquete `Vencido` y todos los demas
paquetes no eliminados esten `Descargado` o `Vencido`. Si hay paquetes
`Disponible`, `Descargando` o `Error`, la solicitud equivalente sigue
bloqueada.

Para implementacion local, `evaluarDuplicado` clasifica una coincidencia como:

| Coincidencia existente | Resultado |
| --- | --- |
| `Creada`, `Enviando`, `Enviada`, `Aceptada` o `EnProceso` | `Bloqueado` |
| `Terminada` con algun paquete no eliminado `Disponible`, `Descargando` o `Error` | `Bloqueado` |
| `Terminada` con todos sus paquetes no eliminados en `Descargado` | `Bloqueado` |
| `Terminada` con al menos un `Vencido` y el resto `Descargado` o `Vencido` | `RequiereConfirmacion` |
| `Terminada` sin paquetes no eliminados | `RequiereConfirmacion` |
| `EnvioIncierto` | `RequiereConfirmacion` |
| `EnvioFallido`, `ErrorSat`, `Rechazada` o `Vencida` | `RequiereConfirmacion` |
| Solicitud eliminada localmente con la misma clave | `RequiereConfirmacion` |

Si hay varias coincidencias para la misma `dedup_key`, la precedencia es
`Bloqueado` > `RequiereConfirmacion` > `Libre`.

La clave no incluye `perfil_sat_id`: debe basarse en RFC solicitante, filtros y
valores visibles para SAT. Asi, eliminar y recrear un perfil con el mismo RFC no
evade la deduplicacion local.

### Formato canonico de `dedup_key v1`

El formato persistido es:

```text
v1:<sha256-hex-lowercase>
```

El hash SHA-256 se calcula sobre una serializacion canonica UTF-8 sin BOM, con
pares `clave=valor` separados por LF y sin LF final. El orden fijo de claves es:

1. `operacion_sat`.
2. `rfc_solicitante`.
3. `rfc_emisor`, si aplica.
4. `rfc_receptor`, si aplica.
5. `rfc_receptores`, si aplica.
6. `fecha_inicial_sat`.
7. `fecha_final_sat`.
8. `tipo_solicitud=CFDI`.
9. `estado_comprobante=Vigente`.
10. `tipo_comprobante`, si aplica.
11. `complemento`, si aplica.

Reglas de serializacion:

- `operacion_sat` usa `SolicitaDescargaEmitidos` o `SolicitaDescargaRecibidos`.
- RFCs normalizados: trim, mayusculas y sin espacios internos.
- Fechas SAT: `YYYY-MM-DDThh:mm:ss` en hora Centro de Mexico, sin offset. Las
  fechas capturadas como dia completo se expanden a `T00:00:00` y `T23:59:59`.
- `rfc_receptores` se normaliza, deduplica, ordena por bytes UTF-8 y se une con
  coma sin espacios.
- Opcionales ausentes, string vacio o listas vacias omiten la linea completa.
  Nunca se serializa `clave=`.
- Valores con LF, `=` o `,` se rechazan en dominio antes de generar la clave.
- No se incluyen `perfil_sat_id`, UUIDs locales, usuario ni timestamps.

Los tests deben fijar vectores golden con el string canonico pre-hash y el
SHA-256 esperado calculado fuera de la implementacion. Cambiar campos, orden,
normalizacion, separadores, hash, prefijo o filtros SAT que afecten equivalencia
exige una nueva version y una migracion de datos.

## Acciones pendientes

Si el monitoreo esta pausado y el usuario pide una accion que requiere SAT:

- `Verificar ahora` activa `verificacion_pendiente = true`.
- `Reintentar descarga` activa `descarga_pendiente = true`.

Ambas intenciones pueden coexistir. No se sobrescriben entre si.

Al reanudar monitoreo:

1. El ejecutor serial consume `verificacion_pendiente` cuando la solicitud tenga `IdSolicitud`.
2. El ejecutor serial consume `descarga_pendiente` cuando existan paquetes `Disponible` o `Error`.
3. Cada intencion se limpia al terminar su operacion.
4. Si la operacion ya no aplica, se limpia la intencion y se registra `LogSolicitud`.

No existe tabla de cola en el MVP.

## Agenda y orden del ciclo del worker

La agenda es por solicitud mediante `siguiente_verificacion_en`. Se considera
una verificacion sin cambio cuando repite `estado_solicitud_sat`,
`codigo_estado_solicitud`, `numero_cfdi` y el conjunto de `idsPaquetes`. Un
cambio pone el contador en 0 y programa 10 min; la tercera verificacion sin
cambio consecutiva programa 30 min. Una falla no cuenta como sin cambio y
programa 30 min.

La racha de fallas se persiste por clave `(fase, codigo)`. Tres fallas iguales
con codigo `300`, `302`, `303` o `5004` suspenden la verificacion automatica
(`siguiente_verificacion_en = NULL`) hasta `Verificar ahora`; red, `404`, Fault
y `5011` no suspenden. Solo un cambio de clave crea un nuevo `LogSolicitud`;
una repeticion actualiza `ultimo_error`.

La recuperacion corre al arrancar, antes de cualquier ciclo. Con monitoreo
activo, cada ciclo toma una seleccion acotada y procesa, en orden:

1. Vencimientos estimados.
2. Intenciones pendientes.
3. Verificaciones debidas.
4. Descargas automaticas.

Lo manual tiene prioridad sobre lo automatico y la verificacion sobre la
descarga. Antes de seleccionar trabajo de un perfil, se consulta una vez su
estado de credencial; si no esta `Lista`, se omite el perfil sin llamar a SAT y
se publica el cambio como evento de aplicacion para la UI.

## Ejecutor serial

Responsabilidades:

- Recibir operaciones del worker y de acciones manuales.
- Ejecutarlas fuera del hilo grafico.
- Evitar operaciones concurrentes sobre la misma base local y los mismos paquetes.
- Publicar resultados para que view models actualicen QML en el hilo grafico.
- Verificar `eliminado_en` antes de aplicar resultados.
- Registrar logs sanitizados.

Operacion serial global:

- Es suficiente para uso personal.
- Reduce riesgo de duplicar descargas o mezclar respuestas.
- Puede evolucionar a serializacion por solicitud si el uso real lo exige.

Reglas Qt/SQLite:

- La conexion SQLite usada por el ejecutor vive en el hilo del ejecutor.
- La UI no comparte `QSqlDatabase` con el ejecutor.
- Los `QAbstractListModel` se actualizan desde el hilo grafico.
- Los resultados del ejecutor cruzan a UI mediante signals/slots o invocaciones encoladas.

## Transacciones locales

### Crear solicitud

1. Transaccion local: insertar `SolicitudMasiva` en `Creada`, con `dedup_key`.
2. Transaccion local: marcar `Enviando` y registrar intento.
3. Llamar SAT.
4. Transaccion local:
   - Si `CodEstatus=5000` e `IdSolicitud` existe: marcar `Enviada`, guardar `id_solicitud_sat`, `cod_estatus_solicitud` y mensaje.
   - Si SAT rechaza con codigo documentado sin `IdSolicitud`: marcar `EnvioFallido`, guardar codigo y mensaje de creacion.
   - Si la falla ocurrio en `Preparacion`, `Autenticacion` o `AntesDeEnvio` de `SolicitaDescarga*` (ADR 0017): regresar atomicamente a `Creada`, limpiar `envio_iniciado_en`, `cod_estatus_solicitud` y `mensaje_solicitud_sat`, guardar `ultimo_error` saneado y registrar `envio_no_iniciado`. El reenvio es solo manual.
   - Si hay timeout/interrupcion despues de iniciar HTTP, `5000` sin `IdSolicitud`, `5006` o codigo de creacion no documentado: marcar `EnvioIncierto`.

### Verificar solicitud

Una sola transaccion debe:

- Actualizar `SolicitudMasiva` con `estado_solicitud_sat`, `codigo_estado_solicitud`, mensaje, fechas y estado local.
- Si SAT reporta `Terminada`, registrar o actualizar todos los `PaqueteSolicitud` devueltos.
- Si SAT reporta `Vencida`, marcar paquetes no descargados como `Vencido` con origen `SAT` y motivo `solicitud_expirada`.
- Registrar `LogSolicitud`.

La app no debe dejar `SolicitudMasiva=Terminada` sin registrar los paquetes recibidos en la misma transaccion.

### Descargar paquete

1. Transaccion local breve: reclamar el paquete y marcarlo `Descargando`.
2. Invocar la operacion externa sin una transaccion abierta; su adaptador hace
   preparacion, autenticacion, descarga del ZIP, escritura por chunks a un
   temporal en la carpeta final y promocion atomica sin reemplazo. El temporal
   se llama `.<archivo>.<16 hex aleatorios>.part`, se crea en exclusiva con
   permisos `0600` y los directorios nuevos usan `0700`. Antes de promover se
   sincroniza el archivo con `fsync` y `F_FULLFSYNC`; la promocion no reemplaza
   un destino existente (`renamex_np` con `RENAME_EXCL`) y despues se
   sincroniza el directorio.
3. Transaccion local `BEGIN IMMEDIATE`: si el paquete no fue eliminado, con
   archivo final confirmado marcar `Descargado` y guardar ruta y codigo SAT.

Una falla de `Preparacion`, `Autenticacion` o `Almacenamiento` deja el paquete
en `Error`; `5007` lo deja `Vencido` y `5008` lo deja `Error`. Una interrupcion
se recupera al arrancar. La solicitud solo pasa a `Vencida` si una respuesta de
verificacion SAT reporta `EstadoSolicitud=6`.

Si falla la sincronizacion del archivo antes de la promocion, el resultado es
`Durabilidad`, no existe archivo final y el paquete queda en `Error`. Si la
promocion ya ocurrio y falla la sincronizacion del directorio, la operacion es
exitosa con `advertenciaDurabilidad=true`: el final existe, el ejecutor marca
`Descargado` y registra una advertencia saneada. No se convierte en error ni
se reintenta como descarga, porque el reintento encontraria una colision de
destino. Ningun flujo sobrescribe o borra un archivo final.

La comprobacion de existencia usada por el detalle se encola en
`OperacionExecutor` como una lectura de filesystem. Su resultado vuelve por
senal encolada como `Presente`, `NoEncontrado` o `ErrorComprobacion`; no cambia
el estado persistido del paquete ni registra un log de solicitud.

### Eliminar solicitud local

Una sola transaccion marca virtualmente:

- `SolicitudMasiva.eliminado_en`.
- `PaqueteSolicitud.eliminado_en`.
- `LogSolicitud.eliminado_en`.

No borra ZIPs fisicos.

## Recuperacion al arrancar

Al iniciar la app:

- `Creada` sin intento de envio: se mantiene visible y puede enviarse.
- `Enviando` con intento iniciado: pasa a `EnvioIncierto` y registra log. No se reenvia desde el MVP.
- Antes de aplicar estados, `PackageStorage` escanea solo hechos propios: rutas
  bajo `<RFC>/<yyyy-mm>/<UUID canonico>/` y temporales con el patron estricto
  `.<archivo>.<16 hex aleatorios>.part`. Reporta `Temporal` o `Final`, ruta
  relativa, UUID de solicitud y nombre de archivo final asociado; no consulta
  SQLite. Todo lo demas se ignora y nunca se borra.
- Para un temporal asociado a un paquete `Descargando`, T007 aplica su regla
  de recuperacion y ordena `eliminarTemporal`. Un temporal propio no asociable
  se elimina con un diagnostico saneado de aplicacion. La eliminacion solo
  acepta temporales propios.
- `Descargando` sin archivo final confirmado: pasa a `Disponible` y registra
  log de descarga interrumpida. Si existe el archivo final para un paquete en
  `Descargando`, se reconcilia como `Descargado` y se registra el evento.
  Si falla la comprobacion de archivo final, conserva `Descargando` y registra
  una falla de reconciliacion; no infiere que el archivo no existe.
- Un final con solicitud asociable pero sin paquete persistido se conserva y
  T007 registra `archivo_huerfano` en `LogSolicitud`. Un final sin solicitud
  asociable se conserva y solo produce un diagnostico saneado en el log de la
  aplicacion; no escribe `LogSolicitud`.
- Ninguna rama de recuperacion sobrescribe o borra archivos finales.
- Solicitudes `Terminada` con paquetes `Disponible` o `Error` quedan disponibles para descarga o reintento manual.
- Solicitudes eliminadas localmente se ignoran por worker y UI principal.

## Perfil SAT, credencial y eliminacion

- `PerfilSat.rfc` se persiste normalizado y queda reservado mientras
  `eliminado_en IS NULL`.
- `activo=false` impide crear nuevas solicitudes con ese perfil, pero no detiene
  el worker, no invalida credenciales y no libera el RFC.
- Un perfil puede existir sin credencial vigente. La relacion es perfil 1 a
  credencial 0..1.
- La credencial vigente se reemplaza sin historial SQLite al actualizar e.firma.
- El MVP permite eliminar perfiles solo si no tienen solicitudes no eliminadas
  en curso. La eliminacion conserva historial local y debe eliminar la
  credencial vigente y sus secretos mediante la capa de aplicacion.

## Logs sanitizados

`LogSolicitud` guarda resumen estructurado, no payload crudo ilimitado.

Debe excluir:

- Tokens SAT.
- Llave privada.
- Contrasena.
- Firma XML.
- Certificado completo si no es necesario para diagnostico.
- Contenido `Paquete` de respuesta de descarga.
- Base64 de ZIP.

Puede guardar:

- Operacion SAT.
- Codigos y mensajes SAT.
- Timestamps.
- IdSolicitud e IdPaquete.
- Filtros normalizados sin secretos.
- Resultado de sanitizacion.

## Implicaciones para SQLite

El modelo fisico debe incluir:

- `operacion_sat`.
- `dedup_key`.
- `cod_estatus_solicitud`.
- `mensaje_solicitud_sat`.
- `estado_solicitud_sat`.
- `codigo_estado_solicitud`.
- `mensaje_verificacion_sat`.
- `verificacion_pendiente`.
- `descarga_pendiente`.
- Campos de intento de envio y operacion en curso.
- `codigo_descarga_sat` y `mensaje_descarga_sat` en paquetes.
- Soporte para eliminacion virtual.

Estos campos deben resolverse antes de cerrar la migracion inicial SQLite.
