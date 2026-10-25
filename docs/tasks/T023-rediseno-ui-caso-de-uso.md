# T023: Rediseno de la UI centrado en las facturas del perfil fiscal

## Estado

Completada (2026-10-24). T023.1 a T023.6 completadas; prueba manual del usuario aprobada (2026-10-24).

## Prioridad y tamano

- Prioridad: alta (define el recorrido principal; T020, T021 y T024 conviene hacerlas despues).
- Tamano: grande; dividida en cinco subtareas verticales (D18).

## Objetivo

Que la pantalla principal muestre las facturas de un perfil fiscal por mes y permita obtener las que falten, sin exponer al usuario la mecanica de solicitudes de descarga masiva del SAT.

## Contexto

La UI actual sigue la estructura del servicio web del SAT: la pantalla principal es una lista de solicitudes de descarga masiva (`SolicitudesPage`), y las facturas solo se ven desde el detalle de una solicitud terminada ("Ver facturas", T015.3). Para un usuario que no conoce el servicio web, solicitudes, paquetes y estados de verificacion son conceptos tecnicos sin significado fiscal.

Lo que el usuario necesita es: elegir un perfil fiscal, ver sus facturas emitidas y recibidas de un mes y, si no estan en el equipo, descargarlas. Las solicitudes, paquetes y reintentos siguen existiendo como mecanismo interno y como diagnostico.

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | El periodo del flujo principal es exclusivamente un mes natural; no se ofrece rango libre. | Aprobada por usuario | Refinamiento, 2026-10-19. |
| D2 | "Descargar facturas" obtiene emitidas y recibidas juntas y crea internamente las dos solicitudes SAT necesarias. | Aprobada por usuario | Refinamiento, 2026-10-19. |
| D3 | La vista principal es un catalogo fiscal por perfil y mes: muestra CFDI, nunca paquetes. Solicitudes, paquetes y verificaciones no forman parte del recorrido normal. | Aprobada por usuario | Refinamiento, 2026-10-19. |
| D4 | T023 entrega el flujo vertical completo. El barrido transversal de vocabulario, manual, capturas y video es T024. | Aprobada por usuario | Refinamiento, 2026-10-19. |
| D5 | Antes de codigo productivo se valida un prototipo navegable del flujo principal. | Aprobada por usuario | Refinamiento, 2026-10-19. |
| D6 | Historial secundario de descargas por perfil y mes. No hay descargas con filtros SAT: la descarga guiada obtiene siempre el mes de ambas direcciones. | Aprobada por usuario | Refinamiento, 2026-10-19. |
| D7 | Mes en curso por tramos: la primera descarga cubre desde el dia 1 hasta el instante de la accion; "Actualizar mes" cubre desde el segundo siguiente a la cobertura hasta el instante de la accion, y solo se ofrece cuando la cobertura termina en un dia anterior a hoy. La UI indica "Facturas disponibles hasta el <dia> de <mes>, <hora>". La app abre en el mes anterior al actual. | Aprobada por usuario | Rondas 02 y 03. Evita repetir parametros SAT (5002 de por vida, 5005 duplicada vigente). Precision de instante: ronda 04. |
| D8 | Cinco estados de mes: Sin descargar, Preparando, Disponibles, Disponibles parcialmente, Requiere atencion (regla en D11). | Aprobada por usuario | Ronda 02. |
| D9 | El mes se elige en una lista lateral con los 12 meses del anio, selector de anio, estado y numero de facturas por mes. Bajo ~760 px de ancho se vuelve un combo. | Aprobada por usuario | Ronda 03. |
| D10 | `SolicitudesPage` sale del recorrido normal y queda en el menu "Ir > Diagnostico". `DetalleSolicitudPage` es la vista de diagnostico, accesible desde Historial, "Requiere atencion" y esa lista. `NuevaSolicitudPage` y Cmd+N salen de la navegacion. | Aprobada por usuario | Ronda 03 y respuesta del usuario. |
| D17 | Un mes cerrado con cobertura incompleta (por ejemplo, del 1 al 10) ofrece "Completar mes": una descarga por cada hueco no cubierto hasta el fin del mes (ADR 0020). Nunca se repite un rango ya cubierto. Los meses cerrados completos no se actualizan. | Aprobada por usuario | Ronda 04 (calidad); enmienda de huecos aprobada 2026-10-20. |
| D18 | T023 es epica con T023.1 (prototipo y ADR), T023.2 (fundamentos de catalogo y disponibilidad), T023.3 (descarga guiada), T023.4 (pantalla Facturas) y T023.5 (historial, diagnostico, notificaciones y menu bar). | Aprobada por usuario | Ronda 04. |
| D21 | Huecos y causas superadas (ADR 0020): con algun tramo acreditado el mes es Disponibles parcialmente aunque no empiece el dia 1; un fallo deja de poner el mes en Requiere atencion si descargas posteriores de la misma direccion cubren integramente su tramo (queda en Diagnostico). | Aprobada por usuario | Desarrollo T023.2, 2026-10-20. |
| D22 | Una solicitud Rechazada con `CodigoEstadoSolicitud` 5004 (sin informacion) acredita su tramo como conjunto vacio y no es causa de atencion; Rechazada con otro codigo sigue siendolo. | Aprobada por usuario | Prueba manual de T023.5, 2026-10-21; `docs/web-service.md` 6.2. Implementa T023.6. |
| D20 | La lista de CFDI muestra solo la contraparte: Receptor en Emitidas y Emisor en Recibidas; el propio perfil no se repite. | Aprobada por usuario | Revision del prototipo T023.1, 2026-10-20. |
| D11 | Cobertura por direccion: solo acreditan solicitudes canonicas (mismo perfil, direccion, CFDI/Vigente, sin filtros, rango dentro del mes, no eliminadas) terminadas con todos sus paquetes descargados, ZIP presente y legible e indice completo, o terminadas con conjunto de paquetes vacio persistido por la verificacion SAT. La cobertura es la union contigua de tramos desde el dia 1 a las 00:00:00; un tramo despues de un hueco no la extiende. La frontera objetivo es el fin del mes (cerrado) o cualquier instante de hoy (en curso): una cobertura que termina en un dia anterior a hoy no alcanza la frontera. Estados con precedencia: Requiere atencion (EnvioIncierto, EnvioFallido, ErrorSat, Rechazada salvo `CodigoEstadoSolicitud` 5004 (D22), Vencida, 5002, 5005, paquete Error o Vencido, ZIP faltante o ilegible, indice ilegible u origen no disponible, indexacion pendiente sin trabajo programado) > Preparando (solicitud Creada, Enviando, Enviada, Aceptada o EnProceso, paquete descargandose, indexacion programada) > Disponibles (ambas direcciones cubiertas hasta la frontera) > Disponibles parcialmente (alguna cobertura en al menos una direccion, sin trabajo ni error) > Sin descargar (ninguna cobertura). | Recomendacion tecnica | Rondas 02 y 04. Las solicitudes con filtros o rango no canonico no acreditan cobertura, pero sus CFDI aparecen en el catalogo. |
| D12 | La descarga guiada planifica ambas direcciones en una transaccion antes de crear nada. Por direccion: si la cobertura ya alcanza la frontera, no hace nada; si hay una solicitud canonica en curso, la reutiliza; si falta un tramo, calcula el siguiente desde el fin de la cobertura contigua y solo entonces aplica `evaluarDuplicado`: `RequiereConfirmacion` pide confirmacion explicita una vez por direccion (cancelar no afecta a la otra direccion), `Bloqueado` reutiliza. Con 5002 en una direccion, la otra se crea si esta libre, el mes queda en Requiere atencion y no se ofrece recrear la afectada; con 5005 se lleva al diagnostico. Repetir la accion mientras hay trabajo en curso no crea nada. Solo la accion del usuario crea solicitudes. | Recomendacion tecnica | Rondas 02 y 04; ADR 0015 y 0018. Tramos distintos producen `dedup_key` distintas. |
| D13 | El catalogo deduplica por UUID no vacio normalizado (mayusculas). Fuente canonica: primero solicitudes canonicas guiadas, luego la indexacion mas antigua, luego el menor id de paquete. Si se elimina la fuente, se usa la siguiente por el mismo orden. Los CFDI sin UUID no se deduplican. Los CFDI de solicitudes o paquetes eliminados no aparecen ni cuentan. | Recomendacion tecnica | Ronda 04. |
| D14 | Se persiste un lote de descarga guiada (perfil por `perfil_sat_id`, mes, fecha, solicitudes vinculadas por direccion y tramo) con migracion nueva; el historial se lee de esos lotes. Si despues se elimina una solicitud del lote, la fila conserva mes y fecha, recalcula su resultado sin ella e indica "Parte de esta descarga se elimino". Los lotes de un perfil eliminado se ocultan con el perfil y no se reasignan a un perfil recreado con el mismo RFC. Las solicitudes previas a T023 aparecen en Historial como "Descarga anterior", solo con diagnostico. | Recomendacion tecnica | Rondas 02, 03 y 04. |
| D15 | Se registra un ADR "Cobertura mensual guiada de CFDI" con D7, D11-D14, D17 y la frontera aplicacion/QML. | Recomendacion tecnica | Ronda 02. |
| D16 | Perfil inicial: el ultimo usado (preferencia local), o el primer perfil activo. Se recuerda por perfil el ultimo mes visto durante la sesion. | Recomendacion tecnica | Ronda 03. |
| D19 | Fechas, meses, "hoy", "mes anterior" y fechas SAT se calculan con reloj inyectable en `America/Mexico_City`, tomando el instante al iniciar la accion. En enero el mes por omision es diciembre del anio anterior. | Recomendacion tecnica | Ronda 04. |

## Subtareas

| Tarea | Resultado |
| --- | --- |
| [T023.1](T023.1-prototipo-y-adr-cobertura.md) | Prototipo navegable aprobado por el usuario y ADR de cobertura mensual |
| [T023.2](T023.2-catalogo-mensual-y-disponibilidad.md) | Migracion de lotes, consulta de catalogo mensual y regla de disponibilidad |
| [T023.3](T023.3-descarga-guiada.md) | Descarga guiada por mes y tramos |
| [T023.4](T023.4-pantalla-facturas.md) | Pantalla Facturas productiva |
| [T023.5](T023.5-historial-diagnostico-notificaciones.md) | Historial, diagnostico, notificaciones y menu bar |
| [T023.6](T023.6-sin-facturas-5004.md) | Periodo sin facturas (Rechazada 5004) no es error |

Orden: T023.1 > T023.2 > T023.3 y T023.4 (en paralelo cuando T023.2 cierre sus contratos) > T023.5 > T023.6.

## Alcance

### Incluye

- Pantalla Facturas por perfil y mes, descarga guiada con disponibilidad, historial y diagnostico, segun las subtareas.

### No incluye

- Cambios al protocolo SAT o al worker: el worker sigue continuando solicitudes existentes.
- Descargas con RFC contraparte, tipo de comprobante, complemento u otros filtros SAT.
- Metadata, vigencia o cancelacion, exportacion contable o conciliacion.
- Vocabulario de menu bar, notificaciones y mensajes fuera del nuevo flujo (salvo el item "Ver facturas"); manual, capturas y video (T024).
- Logo (T020).

## Dependencias

- T013, T014.x, T015.x (indice, explorador, PDF), ADR 0013, 0015, 0016, 0017 y 0018.
- T024 depende de T023.

## Criterios de aceptacion

- [x] T023.1 a T023.6 completadas.
- [x] Prueba manual del usuario con un perfil real sobre la app productiva: un mes cerrado ya descargado se ve con sus facturas y 3 CFDI coinciden con su XML (UUID, RFC emisor, total); la descarga de un mes nuevo se completa sin ver "solicitud", "paquete" ni "verificacion" fuera de Diagnostico.

## Verificacion

Cada subtarea define la suya. Al cierre de la epica: `ctest` completo, build `-Werror`, `git diff --check` y la prueba manual del usuario. Solo el usuario hace operaciones SAT reales.

## Resultado

La pantalla principal muestra las facturas del perfil fiscal por mes y permite obtener las que falten sin exponer solicitudes, paquetes ni verificaciones fuera de Diagnostico (T023.1 a T023.6). El usuario confirmo el 2026-10-24 la prueba manual con su perfil real en la app productiva. El barrido de vocabulario, manual, capturas y video queda en T024.

## Riesgos

- Los CFDI pueden tardar en aparecer en el servicio del SAT despues de timbrarse; un tramo del mes en curso puede no incluir facturas recientes aunque su fecha caiga dentro. La UI dice "disponibles hasta" y no promete completitud.
- Las solicitudes previas con rangos no mensuales no acreditan cobertura: un mes descargado a mano puede verse "Sin descargar" aunque tenga facturas visibles; la UI muestra las facturas y la descarga guiada crea los tramos faltantes.
- Ancho minimo de ventana mayor (~760 px) con la lista lateral.

## Referencias

- `docs/meetings/T023-refinamiento/` (contexto, rondas 02 a 04, bitacora).
- `docs/requirements.md`, `docs/architecture.md`, `docs/design/operational-rules.md`, `docs/design/ui-ux-v2/`, `docs/web-service.md`.
- T013, T014.3, T014.4, T015.2, T015.3, T015.4, T024.
