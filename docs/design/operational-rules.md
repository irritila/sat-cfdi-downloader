# Reglas operativas

Estado: borrador para revision antes de modelo fisico SQLite.

## Objetivo

Cerrar las reglas que afectan persistencia, concurrencia y recuperacion antes de fijar tablas SQLite.

Este documento no agrega funcionalidades al MVP. Solo precisa como se ejecutan y recuperan los flujos ya definidos: crear solicitud, verificar estado, registrar paquetes, descargar ZIP, ejecutar acciones manuales y eliminar registros locales.

## Principios

- Uso personal local-first: un solo proceso de app y un solo usuario local.
- Todas las operaciones criticas pasan por un ejecutor serial.
- La UI nunca aplica directamente respuestas SAT ni escribe estados persistidos.
- Los codigos SAT se guardan sin mezclar respuesta de creacion, estado de verificacion y descarga.
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

| Estado | Significado | Tiene `IdSolicitud` SAT | Worker automatico | Acciones manuales validas |
| --- | --- | --- | --- | --- |
| `Creada` | Registro local creado antes de enviar a SAT. | No | Puede enviar si no hay intento iniciado. | Eliminar local. |
| `Enviando` | La app inicio envio a SAT y espera resultado. | No | No iniciar otro envio concurrente. | Eliminar local, con advertencia. |
| `Enviada` | SAT devolvio `IdSolicitud` con `CodEstatus=5000`. | Si | Verificar. | Verificar ahora, eliminar local. |
| `Aceptada` | SAT reporto `EstadoSolicitud=1`. | Si | Verificar. | Verificar ahora, eliminar local. |
| `EnProceso` | SAT reporto `EstadoSolicitud=2`. | Si | Verificar. | Verificar ahora, eliminar local. |
| `Terminada` | SAT reporto `EstadoSolicitud=3`; paquetes deben estar registrados. | Si | Descargar paquetes pendientes; verificar solo si hace falta reconciliar. | Descargar, reintentar descarga, eliminar local. |
| `ErrorSat` | SAT reporto `EstadoSolicitud=4`. | Si | Terminal para worker automatico. | Eliminar local. |
| `Rechazada` | SAT reporto `EstadoSolicitud=5` o rechazo inicial conocido sin `IdSolicitud`. | Puede no tener | Terminal para worker automatico. | Eliminar local. |
| `Vencida` | SAT reporto `EstadoSolicitud=6` o paquetes pendientes expiraron. | Si | Terminal para verificacion/descarga automatica. | Eliminar local; crear nueva solicitud manualmente. |
| `EnvioFallido` | Fallo conocido antes de crear solicitud: autenticacion, XML local mal formado, rechazo con codigo claro. | No | Terminal para worker automatico. | Reintentar envio si el error es corregible; eliminar local. |
| `EnvioIncierto` | Timeout/interrupcion despues de iniciar envio; no se sabe si SAT creo solicitud. | No | No reintentar automaticamente. | Eliminar local; crear nueva solicitud manualmente con advertencia. |
| `EliminadaLocalmente` | Eliminacion virtual. | Puede tener | Ignorar. | Ninguna. |

Notas:

- `ErrorSat` no debe mezclarse con `EnvioFallido`.
- `EnvioIncierto` existe para evitar reintentos automaticos que puedan gastar cupo SAT o topar duplicados.
- Una solicitud `Terminada` puede pasar a `Vencida` si aun tiene paquetes no descargados y SAT/descarga indica expiracion.

## Estados de PaqueteSolicitud

| Estado | Significado | Worker automatico | Acciones manuales validas |
| --- | --- | --- | --- |
| `Disponible` | SAT devolvio `IdPaquete`; ZIP no descargado. | Descargar si monitoreo activo. | Descargar paquete, eliminar local via solicitud. |
| `Descargando` | Token valido obtenido y descarga iniciada. | No iniciar otra descarga concurrente. | Eliminar local, con advertencia. |
| `Descargado` | Archivo final existe porque el temporal fue renombrado correctamente. | Ignorar. | Eliminar local via solicitud. |
| `Error` | Error SAT, red, autenticacion, archivo o interrupcion. | No reintentar automaticamente. | Reintentar descarga, eliminar local via solicitud. |
| `Vencido` | SAT indico paquete inexistente/expirado o vencio la solicitud. | Ignorar. | Eliminar local via solicitud. |
| `EliminadoLocalmente` | Eliminacion virtual. | Ignorar. | Ninguna. |

Transiciones adicionales:

- `Disponible -> Error`: fallo de autenticacion o preparacion antes de descargar.
- `Descargando -> Vencido`: SAT devuelve paquete inexistente/expirado.
- `Descargando -> Disponible`: recuperacion de arranque tras cierre inesperado sin archivo final.

## Deteccion de vencimiento

La app detecta vencimiento de solicitudes o paquetes por tres caminos:

- Verificacion SAT devuelve `EstadoSolicitud = 6`: la solicitud pasa a `Vencida` y sus paquetes no descargados pasan a `Vencido` en la misma transaccion.
- Descarga SAT devuelve `5007` para un paquete: el paquete pasa a `Vencido`. Si ya no quedan paquetes descargables para esa solicitud, la solicitud pasa a `Vencida`.
- El scheduler detecta `vencimiento_estimado_en` vencido para paquetes `Disponible`, `Error` o `Descargando`: esos paquetes pasan a `Vencido` y se registra `LogSolicitud`. Si no quedan paquetes descargables, la solicitud pasa a `Vencida`.

La deteccion por fecha estimada no borra archivos ni recrea solicitudes. Solo evita seguir presentando paquetes como descargables cuando la ventana de SAT probablemente expiro.

## Duplicados locales

La app calcula una `dedup_key` normalizada antes de llamar a SAT.

Campos de la clave:

- `perfil_sat_id`.
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
- `EnvioIncierto`
- `Enviada`
- `Aceptada`
- `EnProceso`
- `Terminada`

Estados que no bloquean automaticamente, pero deben mostrar advertencia antes de reenviar:

- `EnvioFallido`
- `ErrorSat`
- `Rechazada`
- `Vencida`
- solicitudes eliminadas localmente

La eliminacion local no borra la historia SAT ni garantiza que el SAT permita otra solicitud identica.

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

## Ejecutor serial

Responsabilidades:

- Recibir operaciones del worker y de acciones manuales.
- Ejecutarlas fuera del hilo grafico.
- Evitar operaciones concurrentes sobre la misma base local y los mismos paquetes.
- Publicar resultados para que view models actualicen QML en el hilo grafico.
- Verificar `eliminada_en` antes de aplicar resultados.
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
   - Si SAT rechaza con codigo conocido sin `IdSolicitud`: marcar `Rechazada` o `EnvioFallido` segun causa.
   - Si hay timeout/interrupcion despues de iniciar HTTP: marcar `EnvioIncierto`.

### Verificar solicitud

Una sola transaccion debe:

- Actualizar `SolicitudMasiva` con `estado_solicitud_sat`, `codigo_estado_solicitud`, mensaje, fechas y estado local.
- Si SAT reporta `Terminada`, registrar o actualizar todos los `PaqueteSolicitud` devueltos.
- Si SAT reporta `Vencida`, marcar paquetes no descargados como `Vencido`.
- Registrar `LogSolicitud`.

La app no debe dejar `SolicitudMasiva=Terminada` sin registrar los paquetes recibidos en la misma transaccion.

### Descargar paquete

1. Obtener token valido.
2. Transaccion local: marcar paquete `Descargando`.
3. Descargar bytes del ZIP.
4. Escribir a archivo temporal.
5. Renombrar archivo temporal a ruta final.
6. Transaccion local: marcar `Descargado`, guardar ruta y codigo SAT.

Si falla antes de renombrar, el paquete queda en `Error`, `Disponible` o `Vencido` segun causa. Si la causa es vencimiento y no quedan paquetes descargables, la solicitud queda `Vencida` en la misma transaccion.

### Eliminar solicitud local

Una sola transaccion marca virtualmente:

- `SolicitudMasiva.eliminada_en`.
- `PaqueteSolicitud.eliminado_en`.
- `LogSolicitud.eliminado_en`.

No borra ZIPs fisicos.

## Recuperacion al arrancar

Al iniciar la app:

- `Creada` sin intento de envio: se mantiene visible y puede enviarse.
- `Enviando` con intento iniciado: pasa a `EnvioIncierto` y registra log.
- `Descargando` sin archivo final: pasa a `Disponible` y registra log de descarga interrumpida.
- Archivos temporales `.part` o `.tmp` se eliminan o ignoran.
- Solicitudes `Terminada` con paquetes `Disponible` o `Error` quedan disponibles para descarga o reintento manual.
- Solicitudes eliminadas localmente se ignoran por worker y UI principal.

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
