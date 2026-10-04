# ADR 0015: Separar estados SAT, paquetes y eliminacion local

## Estado

Accepted

Modificado parcialmente por ADR 0017: los fallos locales o de autenticacion previos al envio regresan a `Creada` en lugar de `EnvioFallido`.

## Reemplaza

- ADR 0014 en sus reglas de vencimiento de paquetes y solicitud, rechazo inicial
  sin `IdSolicitud` y eliminacion local como estado operativo.

ADR 0014 sigue vigente para el ejecutor serial, la frontera de concurrencia,
las transacciones locales y la recuperacion al arrancar.

## Contexto

El refinamiento de T001 detecto tres mezclas que hacian ambiguo el modelo
fisico SQLite:

- Marcar la solicitud como `Vencida` cuando solo vence un paquete.
- Usar `Rechazada` para una respuesta de creacion sin `IdSolicitud`, aunque
  `Rechazada` es un `EstadoSolicitud` de verificacion.
- Persistir la eliminacion local como un estado operativo, perdiendo el ultimo
  estado util para trazabilidad y recuperacion.

Tambien se cerraron reglas de perfiles, deduplicacion y excepciones manuales
que el esquema debe soportar desde la migracion inicial.

## Decision

La solicitud y los paquetes conservan dimensiones separadas:

- `estado_solicitud_sat` solo se escribe a partir de verificacion SAT con
  `EstadoSolicitud=1..6`.
- `Rechazada` queda reservada para `EstadoSolicitud=5`.
- `Vencida` queda reservada para `EstadoSolicitud=6`.
- El vencimiento por descarga `5007` o por estimacion local solo cambia el
  estado de paquetes no descargados a `Vencido`; no cambia por si solo el
  estado SAT de la solicitud.

La creacion de solicitud usa estados locales:

- `CodEstatus=5000` con `IdSolicitud` utilizable produce `Enviada`.
- Fallo local, autenticacion fallida previa al envio o rechazo documentado de
  la operacion de creacion produce `EnvioFallido`.
- Timeout, interrupcion despues de iniciar HTTP, `5000` sin `IdSolicitud`,
  `5006` o codigos de creacion no documentados producen `EnvioIncierto`.
- No se reenvia automaticamente una solicitud en `EnvioIncierto`.

La eliminacion local es ortogonal:

- `eliminado_en` es la unica fuente persistida de eliminacion logica.
- No se agregan estados `EliminadaLocalmente` ni `EliminadoLocalmente`.
- La UI puede derivar etiquetas de eliminacion desde `eliminado_en`.
- Los estados, codigos, mensajes y motivos historicos se conservan.

Las solicitudes equivalentes se controlan con `dedup_key` basada en RFC y
criterios visibles para SAT, no en `perfil_sat_id`. Recrear un perfil con el
mismo RFC no evade bloqueos locales ni excepciones manuales.

Se permiten nuevas solicitudes equivalentes manuales, siempre con advertencia
y confirmacion, en estos casos:

- Existe al menos un paquete `Vencido` y todos los demas paquetes no eliminados
  estan `Descargado` o `Vencido`.
- La solicitud anterior esta en `EnvioIncierto`.
- La solicitud anterior esta `Terminada` y no tiene paquetes no eliminados.

La solicitud original se conserva y no hay reenvio automatico.

Para perfiles SAT:

- `activo=false` impide crear solicitudes nuevas con ese perfil, pero no
  detiene el worker, no invalida credenciales y no libera el RFC.
- Solo `eliminado_en` libera el RFC para recrear el perfil.
- El MVP permite eliminar perfiles solo si no tienen solicitudes no eliminadas
  en curso; se conserva el historial y se eliminan la credencial vigente y sus
  secretos.
- La credencial se modela como actual y reemplazable; no se conserva historial
  de credenciales en SQLite.

## Consecuencias

- T001 debe expresar invariantes estructurales con CHECK, indices parciales y
  claves foraneas, pero las clasificaciones de transiciones y respuestas SAT
  quedan en aplicacion y pruebas.
- `solicitud_masiva` necesita persistir `rfc_solicitante` para deduplicar aun
  cuando un perfil sea eliminado y recreado.
- Los codigos SAT se guardan como texto libre; no se cierran catalogos que SAT
  pueda ampliar.
- T003 debe activar claves foraneas por conexion, filtrar eliminados y ejecutar
  escrituras criticas en transacciones.
- T005 debe coordinar reemplazo y borrado de secretos con la fila vigente de
  credencial, contemplando limpieza de secretos huerfanos.
- T007 debe garantizar que solo verificacion escriba `estado_solicitud_sat` y
  que las excepciones manuales se validen en la misma frontera transaccional.

## Referencias

- `docs/design/operational-rules.md`
- `docs/tasks/T001-modelo-fisico-sqlite.md`
- `docs/meetings/T001-refinamiento/bitacora.md`
- ADR 0014
