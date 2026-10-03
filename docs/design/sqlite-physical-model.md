# Modelo fisico SQLite

Estado: implementado en migracion 001 (T001).

## Objetivo

Fijar el esquema fisico SQLite que soporta la persistencia local del MVP:
perfiles SAT, referencias de credencial, solicitudes, paquetes ZIP, logs
sanitizados y configuracion local. Este documento traduce el esquema logico de
`docs/architecture.md` §11 y las reglas de `docs/design/operational-rules.md`
a tablas, restricciones e indices concretos.

Fuente ejecutable:

```text
src/infrastructure/persistence/migrations/001_initial_schema.sql
```

Este documento no agrega funcionalidades al MVP. Repositorios, runner Qt,
secretos, worker y almacenamiento ZIP pertenecen a T003, T005, T007 y T008.

Referencias: ADR 0004, ADR 0013, ADR 0014, ADR 0015, ADR 0016,
`docs/web-service.md` §5.3-5.5 y `docs/tasks/T001-modelo-fisico-sqlite.md`.

## Convenciones de representacion

| Dato | Representacion fisica | Restriccion en esquema |
| --- | --- | --- |
| Identificador local | `TEXT` UUID en minusculas, sin llaves, 36 caracteres (`8-4-4-4-12`). | `CHECK` de longitud 36 y `GLOB` con clase `[0-9a-f]` en cada una de las 32 posiciones hexadecimales y `-` literal solo en las posiciones 9, 14, 19 y 24, en cada `id` de tabla. |
| Timestamp interno | `TEXT` UTC ISO-8601 con milisegundos y sufijo `Z` (`yyyy-MM-ddTHH:mm:ss.zzzZ`, ADR 0016). | Sin `CHECK` de formato (DC7). Lo escribe la aplicacion. |
| Fecha de filtro SAT | `TEXT` hora Centro de Mexico sin offset (`YYYY-MM-DDThh:mm:ss`). | `CHECK` de longitud 19 y patron `GLOB` (DC7), porque forma parte de `dedup_key`. |
| Booleano | `INTEGER NOT NULL` con `0` o `1`. | `CHECK (x IN (0, 1))`. |
| JSON | `TEXT`. | `json_valid` y tipo esperado (`array` u `object`) evaluados dentro de `CASE` para no invocar `json_type` sobre texto invalido. |
| Enumerado propio | `TEXT` con el valor literal del catalogo. | `CHECK (x IN (...))`. Ampliar un catalogo exige migracion. |
| Codigo o mensaje SAT | `TEXT` libre. | Sin catalogo cerrado: SAT puede ampliar codigos (ADR 0015). |
| RFC | `TEXT` normalizado: mayusculas, sin espacios, 12 o 13 caracteres. | `length BETWEEN 12 AND 13` y solo `A-Z`, `0-9`, `&`, `Ñ` (DC9). |
| Texto obligatorio | `TEXT NOT NULL`. | `length(trim(x)) > 0` cuando un valor vacio no tiene sentido. |

Reglas generales:

- No se usan tablas `STRICT`, columnas generadas, operadores `->`/`->>`,
  `RETURNING` ni `UPSERT` (DM4). La afinidad de tipo de SQLite no impide
  guardar un tipo distinto; los repositorios de T003 deben enlazar valores con
  el tipo declarado.
- Los timestamps no tienen `DEFAULT`; la aplicacion es la unica fuente de hora.
  La excepcion es la semilla de `configuracion_app`, que usa
  `strftime('%Y-%m-%dT%H:%M:%fZ', 'now')` con el mismo formato.
- Un `CHECK` que evalua a `NULL` se considera satisfecho en SQLite. Por eso las
  restricciones multicolumna comparan con `IS NULL`/`IS NOT NULL` explicitos y
  no dependen de comparaciones con valores nulos.
- No hay triggers. Las transiciones de negocio viven en aplicacion/T007.
- Las claves foraneas no declaran `ON DELETE` ni `ON UPDATE` (NO ACTION, DC10).
  Solo `credencial_sat` admite borrado fisico. `PRAGMA foreign_keys=ON` lo
  activa T003 por conexion; la migracion no puede fijarlo.

## Tablas

### `perfil_sat`

RFC/contribuyente operado por el usuario local.

| Columna | Tipo | Nulo | Default | Restricciones |
| --- | --- | --- | --- | --- |
| `id` | `TEXT` | No | - | PK, UUID. |
| `rfc` | `TEXT` | No | - | RFC normalizado. Unico entre perfiles no eliminados. |
| `nombre` | `TEXT` | No | - | No vacio (DC9). |
| `activo` | `INTEGER` | No | `1` | `0` o `1`. |
| `creado_en` | `TEXT` | No | - | Timestamp. |
| `actualizado_en` | `TEXT` | No | - | Timestamp. |
| `eliminado_en` | `TEXT` | Si | - | Eliminacion logica. |

- `ux_perfil_sat_rfc_vigente`: `UNIQUE (rfc) WHERE eliminado_en IS NULL`.
  `activo=0` no libera el RFC; solo `eliminado_en` lo libera.
- No existe columna `rfc_normalizado`: `rfc` ya se guarda normalizado.
- Consulta normal: `WHERE eliminado_en IS NULL`. El selector de nueva solicitud
  agrega `activo = 1`. El historial de solicitudes puede leer perfiles
  eliminados por `id`.

### `credencial_sat`

Referencias no secretas a la e.firma vigente de un perfil. Relacion 1 a 0..1.

| Columna | Tipo | Nulo | Default | Restricciones |
| --- | --- | --- | --- | --- |
| `id` | `TEXT` | No | - | PK, UUID. |
| `perfil_sat_id` | `TEXT` | No | - | FK `perfil_sat(id)`. Unico. |
| `certificado_ref` | `TEXT` | No | - | Referencia opaca no vacia. |
| `llave_privada_ref` | `TEXT` | No | - | Referencia opaca no vacia. |
| `contrasena_ref` | `TEXT` | No | - | Referencia opaca no vacia. |
| `registrada_en` | `TEXT` | No | - | Timestamp. |
| `actualizada_en` | `TEXT` | No | - | Timestamp. |
| `numero_serie` | `TEXT` | Si | - | Migracion 002. Serie del certificado, 1 a 64 caracteres. |
| `vigente_desde` | `TEXT` | Si | - | Migracion 002. Timestamp UTC `yyyy-MM-ddTHH:mm:ss.zzzZ`. |
| `vigente_hasta` | `TEXT` | Si | - | Migracion 002. Timestamp UTC; `vigente_desde < vigente_hasta`. |

- `ux_credencial_sat_perfil`: `UNIQUE (perfil_sat_id)`. Tambien sirve la FK.
- Sin `eliminado_en`: al eliminar la credencial vigente se borra la fila (D006).
  El reemplazo actualiza la misma fila; no hay historial en SQLite.
- Nunca guarda certificado, llave, contrasena ni token; solo referencias.
- Las referencias usan el formato `scs1:<uuid>:cert|container|password` (T005): sin rutas ni secretos.
- La metadata de 002 es anulable para filas anteriores; las escrituras nuevas la exigen y la vigencia va
  completa o ausente.

### `solicitud_masiva`

Entidad central. Los filtros SAT viven en columnas explicitas; no existe
`filtros_sat_json` (DC1).

| Columna | Tipo | Nulo | Default | Restricciones |
| --- | --- | --- | --- | --- |
| `id` | `TEXT` | No | - | PK, UUID. |
| `perfil_sat_id` | `TEXT` | No | - | FK `perfil_sat(id)`. Referencia historica local. |
| `id_solicitud_sat` | `TEXT` | Si | - | No vacio. Unico cuando no es nulo. |
| `tipo_cfdi` | `TEXT` | No | - | `emitidos`, `recibidos`. |
| `operacion_sat` | `TEXT` | No | - | `SolicitaDescargaEmitidos`, `SolicitaDescargaRecibidos`. |
| `rfc_solicitante` | `TEXT` | No | - | RFC normalizado. |
| `rfc_emisor` | `TEXT` | Si | - | RFC normalizado. |
| `rfc_receptor` | `TEXT` | Si | - | RFC normalizado. |
| `rfc_receptores_json` | `TEXT` | Si | - | Arreglo JSON no vacio. |
| `tipo_solicitud_sat` | `TEXT` | No | `'CFDI'` | Solo `CFDI`. |
| `estado_comprobante_sat` | `TEXT` | No | `'Vigente'` | Solo `Vigente`. |
| `fecha_inicial_sat` | `TEXT` | No | - | Fecha SAT `YYYY-MM-DDThh:mm:ss`. |
| `fecha_final_sat` | `TEXT` | No | - | Fecha SAT. `fecha_inicial_sat <= fecha_final_sat`. |
| `tipo_comprobante` | `TEXT` | Si | - | `I`, `E`, `T`, `N`, `P` (DC3). |
| `complemento` | `TEXT` | Si | - | Texto libre no vacio (DC3). |
| `dedup_key` | `TEXT` | No | - | Patron `v<n>:<hash>`. |
| `estado_local` | `TEXT` | No | `'Creada'` | `Creada`, `Enviando`, `Enviada`, `EnvioFallido`, `EnvioIncierto`. |
| `cod_estatus_solicitud` | `TEXT` | Si | - | `CodEstatus` de creacion. Libre. |
| `mensaje_solicitud_sat` | `TEXT` | Si | - | Mensaje de creacion. |
| `estado_solicitud_sat` | `TEXT` | Si | - | `Aceptada`, `EnProceso`, `Terminada`, `Error`, `Rechazada`, `Vencida`. |
| `codigo_estado_solicitud` | `TEXT` | Si | - | `CodigoEstadoSolicitud` de verificacion. Libre. |
| `mensaje_verificacion_sat` | `TEXT` | Si | - | Mensaje de verificacion. |
| `numero_cfdi` | `INTEGER` | Si | - | `>= 0`. |
| `creada_en` | `TEXT` | No | - | Timestamp. |
| `envio_iniciado_en` | `TEXT` | Si | - | Intento de envio registrado. |
| `enviada_en` | `TEXT` | Si | - | Momento en que se obtuvo `IdSolicitud`. |
| `ultima_verificacion_en` | `TEXT` | Si | - | Ultima verificacion SAT registrada. |
| `siguiente_verificacion_en` | `TEXT` | Si | - | Programacion del worker (T007). |
| `verificaciones_sin_cambio` | `INTEGER` | No | `0` | `>= 0`. Base del backoff (T007). |
| `ultimo_error` | `TEXT` | Si | - | Resumen sanitizado. |
| `verificacion_pendiente` | `INTEGER` | No | `0` | `0` o `1`. |
| `descarga_pendiente` | `INTEGER` | No | `0` | `0` o `1`. |
| `accion_pendiente_en` | `TEXT` | Si | - | Momento de la intencion manual mas antigua activa. |
| `eliminado_en` | `TEXT` | Si | - | Eliminacion logica. |

Restricciones de tabla:

| Nombre | Regla |
| --- | --- |
| `ck_solicitud_operacion` | `emitidos` <=> `SolicitaDescargaEmitidos`; `recibidos` <=> `SolicitaDescargaRecibidos` (ADR 0013). |
| `ck_solicitud_rfcs` | Emitidos: `rfc_emisor = rfc_solicitante` no nulo, `rfc_receptor` nulo, contraparte opcional en `rfc_receptores_json`. Recibidos: `rfc_receptor = rfc_solicitante` no nulo, `rfc_receptores_json` nulo, contraparte opcional en `rfc_emisor` (DC2). |
| `ck_solicitud_rango_fechas` | `fecha_inicial_sat <= fecha_final_sat` (comparacion lexicografica valida por formato fijo). |
| `ck_solicitud_id_sat_enviada` | `id_solicitud_sat IS NOT NULL` si y solo si `estado_local = 'Enviada'`. |
| `ck_solicitud_enviada_en` | `enviada_en IS NOT NULL` si y solo si `estado_local = 'Enviada'`. |
| `ck_solicitud_codigo_creacion` | `Enviada` exige `cod_estatus_solicitud`. `Creada` y `Enviando` no tienen codigo ni mensaje de creacion. |
| `ck_solicitud_intento_envio` | `Creada` sin `envio_iniciado_en`. `Enviando`, `Enviada` y `EnvioIncierto` con `envio_iniciado_en`. `EnvioFallido` libre (puede fallar antes o despues de iniciar). |
| `ck_solicitud_verificacion` | Sin `id_solicitud_sat` no hay estado SAT, codigo/mensaje de verificacion, `numero_cfdi`, `ultima_verificacion_en` ni `siguiente_verificacion_en`. `estado_solicitud_sat` no nulo exige `ultima_verificacion_en`. |
| `ck_solicitud_accion_pendiente` | `accion_pendiente_en IS NOT NULL` si y solo si `verificacion_pendiente = 1 OR descarga_pendiente = 1` (DC5). |

Consecuencias de las restricciones:

- `estado_solicitud_sat` no nulo implica `id_solicitud_sat` y por tanto
  `estado_local = 'Enviada'`. Un `EnvioFallido` o `EnvioIncierto` nunca tiene
  estado SAT inventado.
- El esquema no puede distinguir si `estado_solicitud_sat` se escribio desde
  creacion o verificacion; T007 garantiza que solo verificacion lo escribe.
  El esquema si impide escribirlo sin `ultima_verificacion_en`.
- `Rechazada` y `Vencida` solo existen como valores de `estado_solicitud_sat`;
  el vencimiento de paquete no tiene representacion en esta columna.
- `rfc_receptores_json` se valida como arreglo no vacio. La normalizacion,
  deduplicacion y orden de sus elementos los aplica dominio/T003; un `CHECK` no
  puede recorrer el arreglo sin subconsultas.
- El formato exacto `v1:<sha256-hex-lowercase>` lo garantiza T003. El esquema
  solo exige prefijo de version (`v[1-9]*:?*`) para no requerir reconstruir la
  tabla cuando una version futura cambie el formato.

Reglas de consulta con `eliminado_en`:

- Lista, detalle, worker, recuperacion y acciones manuales filtran
  `eliminado_en IS NULL`. Detalle de una solicitud eliminada responde "no
  encontrada".
- `evaluarDuplicado` es la unica lectura normal que incluye eliminadas, porque
  una coincidencia eliminada produce `RequiereConfirmacion`.
- Antes de aplicar un resultado SAT, el ejecutor relee la fila y descarta el
  resultado si `eliminado_en` ya no es nulo.

### `paquete_solicitud`

Paquete ZIP reportado por SAT. Los bytes viven fuera de SQLite.

| Columna | Tipo | Nulo | Default | Restricciones |
| --- | --- | --- | --- | --- |
| `id` | `TEXT` | No | - | PK, UUID. |
| `solicitud_masiva_id` | `TEXT` | No | - | FK `solicitud_masiva(id)`. |
| `id_paquete_sat` | `TEXT` | No | - | `IdPaquete` SAT no vacio. |
| `estado_descarga` | `TEXT` | No | `'Disponible'` | `Disponible`, `Descargando`, `Descargado`, `Error`, `Vencido`. |
| `ruta_local` | `TEXT` | Si | - | No vacia. Formato definido por T008. |
| `disponible_en` | `TEXT` | No | - | Momento de registro del paquete reportado por SAT. |
| `descarga_iniciada_en` | `TEXT` | Si | - | Ultimo inicio de descarga (DC9). |
| `descargado_en` | `TEXT` | Si | - | Momento en que se confirmo el archivo final. |
| `vencimiento_estimado_en` | `TEXT` | Si | - | Fecha estimada calculada por T007. |
| `vencido_en` | `TEXT` | Si | - | Momento en que se marco `Vencido`. |
| `motivo_vencimiento` | `TEXT` | Si | - | `solicitud_expirada`, `paquete_expirado`, `vencimiento_estimado`. |
| `origen_vencimiento` | `TEXT` | Si | - | `SAT`, `estimacion_local`. |
| `reconciliado_en` | `TEXT` | Si | - | Ultima reconciliacion con el sistema de archivos. |
| `codigo_descarga_sat` | `TEXT` | Si | - | Codigo de `Descargar`. Libre. |
| `mensaje_descarga_sat` | `TEXT` | Si | - | Mensaje de `Descargar`. |
| `ultimo_error` | `TEXT` | Si | - | Resumen sanitizado. |
| `eliminado_en` | `TEXT` | Si | - | Eliminacion logica. |

Restricciones de tabla:

| Nombre | Regla |
| --- | --- |
| `ck_paquete_descargado` | `Descargado` si y solo si `descargado_en` no nulo. `Descargado` exige `ruta_local`. |
| `ck_paquete_descargando` | `Descargando` exige `descarga_iniciada_en`. |
| `ck_paquete_vencido` | `Vencido` si y solo si `motivo_vencimiento`, `origen_vencimiento` y `vencido_en` no nulos. |
| `ck_paquete_motivo_origen` | `solicitud_expirada` y `paquete_expirado` con origen `SAT`; `vencimiento_estimado` con origen `estimacion_local`; o ambos nulos. |

- `estado_descarga` no tiene `EliminadoLocalmente`; la eliminacion es solo
  `eliminado_en`.
- `vencimiento_estimado_en` se conserva al vencer; `vencido_en` registra cuando
  se marco `Vencido`.
- `ruta_local` puede existir en estados distintos de `Descargado` (por ejemplo,
  ruta determinista calculada al iniciar); solo `Descargado` la exige.
- Consulta normal: `eliminado_en IS NULL`. Los paquetes se eliminan logicamente
  solo junto con su solicitud.

### `log_solicitud`

Eventos sanitizados por solicitud.

| Columna | Tipo | Nulo | Default | Restricciones |
| --- | --- | --- | --- | --- |
| `id` | `TEXT` | No | - | PK, UUID. |
| `solicitud_masiva_id` | `TEXT` | No | - | FK `solicitud_masiva(id)` (DC4). |
| `tipo_evento` | `TEXT` | No | - | Catalogo cerrado (ver abajo). |
| `origen` | `TEXT` | No | - | `worker`, `usuario`, `recuperacion` (DC8). |
| `origen_codigo_sat` | `TEXT` | Si | - | `creacion`, `verificacion`, `descarga` (DC8). |
| `codigo_sat` | `TEXT` | Si | - | Codigo SAT libre. |
| `mensaje_sat` | `TEXT` | Si | - | Mensaje SAT sanitizado. |
| `payload_resumen_json` | `TEXT` | Si | - | Objeto JSON valido. |
| `creado_en` | `TEXT` | No | - | Timestamp. |
| `eliminado_en` | `TEXT` | Si | - | Eliminacion logica. |

- `ck_log_codigo_sat`: `origen_codigo_sat` no nulo si y solo si `codigo_sat`
  no nulo; `mensaje_sat` exige `codigo_sat`. Asi un codigo nunca queda sin
  indicar si proviene de creacion, verificacion o descarga.
- Catalogo `tipo_evento` (DC8): `solicitud_creada`, `duplicado_confirmado`,
  `envio_iniciado`, `solicitud_enviada`, `envio_fallido`, `envio_incierto`,
  `verificacion_realizada`, `verificacion_fallida`, `paquetes_registrados`,
  `descarga_iniciada`, `paquete_descargado`, `descarga_fallida`,
  `descarga_interrumpida`, `paquete_reconciliado`, `paquete_vencido`,
  `archivo_huerfano`, `accion_pendiente_registrada`,
  `accion_pendiente_descartada`. No existe `solicitud_eliminada`: el log
  quedaria oculto por la misma eliminacion logica.
- `IdSolicitud`, `IdPaquete`, operacion SAT y filtros normalizados van dentro de
  `payload_resumen_json` cuando aporten diagnostico. No se agrega
  `paquete_solicitud_id` (DC4).
- La sanitizacion (sin tokens, llave, contrasena, firma, certificado completo,
  `Paquete` ni base64 de ZIP) la aplica `LogSanitizer` antes de persistir; el
  esquema solo garantiza JSON objeto valido.
- Consulta normal del detalle: `solicitud_masiva_id = ? AND eliminado_en IS
  NULL ORDER BY creado_en`.

### `configuracion_app`

Registro unico local.

| Columna | Tipo | Nulo | Default | Restricciones |
| --- | --- | --- | --- | --- |
| `id` | `INTEGER` | No | - | PK, `CHECK (id = 1)`. |
| `inicio_automatico_habilitado` | `INTEGER` | No | `0` | `0` o `1`. |
| `monitoreo_pausado` | `INTEGER` | No | `0` | `0` o `1`. |
| `ultimo_cierre_en` | `TEXT` | Si | - | Timestamp. |
| `actualizada_en` | `TEXT` | No | - | Timestamp. |

- Semilla `INSERT OR IGNORE` con `id = 1`, inicio automatico deshabilitado y
  monitoreo activo. Es la unica sentencia idempotente por si misma de 001 (DM3).
- Sin `eliminado_en`.

## Indices

| Indice | Definicion | Consulta que sirve |
| --- | --- | --- |
| `ux_perfil_sat_rfc_vigente` | `perfil_sat(rfc) WHERE eliminado_en IS NULL`, unico. | Reserva de RFC y busqueda de perfil por RFC. |
| `ux_credencial_sat_perfil` | `credencial_sat(perfil_sat_id)`, unico. | Credencial vigente por perfil y FK. |
| `ux_solicitud_masiva_id_solicitud_sat` | `solicitud_masiva(id_solicitud_sat) WHERE id_solicitud_sat IS NOT NULL`, unico. | Unicidad de `IdSolicitud` (incluye eliminadas). |
| `ux_solicitud_masiva_dedup_bloqueante` | Predicado DM6, unico. | Defensa estructural contra duplicados bloqueantes. |
| `ix_solicitud_masiva_dedup` | `solicitud_masiva(dedup_key)`. | `evaluarDuplicado`: todas las coincidencias, incluidas eliminadas. |
| `ix_solicitud_masiva_monitoreo` | `(estado_local, estado_solicitud_sat, siguiente_verificacion_en) WHERE eliminado_en IS NULL`. | Worker: solicitudes `Enviada` por verificar ordenadas por `siguiente_verificacion_en`. Recuperacion: `estado_local IN ('Creada','Enviando')`. |
| `ix_solicitud_masiva_accion_pendiente` | `(accion_pendiente_en) WHERE eliminado_en IS NULL AND accion_pendiente_en IS NOT NULL`. | Consumo de intenciones al reanudar monitoreo. |
| `ix_solicitud_masiva_perfil` | `(perfil_sat_id, creada_en)`. | FK, solicitudes por perfil y validacion previa a eliminar perfil. |
| `ix_solicitud_masiva_lista` | `(creada_en) WHERE eliminado_en IS NULL`. | Lista principal ordenada por creacion. |
| `ux_paquete_solicitud_id_paquete_sat` | `(solicitud_masiva_id, id_paquete_sat)`, unico. | Unicidad de paquete, FK, paquetes del detalle y conteo por estado en `evaluarDuplicado`. |
| `ix_paquete_solicitud_descarga` | `(estado_descarga, solicitud_masiva_id) WHERE eliminado_en IS NULL`. | Paquetes `Disponible`/`Error` por descargar y `Descargando` en recuperacion. |
| `ix_paquete_solicitud_vencimiento_estimado` | `(vencimiento_estimado_en) WHERE eliminado_en IS NULL AND vencimiento_estimado_en IS NOT NULL AND estado_descarga IN ('Disponible','Descargando','Error')`. | Scheduler de vencimiento estimado. |
| `ix_log_solicitud_detalle` | `(solicitud_masiva_id, creado_en)`. | Logs del detalle en orden cronologico y FK. |

Los indices parciales con `eliminado_en IS NULL` solo se usan si la consulta
incluye literalmente `eliminado_en IS NULL` en su `WHERE`. Los repositorios de
T003 deben escribir las consultas con ese termino. Sin `ANALYZE`, el
planificador puede preferir `ix_paquete_solicitud_descarga` para la consulta de
vencimiento estimado; ambos planes son indexados.

### Predicado de duplicados (DM6)

```sql
CREATE UNIQUE INDEX ux_solicitud_masiva_dedup_bloqueante
    ON solicitud_masiva (dedup_key)
    WHERE eliminado_en IS NULL
      AND (estado_local IN ('Creada', 'Enviando')
           OR (estado_local = 'Enviada'
               AND (estado_solicitud_sat IS NULL
                    OR estado_solicitud_sat IN ('Aceptada', 'EnProceso'))))
```

Cubre los estados inequivocamente bloqueantes: `Creada`, `Enviando`, `Enviada`
sin verificar, `Aceptada` y `EnProceso`. No incluye:

- `Terminada`: bloquea o no segun sus paquetes no eliminados. Un indice no
  puede consultar otra tabla.
- `EnvioIncierto` (D003), `Terminada` sin paquetes no eliminados (D008) y
  `Terminada` con al menos un `Vencido` y el resto `Descargado`/`Vencido`
  (D002): requieren advertencia y confirmacion del usuario, no rechazo.
- `EnvioFallido`, `Error`, `Rechazada`, `Vencida` y eliminadas: requieren
  confirmacion.

La fila con `dedup_key` repetida nunca entra al indice si es eliminada o esta
en estado no bloqueante, por lo que el historial no impide nuevas solicitudes.

### Validacion transaccional de duplicados

`evaluarDuplicado` (T003) corre bajo `BEGIN IMMEDIATE` dentro de la misma
transaccion que inserta la nueva solicitud `Creada`:

1. `SELECT id, estado_local, estado_solicitud_sat, eliminado_en FROM
   solicitud_masiva WHERE dedup_key = ?` (`ix_solicitud_masiva_dedup`).
2. Para cada coincidencia no eliminada con `estado_solicitud_sat =
   'Terminada'`: `SELECT estado_descarga, count(*) FROM paquete_solicitud
   WHERE solicitud_masiva_id = ? AND eliminado_en IS NULL GROUP BY
   estado_descarga` (`ux_paquete_solicitud_id_paquete_sat`).
3. Clasifica cada coincidencia:

| Coincidencia | Resultado |
| --- | --- |
| `Creada`, `Enviando`, `Enviada` (sin estado SAT), `Aceptada`, `EnProceso` | `Bloqueado` |
| `Terminada` con algun paquete `Disponible`, `Descargando` o `Error` | `Bloqueado` |
| `Terminada` con todos sus paquetes en `Descargado` | `Bloqueado` |
| `Terminada` con al menos un `Vencido` y el resto `Descargado` o `Vencido` | `RequiereConfirmacion` (D002) |
| `Terminada` sin paquetes no eliminados | `RequiereConfirmacion` (D008) |
| `EnvioIncierto` | `RequiereConfirmacion` (D003) |
| `EnvioFallido`, `Error`, `Rechazada`, `Vencida` | `RequiereConfirmacion` |
| Eliminada localmente | `RequiereConfirmacion` |

4. Precedencia: `Bloqueado` > `RequiereConfirmacion` > `Libre`.
5. `Bloqueado`: rollback sin insertar. `RequiereConfirmacion` sin confirmacion:
   rollback y se devuelve el motivo a la UI. Con confirmacion explicita: se
   repite la evaluacion completa en una nueva transaccion (el estado pudo
   cambiar) y, si sigue siendo `RequiereConfirmacion`, se inserta la solicitud y
   un log `duplicado_confirmado` con `origen = 'usuario'`.
6. Si el `INSERT` viola `ux_solicitud_masiva_dedup_bloqueante`, T003 lo traduce
   a `Bloqueado`; es la defensa ante una validacion omitida.

`BEGIN IMMEDIATE` toma el bloqueo de escritura antes de leer, de modo que
ninguna otra conexion puede insertar o cambiar paquetes entre la evaluacion y
el `INSERT`.

## Contrato de migraciones

### `schema_migrations` (DM1)

```sql
CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    aplicada_en TEXT NOT NULL
)
```

Pertenece al runner, no a 001. Algoritmo del runner (T003):

1. Abrir conexion y activar `PRAGMA foreign_keys=ON`.
2. Si `schema_migrations` no existe y hay otros objetos de usuario en
   `sqlite_master`: fallar explicitamente sin escribir.
3. Leer `max(version)`. Si es mayor que la ultima migracion conocida: rechazar
   antes de abrir transaccion y sin modificar el archivo.
4. Por cada migracion pendiente, en orden: `BEGIN IMMEDIATE`; crear
   `schema_migrations` si falta; ejecutar cada sentencia; insertar
   `(version, aplicada_en)`; `COMMIT`. Ante cualquier error, `ROLLBACK`: no
   quedan objetos parciales ni version registrada (incluido el bootstrap de
   `schema_migrations`).
5. Una segunda ejecucion encuentra la version aplicada y no ejecuta nada.

La idempotencia la aporta el runner: 001 usa `CREATE` sin `IF NOT EXISTS`
(DM3). Ejecutar 001 dos veces fuera del runner falla, y es intencional.

### Convencion de division (DM2)

- UTF-8 sin BOM.
- Comentarios solo en lineas completas que empiezan con `--` (pueden estar
  dentro del cuerpo de un `CREATE TABLE`). Sin comentarios al final de linea ni
  `/* */`.
- Cada sentencia termina con una linea cuyo contenido, sin espacios, es
  unicamente `;`. El runner acumula lineas hasta esa linea, la descarta y
  ejecuta el bloque con un solo `QSqlQuery::exec`. Bloques que solo contienen
  comentarios o espacios se omiten.
- Prohibidos: `BEGIN`, `COMMIT`, `ROLLBACK`, `SAVEPOINT`, `VACUUM`, `ATTACH`,
  comandos `.` del CLI, triggers y `;` dentro de literales o comentarios.

### Version minima de SQLite (DM4)

La sintaxis se limita a SQLite >= 3.9.0 con JSON1 (`json_valid`, `json_type`,
`json_array_length`), indices parciales, `CHECK` con nombre y `GLOB`. T003
verifica en `QSQLITE` `SELECT sqlite_version(), json_valid('{}')` antes de
migrar y falla si no se cumple.

## Transacciones mapeadas a columnas

Todas las escrituras de esta seccion usan `BEGIN IMMEDIATE` (ADR 0016) y
releen `eliminado_en IS NULL` antes de aplicar resultados.

### Crear solicitud

1. Tx1: `evaluarDuplicado` + `INSERT solicitud_masiva` con `estado_local =
   'Creada'`, filtros, `rfc_solicitante`, `dedup_key`, `creada_en`; log
   `solicitud_creada` (y `duplicado_confirmado` si aplica).
2. Tx2: `estado_local = 'Enviando'`, `envio_iniciado_en = ahora`; log
   `envio_iniciado`.
3. Llamada SAT fuera de transaccion.
4. Tx3, segun resultado:
   - `CodEstatus=5000` con `IdSolicitud`: `estado_local = 'Enviada'`,
     `id_solicitud_sat`, `cod_estatus_solicitud`, `mensaje_solicitud_sat`,
     `enviada_en`, `siguiente_verificacion_en`; log `solicitud_enviada` con
     `origen_codigo_sat = 'creacion'`. `estado_solicitud_sat` queda nulo.
   - Rechazo documentado o fallo local: `estado_local = 'EnvioFallido'`,
     codigo/mensaje de creacion si existen, `ultimo_error`; log
     `envio_fallido`.
   - Timeout, interrupcion, `5000` sin `IdSolicitud`, `5006` o codigo no
     documentado: `estado_local = 'EnvioIncierto'`, codigo/mensaje si existen;
     log `envio_incierto`.

### Verificar solicitud

Una transaccion:

- `estado_solicitud_sat`, `codigo_estado_solicitud`,
  `mensaje_verificacion_sat`, `numero_cfdi`, `ultima_verificacion_en`,
  `siguiente_verificacion_en`, `verificaciones_sin_cambio`, `ultimo_error`.
- `Terminada`: insertar cada `paquete_solicitud` devuelto (`Disponible`,
  `disponible_en`, `vencimiento_estimado_en`) respetando
  `ux_paquete_solicitud_id_paquete_sat`; los existentes no se duplican.
- `Vencida`: paquetes no eliminados y no `Descargado` pasan a `Vencido` con
  `motivo_vencimiento = 'solicitud_expirada'`, `origen_vencimiento = 'SAT'`,
  `vencido_en`.
- Si se consumio `verificacion_pendiente`: ponerla en `0` y recalcular
  `accion_pendiente_en` (nulo si `descarga_pendiente = 0`).
- Logs `verificacion_realizada` o `verificacion_fallida` con
  `origen_codigo_sat = 'verificacion'`, `paquetes_registrados` y
  `paquete_vencido` cuando aplique.

### Descargar paquete

1. Tx1: `estado_descarga = 'Descargando'`, `descarga_iniciada_en`; log
   `descarga_iniciada`.
2. Descarga, archivo temporal y rename atomico fuera de transaccion (T008).
3. Tx2:
   - Exito: `estado_descarga = 'Descargado'`, `ruta_local`, `descargado_en`,
     `codigo_descarga_sat`, `mensaje_descarga_sat`; log `paquete_descargado`.
   - `5007`: `estado_descarga = 'Vencido'`, `motivo_vencimiento =
     'paquete_expirado'`, `origen_vencimiento = 'SAT'`, `vencido_en`; log
     `paquete_vencido`. `estado_solicitud_sat` no cambia.
   - Otro fallo: `estado_descarga = 'Error'` (o `Disponible` segun causa),
     `ultimo_error`, codigo/mensaje de descarga; log `descarga_fallida`.
   - Si se consumio `descarga_pendiente`: ponerla en `0` y recalcular
     `accion_pendiente_en`.

### Vencimiento estimado

Una transaccion por lote: paquetes `Disponible`, `Descargando` o `Error` con
`vencimiento_estimado_en <= ahora` pasan a `Vencido` con `motivo_vencimiento =
'vencimiento_estimado'`, `origen_vencimiento = 'estimacion_local'`,
`vencido_en`; log `paquete_vencido`. No cambia `estado_solicitud_sat`.

### Acciones pendientes

Con monitoreo pausado, `Verificar ahora` pone `verificacion_pendiente = 1` y
`Reintentar descarga` pone `descarga_pendiente = 1`; si `accion_pendiente_en`
era nulo se asigna `ahora`. Log `accion_pendiente_registrada`. Si al reanudar
ya no aplica, se limpia la bandera y se registra
`accion_pendiente_descartada`.

### Recuperar al arrancar

Una transaccion por regla, con `origen = 'recuperacion'` en los logs:

- `Creada` (sin `envio_iniciado_en`): sin cambios.
- `Enviando`: `estado_local = 'EnvioIncierto'`; log `envio_incierto`.
- Paquete `Descargando` sin archivo final: `estado_descarga = 'Disponible'`,
  `reconciliado_en`; log `descarga_interrumpida`.
- Paquete `Descargando` con archivo final: `estado_descarga = 'Descargado'`,
  `ruta_local`, `descargado_en`, `reconciliado_en`; log
  `paquete_reconciliado`.
- Archivo final sin paquete persistido pero con solicitud identificable: log
  `archivo_huerfano`. Sin solicitud identificable: ver limite T008.

### Eliminar solicitud local

Una transaccion, idempotente: asignar el mismo `eliminado_en` a la
`solicitud_masiva` y a sus `paquete_solicitud` y `log_solicitud` con
`eliminado_en IS NULL`. No borra ZIPs ni filas. Si ya estaba eliminada, no
cambia nada.

### Eliminar perfil

Una transaccion: verificar que no existan solicitudes del perfil con
`eliminado_en IS NULL` en curso (`ix_solicitud_masiva_perfil`); asignar
`perfil_sat.eliminado_en`; `DELETE FROM credencial_sat WHERE perfil_sat_id =
?`. El borrado de secretos lo coordina T005.

## Limites para T003, T005, T007 y T008

- T003:
  - Activa y verifica `PRAGMA foreign_keys=ON` en cada conexion; la migracion
    no puede hacerlo. Sin ese PRAGMA las FK no se aplican.
  - Implementa runner (DM1/DM2), rechazo de version futura, verificacion de
    version SQLite/JSON1 y prueba con `QSQLITE` y el recurso embebido.
  - Calcula `dedup_key v1` exacto, normaliza RFCs y `rfc_receptores_json`, y
    ejecuta `evaluarDuplicado` bajo `BEGIN IMMEDIATE`.
  - Escribe consultas con `eliminado_en IS NULL` literal para usar los indices
    parciales.
- T005:
  - Define el formato de `certificado_ref`, `llave_privada_ref` y
    `contrasena_ref` (referencias opacas de generacion/contenedor/Keychain).
    T001 solo exige texto no vacio.
  - Coordina el `DELETE`/`UPDATE` de `credencial_sat` con la generacion de
    secretos y su reconciliacion. Metadatos como `numero_serie` o vigencia
    requieren una migracion posterior si se deciden.
- T007:
  - Calcula `vencimiento_estimado_en` (base documental: 72 horas desde la
    generacion del paquete, `docs/web-service.md`) y `siguiente_verificacion_en`
    con intervalo y backoff.
  - Garantiza que solo verificacion escribe `estado_solicitud_sat` y aplica el
    orden de transiciones; el esquema solo impide estados incoherentes.
- T008:
  - Define el formato de `ruta_local` (absoluta o relativa a la carpeta de
    paquetes) y su unicidad; T001 no crea indice unico de ruta (DC6).
  - Decide como registrar un archivo huerfano sin solicitud identificable:
    `log_solicitud.solicitud_masiva_id` es `NOT NULL` (DC4), por lo que ese caso
    requiere log de aplicacion u otra decision de T008.

## Decisiones fisicas de este ciclo

| Id | Decision | Motivo |
| --- | --- | --- |
| DC1 | Filtros SAT en columnas explicitas, sin `filtros_sat_json`. `tipo_cfdi` con `CHECK` contra `operacion_sat`. | Una sola fuente para recalcular `dedup_key` y para restricciones; `tipo_cfdi` se conserva por ADR 0013. |
| DC2 | Contraparte en emitidos como `rfc_receptores_json` arreglo no vacio; en recibidos en `rfc_emisor`. | El WSDL de emitidos usa `RfcReceptores/RfcReceptor` (`docs/web-service.md`). |
| DC3 | `tipo_comprobante` con `CHECK` `I,E,T,N,P`; `complemento` libre no vacio. | Valores documentados por SAT; el catalogo de complementos puede crecer. |
| DC4 | `log_solicitud.solicitud_masiva_id NOT NULL`; sin `paquete_solicitud_id`. | Todo log pertenece a una solicitud; `IdPaquete` cabe en el resumen JSON. Huerfano sin solicitud queda para T008. |
| DC5 | `accion_pendiente_en` con `CHECK` de equivalencia. | Esta en arquitectura §11; la equivalencia evita marcas obsoletas. |
| DC6 | Sin `ux_paquete_ruta`. | El formato de ruta corresponde a T008. |
| DC7 | Sin `CHECK` de formato en timestamps internos; si en `fecha_*_sat`. | Las fechas SAT participan en `dedup_key` y deben ser estables. |
| DC8 | Catalogos de `tipo_evento`, `origen` y `origen_codigo_sat`; sin `solicitud_eliminada`. | Un evento de eliminacion quedaria oculto por la propia eliminacion logica. |
| DC9 | `descarga_iniciada_en`, `CHECK` de UUID, RFC 12-13 normalizado, `perfil_sat.nombre NOT NULL`. | Recuperacion de `Descargando` y datos estructuralmente validos. |
| DC10 | FK sin `ON DELETE`; solo `credencial_sat` se borra fisicamente. | Conserva historial; la eliminacion es logica. |

Otras decisiones menores de este documento:

- `fecha_inicial`/`fecha_final` de arquitectura §11 se nombran
  `fecha_inicial_sat`/`fecha_final_sat`, igual que en la serializacion de
  `dedup_key`.
- Se agregan `rfc_emisor`, `rfc_receptor`, `rfc_receptores_json`,
  `tipo_comprobante` y `complemento` como columnas (consecuencia de DC1).
- Unicidades declaradas como indices con nombre estable (`ux_`) en lugar de
  `UNIQUE` en columna, para que las pruebas de `EXPLAIN QUERY PLAN` y los
  errores sean deterministas.
- `dedup_key` solo valida prefijo de version, no el formato v1 completo.

## Fuera del modelo

No existen tablas ni columnas para XML, CFDI individuales, UUID de
comprobante, clientes, usuarios, roles, organizaciones, permisos, secretos ni
bytes de ZIP. Tampoco hay tabla de cola: las intenciones manuales viven en
`solicitud_masiva`.
