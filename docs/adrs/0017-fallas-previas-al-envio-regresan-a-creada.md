# ADR 0017: Regresar a `Creada` las solicitudes cuyo envio fallo antes de salir a SAT

## Estado

Accepted

## Modifica

- ADR 0015, solo la regla de creacion "fallo local o autenticacion fallida previa al envio produce `EnvioFallido`". El resto de ADR 0015 sigue vigente.

## Contexto

ADR 0015 lleva a `EnvioFallido` cualquier fallo local o de autenticacion previo al envio. `EnvioFallido` es terminal: para reintentar, el usuario debe eliminar la solicitud y crear otra equivalente con confirmacion.

El refinamiento de T009 (2026-10-04) detecto que esto quema solicitudes que SAT nunca recibio, por causas locales y recuperables: llavero de macOS bloqueado, e.firma vencida o danada, red caida antes de `Autentica` o autenticacion rechazada. El resultado es ruido en el historial y pasos manuales innecesarios, sin ningun beneficio de seguridad, porque ningun byte de `SolicitaDescarga*` salio del equipo.

`T007` clasifica las fallas de `OperacionesSat` por fase: `Preparacion`, `Autenticacion`, `AntesDeEnvio`, `DespuesDeEnvio`, `RespuestaExplicita` y `Almacenamiento`.

## Decision

Decision aprobada por el usuario en el refinamiento de T009.

- Una falla de envio reportada en vivo en las fases `Preparacion`, `Autenticacion` o `AntesDeEnvio` (esta ultima para la llamada `SolicitaDescarga*`) regresa la solicitud de `Enviando` a `Creada`. La transicion es atomica: limpia `envio_iniciado_en`, `cod_estatus_solicitud` y `mensaje_solicitud_sat`, guarda `ultimo_error` saneado y registra `LogSolicitud`.
- El reenvio desde `Creada` es solo una accion manual (`Enviar`). El worker nunca envia.
- La UI habilita `Enviar` solo si la credencial del perfil esta `Lista` (`SecretStore::obtenerEstado`). La comprobacion se repite en el adaptador antes de usar el material.
- `EnvioFallido` queda reservado para los rechazos explicitos de la operacion de creacion sin `IdSolicitud` (por ejemplo, `300`-`305`, `5001`, `5002`, `5005`).
- `EnvioIncierto` no cambia: aplica tras `DespuesDeEnvio`, `5000` sin `IdSolicitud`, `5006`, un codigo no documentado o una respuesta ambigua.
- La recuperacion al arrancar no cambia: una solicitud que quedo en `Enviando` pasa a `EnvioIncierto`, porque sin evidencia no se sabe si `SolicitaDescarga*` llego a salir.
- `Creada` sigue bloqueando las solicitudes equivalentes (`dedup_key`).

## Consecuencias

- Los errores locales dejan de producir estados terminales; el usuario corrige la causa y pulsa `Enviar`.
- El adaptador de `OperacionesSat` (T009) debe garantizar que esas tres fases implican que la operacion de creacion no se transmitio; cualquier falla de `Autentica` se reporta como `Autenticacion`.
- `T007` debe implementar la transicion `Enviando -> Creada` y un tipo de evento de log para ella (migracion del catalogo `tipo_evento`).
- `docs/design/operational-rules.md` actualiza el significado de `EnvioFallido` y la secuencia de "Crear solicitud".

## Referencias

- ADR 0015
- `docs/design/operational-rules.md`
- `docs/tasks/T007-worker-ejecutor-serial.md`, `docs/tasks/T009-flujo-sat-integrado.md`
- `docs/meetings/T009-refinamiento/` (bitacora del refinamiento)
