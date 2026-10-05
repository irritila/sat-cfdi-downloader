# T009: Flujo SAT integrado

## Estado

Pendiente (refinada 2026-10-04). Lista para implementar por operacion una vez cumplida la precondicion de T006 (ver Precondiciones).

## Prioridad y tamano

- Prioridad: Critica.
- Tamano: Grande.

## Objetivo

Sustituir el adaptador nulo de `OperacionesSat` por el productivo, que combina el `SatGateway` real, `PackageStorage` y `SecretStore`. Conectar la UI existente, el catalogo de mensajes y las notificaciones nativas para que el flujo real del MVP (crear, enviar, verificar, registrar paquetes y descargar) funcione de punta a punta.

## Contexto

Esta tarea integra piezas ya definidas y no debe redescubrirlas:

- `T006`: bloques SAT (sobres, firma, parser, HTTP por fases), evidencia real y contrato recomendado.
- `T007`: ejecutor serial, agenda, transiciones, transacciones, recuperacion, gate por perfil, fallas repetidas y el puerto `OperacionesSat`.
- `T008`: `PackageStorage` (guardado atomico por chunks, existencia y escaneo).

T009 no decide transiciones de estado: entrega resultados semanticos y fallas clasificadas al ejecutor de T007. Su resultado sigue limitado a uso personal.

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | Notificaciones por solicitud: `Terminada (N paquetes)`; una sola `Descarga completa: N de N` cuando no queden paquetes por descargar; una por `ErrorSat`, `Rechazada` o `Vencida`. La de `Vencida` indica que los paquetes pueden ya no estar disponibles. No hay notificacion por paquete. | Aprobada por usuario | Refinamiento 2026-10-04. Interpreta `requirements.md` §7.6 sin generar spam en solicitudes grandes. |
| D2 | Prueba de humo real en T009: 1 solicitud nueva desde la UI, en un dia cerrado distinto a los de T006, con entre 1 y 50 CFDI y solo filtros obligatorios. El worker la verifica y descarga, e incluye un cierre y reapertura de la app. Maximo 2 solicitudes: la segunda solo si la primera recibe un rechazo explicito, nunca tras `EnvioIncierto`. La solicitud de T006 no se reutiliza. | Aprobada por usuario | Refinamiento 2026-10-04. Arquitectura proponia dejar el humo a T010; el usuario lo autorizo aqui. |
| D3 | Una falla de envio en `Preparacion`, `Autenticacion` o antes de enviar `SolicitaDescarga*` regresa la solicitud a `Creada`, con el error visible y `Enviar` solo manual. `Enviar` se habilita solo con la credencial `Lista`. `EnvioFallido` queda para los rechazos explicitos. | Aprobada por usuario | `ADR 0017` (creado en este refinamiento), implementado en `T007` D7. |
| D4 | Contrato `SatGateway` sin SOAP ni persistencia: `autenticar(MaterialFirma&&, Cancelacion) -> TokenSat`; `crearSolicitud(TokenSat, SolicitudSat, Cancelacion) -> RespuestaCreacion{codEstatus, mensaje, idSolicitud?}`; `verificarSolicitud(TokenSat, ConsultaSolicitudSat, Cancelacion) -> RespuestaVerificacion{codEstatus, estadoSolicitud?, codigoEstadoSolicitud?, mensaje, numeroCfdi?, idsPaquete}`; `descargarPaquete(TokenSat, ConsultaPaqueteSat, receptor por chunks, Cancelacion) -> RespuestaDescarga{codEstatus, mensaje}`. `TokenSat` es opaco, no serializable ni registrable. `ErrorSatGateway{fase HTTP, codigo HTTP/SAT?, cancelada, diagnosticoSanitizado}`. | Recomendacion tecnica; los detalles SOAP dependen de T006 | `qt-architecture-lead`; T006 D8. |
| D5 | `OperacionesSat` productivo: para cada operacion consulta `obtenerEstado` y luego `obtenerMaterialFirma` (este ultimo no valida vigencia), usa la sesion de token, invoca `SatGateway`, transmite el ZIP a `PackageStorage.guardarAtomico` y traduce todo a resultados o `FallaOperacion` segun la tabla de mapeo. Tambien implementa `existeArchivoFinal` y `obtenerEstadoCredencial`. | Recomendacion tecnica | `qt-architecture-lead`, `especialista-sat-seguridad`. |
| D6 | Sesion de token privada del adaptador, solo en memoria. Se reutiliza hasta el TTL observado en T006 y se invalida por expiracion, falla de autenticacion, token rechazado (forma confirmada por T006), cambio de credencial o cierre. No retiene `MaterialFirma`. | Recomendacion tecnica | `ADR 0010`, `qt-architecture-lead`. |
| D7 | Timeouts iniciales en `SatGatewayOptions`, inyectado por el composition root (no son preferencia de usuario): autenticacion 30 s, creacion 45 s, verificacion 30 s, descarga 5 min. T006 los confirma o ajusta. Cancelar aborta `QNetworkReply` y la escritura, y conserva la fase (T007 D12). | Recomendacion tecnica | `qt-architecture-lead`. |
| D8 | Tabla de mapeo (seccion siguiente): cualquier error de credencial va a `Preparacion` y cualquier falla de `Autentica` a `Autenticacion`. Las ambiguedades de creacion van a `EnvioIncierto`. En verificacion, `304`/`305` se tratan como credencial y los errores transitorios no cambian el estado. En descarga, `5008` no ofrece reintento. | Recomendacion tecnica; Fault, HTTP no 200 y forma del token rechazado son inferidos hasta T006 | `especialista-sat-seguridad`. |
| D9 | Notificaciones: `OSIntegration::notificar(NotificacionLocal{id, tipo, titulo, cuerpo})` y la senal `notificacionTerminada(id, resultado)`. El servicio de aplicacion decide emitir solo despues del commit de una transicion confirmada, con dedupe por solicitud y transicion. Un permiso denegado no cambia estados, logs ni reintentos. Las notificaciones de cambio de credencial por perfil (evento de T007 D9) tambien usan este metodo. | Recomendacion tecnica | `qt-architecture-lead`, `ADR 0009`. |
| D10 | Catalogo de mensajes visibles (seccion siguiente): sin RFC completo, Ids, token, rutas ni `Mensaje` SAT crudo. | Recomendacion tecnica | `especialista-sat-seguridad`. |
| D11 | Pruebas: `FakeSatGateway` con fixtures sanitizados de T006 para el adaptador; `FakeOperacionesSat` para el flujo; un servidor HTTP local para el transporte; el humo real es manual y nunca forma parte de `ctest`. | Recomendacion tecnica | `qt-quality-engineer`. |

## Tabla de mapeo (D8)

| Origen | Fase o resultado del puerto | Enviar | Verificar | Descargar |
| --- | --- | --- | --- | --- |
| `obtenerEstado` distinto de `Lista`; errores de `SecretStore` | `Preparacion` | `Creada` (ADR 0017) | sin cambio de estado, `ultimo_error` | `Error` |
| Cualquier falla de `Autentica` | `Autenticacion` | `Creada` | sin cambio | `Error` |
| Timeout o corte antes de `requestSent` | `AntesDeEnvio` | `Creada` | sin cambio (transitoria) | `Error` |
| Timeout o corte despues de `requestSent` | `DespuesDeEnvio` | `EnvioIncierto` | sin cambio (verificar es idempotente) | `Error` |
| `5000` con resultado completo | resultado | `Enviada` | estado SAT 1..6 (T007) | `Descargado` |
| `5000` sin `IdSolicitud`, `5006`, codigo no documentado, Fault, HTTP no 200 | `RespuestaExplicita` | `EnvioIncierto` | sin cambio (transitoria) | `Error` |
| `300`-`305`, `5001`, `5002`, `5005` en creacion | `RespuestaExplicita` | `EnvioFallido` | — | — |
| `300`, `302`, `303`, `5004` en verificacion | `RespuestaExplicita` | — | sin cambio; suspension tras 3 iguales (T007 D8) | — |
| `304`, `305` en verificacion | `Preparacion` (credencial) | — | sin cambio | — |
| `5011`, `404` en verificacion | `RespuestaExplicita` (transitoria) | — | sin cambio | — |
| `5007` en descarga | resultado | — | — | `Vencido` (SAT, `paquete_expirado`) |
| `5008` en descarga | `RespuestaExplicita` | — | — | `Error`, sin `Reintentar` |
| `Paquete` vacio o base64 invalido | `RespuestaExplicita` | — | — | `Error` |
| `ErrorAlmacenamiento` | `Almacenamiento` | — | — | `Error` |

Las filas de Fault, HTTP no 200 y token rechazado se confirman o ajustan con `sat-spike-results.md`. Un ajuste de clasificacion se hace en el adaptador, no en el ejecutor.

## Catalogo de mensajes (D10)

- Credencial: "La e.firma de este perfil esta vencida. Reemplazala para continuar." / "No se pudo leer la e.firma guardada. Vuelve a importarla." / "El llavero de macOS esta bloqueado. Desbloquea tu sesion e intenta de nuevo."
- Autenticacion SAT: "El SAT no acepto la autenticacion con esta e.firma." / "No se pudo conectar con el SAT para autenticar. Intenta mas tarde."
- Solicitud rechazada: "El SAT rechazo la solicitud (codigo N). No se registro en el SAT." / "No estas autorizado para descargar estos CFDI (5001)."
- Limites SAT: 5002 "El SAT ya no acepta solicitudes con este mismo criterio." / 5003 "La consulta supera el maximo de CFDI; usa un rango mas corto." / 5005 "El SAT ya tiene una solicitud activa con este criterio." / 5011 "Se alcanzo el limite diario del SAT. Intenta manana."
- Incierto: "No se sabe si el SAT registro la solicitud. No se reenviara automaticamente; revisa antes de crear otra."
- Verificacion: "No se pudo consultar el estado; se reintentara automaticamente." / "El SAT no encontro esta solicitud. La verificacion automatica se detuvo."
- Descarga: "El paquete ya no existe en el SAT (vencido)." / "El paquete alcanzo el maximo de descargas permitidas." / "No se pudo descargar el paquete. Puedes reintentar."
- Almacenamiento: "No hay espacio suficiente para guardar el paquete." / "No se pudo escribir en la carpeta de paquetes; revisa los permisos." / "Ya existe un archivo con ese nombre en la carpeta del paquete."

## Alcance

### Incluye

- El `SatGateway` productivo (D4), construido sobre los bloques de `src/infrastructure/sat/` de T006.
- El `OperacionesSat` productivo (D5), con la sesion de token (D6), los timeouts y la cancelacion (D7), y el mapeo (D8).
- Reemplazar el adaptador nulo en `AppCompositionRoot`, con la propiedad de objetos y los hilos correctos.
- UI existente: crear localmente y encolar el envio, `Enviar` solo con la credencial `Lista`, `Verificar ahora`, `Reintentar descarga` (oculto ante `5008`), estados y mensajes (D10), y la existencia del ZIP en el detalle (T008 D11).
- Notificaciones (D1, D9) y el metodo `notificar` en `OSIntegration` (adaptador macOS y fake).
- La prueba de humo real (D2) con evidencia sanitizada.

### No incluye

- Transiciones, transacciones, agenda, recuperacion, gate por perfil y fallas repetidas (`T007`); el filesystem de los ZIP (`T008`).
- Solicitudes de metadata o por folio; extraccion, parsing o indexacion de XML; consulta de vigencia o cancelacion de CFDI.
- Reenviar el mismo registro desde `EnvioFallido` o `EnvioIncierto`. Crear una solicitud equivalente nueva con confirmacion sigue las reglas de duplicados existentes.
- Particion automatica de rangos, exportaciones contables, backend remoto, multiusuario o Windows.

## Dependencias

- `T005` y `T005.1` (completadas), `T006`, `T007` y `T008` implementadas y con sus pruebas aprobadas.
- `ADR 0009`, `ADR 0010`, `ADR 0013`, `ADR 0014`, `ADR 0015`, `ADR 0017`; `docs/web-service.md`, `docs/design/operational-rules.md`.

## Precondiciones

- Para cada operacion del MVP (autenticacion, creacion de emitidos y de recibidos, verificacion y descarga), `docs/design/sat-spike-results.md` la clasifica `Viable`, o `Requiere ajuste` con el ajuste identificado, implementado y probado. Una operacion `Bloqueada` o `No probada` bloquea solo su parte de T009, y la tarea debe declarar explicitamente que parte queda fuera.
- T006 aporta fixtures sanitizados de cada respuesta observada, el TTL del token y la forma real de Fault y de token rechazado. Lo que T006 no observe queda como fixture `sintetico` y se marca en las pruebas.
- Existe al menos un `PerfilSat` con e.firma `Lista`.
- Resultado de T006 (2026-10-05): autenticacion, emitidos, verificacion y descarga `Viable`. `SolicitaDescargaRecibidos` quedo `No probada` (forma confirmada por WSDL, misma firma que emitidos): la creacion de recibidos queda pendiente de una corrida real antes de habilitarse en T009. SOAP Fault real y token rechazado no observados: sus pruebas usan fixtures `sintetico`.

## Reglas de integracion

- La UI nunca llama a SAT ni decide transiciones de estado.
- El worker no crea ni envia solicitudes; `Crear solicitud` guarda primero el registro y despues encola el envio.
- `CodEstatus` de creacion, `EstadoSolicitud`, `CodigoEstadoSolicitud` y los codigos de descarga se conservan por separado.
- `MaterialFirma` y `TokenSat` nunca salen del adaptador ni llegan a logs, SQLite o QML.
- Ninguna respuesta SAT sin sanitizar se persiste ni se muestra.

## Trabajo esperado

1. Implementar el `SatGateway` productivo (D4) y sus pruebas con un servidor HTTP local y `FakeSatGateway`.
2. Implementar `OperacionesSat` productivo (D5-D8), con la tabla de mapeo como pruebas parametrizadas.
3. Agregar `notificar` a `OSIntegration` (adaptador macOS y `FakeOSIntegration`) y el servicio de notificaciones (D1, D9).
4. Conectar la UI: acciones, habilitacion de `Enviar`, mensajes y existencia del ZIP.
5. Reemplazar el adaptador nulo en el composition root.
6. Ejecutar la prueba de humo (D2) y registrar la evidencia en `docs/design/sat-smoke-t009.md`, con el mismo enmascarado y escaneo de secretos de T006.

## Criterios de aceptacion

### Precondicion y contrato

- [ ] Dada la clasificacion de T006 por operacion, cuando se habilita el flujo, entonces solo las operaciones `Viable` o con ajuste probado estan conectadas, y las demas quedan declaradas fuera con su motivo.
- [ ] Dados filtros de emitidos y de recibidos validos, cuando se envian, entonces el adaptador invoca solo la operacion SAT correspondiente con los atributos efectivos de ADR 0013 (verificado con `FakeSatGateway` y el servidor local).

### Mapeo y estados

- [ ] Dada cada fila de la tabla de mapeo, cuando el fake o el servidor local la reproduce, entonces `OperacionesSat` devuelve el resultado o la fase indicados, y el estado final aplicado por T007 coincide con la tabla (prueba parametrizada).
- [ ] Dado un perfil sin credencial `Lista`, cuando se abre el detalle de una solicitud `Creada` de ese perfil, entonces `Enviar` esta deshabilitado con el mensaje de credencial y no hay trafico SAT.
- [ ] Dada una falla en `Preparacion`, `Autenticacion` o `AntesDeEnvio` al enviar, cuando termina, entonces la solicitud vuelve a `Creada` con el mensaje correspondiente y `Enviar` manual disponible.
- [ ] Dado `5008` en una descarga, cuando se aplica, entonces el paquete queda en `Error` y `Reintentar descarga` no se ofrece para ese paquete.

### Sesion, tiempos y seguridad

- [ ] Dado un token vigente, cuando siguen varias operaciones del mismo perfil, entonces se autentica una sola vez. Al expirar el TTL, ante una falla de autenticacion, un token rechazado, un cambio de credencial o el cierre, el token se invalida y la siguiente operacion autentica de nuevo.
- [ ] Dado un servidor local que no responde, cuando vence el timeout de cada operacion (con reloj y deadline controlables), entonces se aborta la peticion y la fase reportada distingue `AntesDeEnvio` de `DespuesDeEnvio`. Dada una cancelacion durante una descarga, la escritura se aborta sin archivo final.
- [ ] Dados los logs, mensajes, notificaciones y SQLite tras ejecutar todas las pruebas, cuando se inspeccionan, entonces no contienen token, firma, contrasena, certificado, RFC completo en mensajes o notificaciones, ni bytes o base64 del ZIP.

### Notificaciones

- [ ] Dada una transicion confirmada a `Terminada`, `ErrorSat`, `Rechazada` o `Vencida`, cuando se hace el commit, entonces se emite una sola notificacion por solicitud y transicion, aunque la verificacion se repita.
- [ ] Dada la descarga del ultimo paquete pendiente, cuando se hace el commit, entonces se emite una sola `Descarga completa: N de N` y ninguna por paquete.
- [ ] Dado un permiso de notificaciones denegado o no disponible, cuando se intenta notificar, entonces los estados, logs y reintentos no cambian, y la UI muestra que las notificaciones estan deshabilitadas.

### Integracion y humo real

- [ ] Dada la app con el adaptador productivo y fakes de red, cuando se crea una solicitud desde la UI, entonces se persiste en `Creada` antes de cualquier llamada y el envio pasa por el ejecutor.
- [ ] Dadas solicitudes pendientes y paquetes `Disponible`, cuando se cierra y reabre la app, entonces el worker continua la verificacion y la descarga, y una solicitud `Enviando` interrumpida queda `EnvioIncierto`.
- [ ] Dada la precondicion cumplida, cuando el usuario ejecuta el humo (D2), entonces la evidencia en `sat-smoke-t009.md` muestra los estados recorridos (`Creada`, `Enviada`, verificaciones, `Terminada`, `Descargado`), los textos de las notificaciones, los paquetes con nombre enmascarado y tamano, el conteo de solicitudes reales (2 como maximo) y la reapertura de la app.
- [ ] Antes del humo, con el llavero bloqueado o la credencial no `Lista`, cuando el usuario intenta enviar, entonces la solicitud sigue en `Creada` y no hay trafico SAT.
- [ ] El escaneo de secretos de T006 sobre el arbol y el staging da resultado vacio despues de registrar la evidencia.
- [ ] No se agrega parsing XML, metadata SAT ni funcionalidad comercial.

## Verificacion

1. `ctest --test-dir build --output-on-failure`:
   - `unit`: adaptador con `FakeSatGateway` y fixtures de T006, mapeo parametrizado, sesion de token, mensajes y notificaciones.
   - `infrastructure`: `SatGateway` contra un servidor HTTP local (SOAPAction, headers, `requestSent`, deadlines, Fault, HTTP incompleto, streaming y cancelacion).
   - `integration` y `presentation`: flujo con `FakeOperacionesSat`, UI, `FakeOSIntegration` y reapertura.
2. Humo real manual (D2), ejecutado solo por el usuario siguiendo las reglas de operacion de T006, con la evidencia en `docs/design/sat-smoke-t009.md`.
3. Revision de SQLite, logs, notificaciones y la carpeta de paquetes tras el humo, y escaneo de secretos.

## Definicion de terminado

- El flujo real de crear, enviar, verificar, registrar paquetes y descargar funciona para las operaciones habilitadas por T006.
- La UI muestra estados y mensajes sin conocer detalles SOAP; `MaterialFirma` y el token no salen del adaptador.
- Los ZIP quedan fuera de SQLite, y los logs y la evidencia quedan sanitizados.
- `T010` puede ejecutar las pruebas de aceptacion del MVP sin dependencias faltantes.

## Resultado

Pendiente.

## Riesgos y notas

- Si la evidencia de T006 contradice filas de la tabla de mapeo, se ajustan el adaptador y la tabla, no el ejecutor. Si cambia el significado de un estado, se requiere un ADR.
- Una prueba SAT exitosa no garantiza que todos los rangos o filtros se comporten igual.
- Las credenciales y los limites del SAT son recursos reales del usuario; el humo respeta el tope de D2.
- Esta tarea no convierte la app en fuente de verdad fiscal.

## Referencias

- `T005`, `T005.1`, `T006`, `T007`, `T008`, `T010`
- `docs/web-service.md`, `docs/design/operational-rules.md`, `docs/requirements.md` §7.6
- `ADR 0009`, `ADR 0010`, `ADR 0013`, `ADR 0014`, `ADR 0015`, `ADR 0017`
- `docs/meetings/T009-refinamiento/` (bitacora y rondas del refinamiento)
