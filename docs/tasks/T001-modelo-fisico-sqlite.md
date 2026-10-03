# T001: Modelo fisico SQLite

## Estado

Completada

## Prioridad y tamano

- Prioridad: Alta.
- Tamano: Mediana.

## Objetivo

Definir y dejar reproducible el esquema fisico SQLite que soportara la persistencia local del MVP, incorporando las decisiones D001-D008 del refinamiento de T001.

## Contexto

La app necesita conservar solicitudes, paquetes, logs, perfiles y configuracion despues de reinicios. El esquema debe reflejar las reglas ya aprobadas: estado SAT separado del ciclo local, eliminacion logica, deduplicacion, acciones pendientes y reconciliacion de archivos ZIP.

El refinamiento de T001 cerro las reglas de vencimiento, deduplicacion, perfiles, credenciales y eliminacion local. Esas decisiones viven en `docs/meetings/T001-refinamiento/bitacora.md` y quedan consolidadas en ADR 0015 y `docs/design/operational-rules.md`.

## Alcance

### Incluye

- Tablas `perfil_sat`, `credencial_sat`, `solicitud_masiva`, `paquete_solicitud`, `log_solicitud` y `configuracion_app`.
- Claves primarias, claves foraneas, nulabilidad, tipos y valores validos.
- Separacion entre `estado_local` y `estado_solicitud_sat`.
- Codigos y mensajes separados para creacion, verificacion y descarga.
- Eliminacion logica mediante `eliminado_en`.
- `rfc_solicitante`, `dedup_key`, banderas de acciones pendientes y datos de reconciliacion de archivos.
- Indices para worker, detalle, paquetes y duplicados.
- Migracion inicial y estrategia de versionado.
- Transacciones requeridas por crear, verificar, descargar, recuperar y eliminar.

### No incluye

- Implementacion de repositorios o servicios de aplicacion.
- Pantallas QML.
- Llamadas al SAT.
- Parsing, extraccion o indexacion de XML.
- Backend remoto, usuarios, clientes, roles o permisos.

## Dependencias

- `docs/design/operational-rules.md`.
- `docs/architecture.md`.
- ADR 0004, ADR 0014 y ADR 0015.

## Trabajo esperado

1. Crear `docs/design/sqlite-physical-model.md` con el esquema tabular completo.
2. Resolver y documentar la representacion de UUID, fechas, booleanos, JSON y valores enumerados.
3. Definir restricciones de unicidad para perfiles, credenciales, solicitudes SAT y paquetes.
4. Definir el indice unico parcial para `dedup_key` y los estados que bloquean duplicados.
5. Definir la tabla o mecanismo de versionado de migraciones.
6. Crear la migracion inicial SQLite en la ubicacion establecida por la estructura Qt.
7. Documentar las transacciones y la recuperacion al arrancar.
8. Documentar que queda para T003, T005, T007 y T008 sin implementarlo en esta tarea.

## Decisiones cerradas para esta tarea

- Identificadores: UUID como `TEXT` en minusculas y sin llaves.
- Timestamps internos: `TEXT` ISO-8601 UTC con sufijo `Z`.
- Fechas de filtro SAT: `TEXT` en hora Centro de Mexico sin offset, para que la `dedup_key` sea estable.
- Booleanos: `INTEGER NOT NULL CHECK (valor IN (0,1))`.
- JSON: `TEXT` con `CHECK (json_valid(campo))` cuando no sea nulo.
- Enumerados: `TEXT` con `CHECK` para catalogos propios. Los codigos SAT quedan como `TEXT` libre.
- `eliminado_en` es el nombre uniforme de eliminacion logica en las entidades que la soportan.
- `estado_local` solo contiene `Creada`, `Enviando`, `Enviada`, `EnvioFallido` o `EnvioIncierto`.
- `estado_solicitud_sat` queda nulo hasta verificacion SAT y solo contiene `Aceptada`, `EnProceso`, `Terminada`, `Error`, `Rechazada` o `Vencida`.
- `Rechazada` y `Vencida` solo proceden de verificacion SAT (`EstadoSolicitud=5` y `6`).
- Vencimiento por paquete (`5007`) o por estimacion local no cambia `estado_solicitud_sat`; cambia paquetes no descargados a `Vencido`.
- La creacion sin `IdSolicitud` nunca inventa estado SAT: rechazo documentado o fallo local va a `EnvioFallido`; incertidumbre, `5000` sin ID, `5006` o codigo no documentado va a `EnvioIncierto`.
- `perfil_sat.rfc` se guarda normalizado directamente; no se crea columna duplicada `rfc_normalizado`.
- `activo=false` no libera RFC, no detiene worker y no invalida credenciales; solo impide nuevas solicitudes con ese perfil.
- Perfil a credencial es relacion 1 a 0..1; la credencial vigente se reemplaza sin historial SQLite.
- El MVP permite eliminar perfiles solo si no tienen solicitudes no eliminadas en curso; la eliminacion conserva historial y borra la credencial vigente y secretos desde aplicacion/T005.
- `dedup_key` se basa en RFC y criterios visibles para SAT, no en `perfil_sat_id`.
- Se permite nueva solicitud equivalente manual, con advertencia y confirmacion, en los casos aprobados: paquetes vencidos con todos los demas descargados/vencidos; `EnvioIncierto`; `Terminada` sin paquetes no eliminados.

## Requisitos fisicos obligatorios

- `perfil_sat`:
  - `rfc TEXT NOT NULL` persistido normalizado.
  - `activo INTEGER NOT NULL CHECK (activo IN (0,1))`.
  - `eliminado_en TEXT NULL`.
  - Indice unico parcial `UNIQUE (rfc) WHERE eliminado_en IS NULL`.
- `credencial_sat`:
  - `perfil_sat_id TEXT NOT NULL UNIQUE` con FK a `perfil_sat(id)` sin cascada fisica.
  - Referencias no secretas obligatorias a los materiales vigentes del `SecretStore`.
  - Sin `eliminado_en`; D006 permite borrar fisicamente esta fila al eliminar la credencial vigente.
- `solicitud_masiva`:
  - `perfil_sat_id` como referencia historica local.
  - `rfc_solicitante TEXT NOT NULL` para deduplicacion y rutas independientes de perfiles recreados.
  - `dedup_key TEXT NOT NULL` calculada sin `perfil_sat_id`.
  - `id_solicitud_sat` unico cuando no sea nulo.
  - `estado_local`, `estado_solicitud_sat`, codigos y mensajes separados para creacion y verificacion.
  - `eliminado_en TEXT NULL`.
  - Indice unico parcial para solicitudes inequívocamente bloqueantes por `dedup_key`.
  - Validacion documentada, dentro de la misma transaccion, para las excepciones D002, D003 y D008.
- `paquete_solicitud`:
  - `UNIQUE (solicitud_masiva_id, id_paquete_sat)`.
  - `estado_descarga` sin estado de eliminacion.
  - `motivo_vencimiento`, `origen_vencimiento` y `vencido_en` cuando aplique.
  - `vencimiento_estimado_en` conserva la fecha estimada previa; `vencido_en` registra cuando el paquete fue marcado como `Vencido`.
  - `motivo_vencimiento` debe cubrir `solicitud_expirada`, `paquete_expirado` y `vencimiento_estimado`.
  - `Descargado` implica `ruta_local` y `descargado_en`.
  - `eliminado_en TEXT NULL`.
- `log_solicitud`:
  - Codigos SAT con `origen_codigo_sat` para distinguir creacion, verificacion y descarga.
  - Logs sanitizados, sin secretos ni payloads crudos.
  - `eliminado_en TEXT NULL`.
- `configuracion_app`:
  - Registro unico local, por ejemplo `id INTEGER PRIMARY KEY CHECK (id=1)` con semilla idempotente.
- Migraciones:
  - Tabla `schema_migrations(version PRIMARY KEY, aplicada_en)`.
  - Cada migracion corre en su propia transaccion.
  - La primera migracion debe crear tablas, indices, constraints y fila inicial de configuracion.

## Invariantes y validaciones

- `id_solicitud_sat IS NOT NULL` solo cuando `estado_local='Enviada'`.
- `estado_solicitud_sat IS NOT NULL` implica que existe `id_solicitud_sat`.
- `estado_local IN ('Creada','Enviando')` implica ausencia de codigo de creacion SAT.
- `estado_local='Enviada'` implica codigo de creacion SAT y `id_solicitud_sat`.
- Los codigos SAT no deben cerrarse mediante CHECK de catalogo.
- `Vencido` en paquete implica motivo y origen no nulos.
- `Descargado` en paquete implica ruta local y timestamp de descarga.
- No usar triggers para transiciones de negocio; el orden de transiciones queda en aplicacion/T007 y pruebas.
- `PRAGMA foreign_keys=ON` no se resuelve con la migracion; T003 debe activarlo por conexion.

## Criterios de aceptacion

- [x] El documento fisico describe las seis tablas, todas sus columnas, tipos, nulabilidad y restricciones.
- [x] `solicitud_masiva` separa `estado_local` de `estado_solicitud_sat`.
- [x] Los codigos y mensajes de creacion, verificacion y descarga no se mezclan.
- [x] `paquete_solicitud` conserva ruta local, estado de descarga, motivo/origen de vencimiento y datos de reconciliacion.
- [x] Las cuatro entidades con eliminacion logica tienen `eliminado_en` y reglas de consulta documentadas.
- [x] El esquema permite como maximo una credencial vigente por perfil y permite perfil sin credencial.
- [x] El esquema impide perfiles no eliminados duplicados por RFC, incluso si alguno tiene `activo=false`.
- [x] La regla de solicitudes equivalentes por `dedup_key` queda expresada mediante indice o validacion documentada.
- [x] `dedup_key` no incluye `perfil_sat_id` y `solicitud_masiva` persiste `rfc_solicitante`.
- [x] El indice parcial no bloquea D002, D003 ni D008, y la validacion transaccional documenta sus advertencias y confirmaciones.
- [x] Una solicitud `Terminada` con todos sus paquetes no eliminados en `Descargado` sigue bloqueando una solicitud equivalente.
- [x] `estado_local` no contiene `EliminadaLocalmente` y `estado_descarga` no contiene `EliminadoLocalmente`.
- [x] `estado_solicitud_sat` no se escribe desde la respuesta de creacion.
- [x] `Rechazada` solo representa `EstadoSolicitud=5`; `Vencida` solo representa `EstadoSolicitud=6`.
- [x] Vencimiento de paquete por `5007` o estimacion local no cambia `estado_solicitud_sat`.
- [x] `configuracion_app` queda restringida a un solo registro.
- [x] Los indices cubren monitoreo, paquetes pendientes y logs del detalle.
- [x] La migracion se aplica correctamente sobre una base vacia.
- [x] La migracion no modifica una base ya actualizada cuando se ejecuta nuevamente.
- [x] Si la migracion falla a mitad, se revierte y no avanza `schema_migrations`.
- [x] `PRAGMA integrity_check` y `PRAGMA foreign_key_check` no reportan errores.
- [x] Cada CHECK y UNIQUE relevante tiene al menos un caso negativo documentado o verificado.
- [x] Las transacciones de crear, verificar, descargar, recuperar y eliminar estan descritas y son compatibles con el esquema.
- [x] No existe ninguna columna para XML, CFDI individual, clientes, usuarios o funcionalidades fuera del MVP.

## Verificacion

- Aplicar la migracion sobre una base SQLite vacia.
- Ejecutar una segunda vez el mecanismo de migraciones y comprobar que no duplica cambios.
- Inspeccionar tablas, indices y restricciones con `.schema` y consultas SQLite.
- Crear datos representativos para validar unicidad, eliminacion logica y relaciones.
- Validar INSERTs negativos para CHECKs y UNIQUEs relevantes.
- Usar `EXPLAIN QUERY PLAN` para confirmar indices de worker, paquetes, logs y deduplicacion.
- Comparar el resultado contra `docs/design/operational-rules.md` y `docs/architecture.md`.

## Definicion de terminado

- El esquema fisico y la migracion inicial estan en el repositorio.
- Las decisiones fisicas quedan documentadas.
- Los criterios de aceptacion estan verificados.
- `T003` puede implementar repositorios sin inventar tablas o columnas.
- `T005`, `T007` y `T008` tienen limites documentados sin que T001 implemente secretos, worker ni almacenamiento ZIP.

## Resultado

Completada el 2026-10-03.

- Modelo fisico: `docs/design/sqlite-physical-model.md`.
- Migracion inicial: `src/infrastructure/persistence/migrations/001_initial_schema.sql`.
- Verificacion reproducible sin Qt: `python3 tests/infrastructure/sql/verify_t001.py` (14 tests OK con SQLite 3.51.0).
- Contrato de migraciones fijado: el runner de T003 es propietario de `schema_migrations`; cada sentencia del SQL termina con una linea que solo contiene `;`; sintaxis limitada a SQLite >= 3.9.0 con JSON1.
- Todos los criterios de aceptacion tienen evidencia; el bloqueo transaccional de `Terminada` y las excepciones D002/D003/D008 quedan documentados para `evaluarDuplicado` en T003.
- Pendiente para T003: verificar la SQLite efectiva de `QSQLITE` (`sqlite_version()` y `json_valid`).

## Riesgos y notas

- El esquema no debe optimizarse para 10,000 filas de CFDI individuales: el MVP no almacena CFDI, solo solicitudes, paquetes y logs.
- Los ZIP permanecen fuera de SQLite; la base conserva metadata y rutas.
- El modelo debe poder evolucionar mediante migraciones sin requerir reconstruir la base manualmente.

## Referencias

- `docs/design/operational-rules.md`
- `docs/architecture.md`
- ADR 0004
- ADR 0014
- ADR 0015
