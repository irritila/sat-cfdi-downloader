# T003: Persistencia local de solicitudes

## Estado

Pendiente

## Prioridad y tamano

- Prioridad: Alta.
- Tamano: Grande.

## Objetivo

Implementar un corte vertical de persistencia SQLite local que reemplace los datos demo de T002 por repositorios, servicios de aplicacion y vistas QML persistentes para crear, listar, consultar y eliminar logicamente solicitudes locales simuladas.

## Contexto

La app debe conservar metadata local aunque el SAT no este disponible y debe mostrarla despues de reiniciar. T003 consume el esquema fisico y la migracion inicial definidos por T001 y el shell Qt/QML de T002.

El incremento sigue usando datos simulados: no llama al SAT, no guarda credenciales reales, no descarga ZIPs y no ejecuta worker. Su objetivo es dejar lista la persistencia local, los contratos y la verificacion que consumiran T005, T006, T007 y T009.

## Alcance

### Incluye

- Inicializacion de SQLite en `QStandardPaths::AppDataLocation/satcfdi.sqlite3`.
- Soporte de `--data-dir <dir>` para desarrollo y pruebas manuales.
- `organizationName="Adenium"` y `applicationName="SAT CFDI Downloader"` antes de resolver `AppDataLocation`.
- Aplicacion idempotente de `src/infrastructure/persistence/migrations/001_initial_schema.sql`, embebida como recurso `:/migrations/001_initial_schema.sql`.
- Rechazo de bases con version futura de migracion.
- `PersistenceDispatcher` con `QThread` dedicado y `QPromise`, distinto del `OperacionExecutor` de T007.
- Proveedor de conexiones SQLite por hilo, con `foreign_keys=ON`, `busy_timeout`, WAL y cierre controlado.
- Repositorios sincronos para perfiles, solicitudes, paquetes, logs y configuracion.
- `UnitOfWork` con `BEGIN IMMEDIATE`, `commit` y `rollback` tipados.
- Servicios asincronos de aplicacion para solicitudes y perfiles.
- Creacion visible de un perfil SAT simulado cuando no existan perfiles activos.
- Creacion local de solicitud simulada en `estado_local=Creada`, sin estado SAT ni `id_solicitud_sat`.
- `evaluarDuplicado` con resultados `Libre`, `Bloqueado` y `RequiereConfirmacion`.
- Confirmacion explicita de duplicado en UI cuando aplique `RequiereConfirmacion`.
- `dedup_key v1:<sha256-hex>` con serializacion canonica y vectores golden.
- Eliminacion logica visible desde la UI, idempotente y transaccional para solicitud, paquetes y logs.
- Estado de detalle "solicitud no encontrada" cuando `obtener(id)` no encuentre una solicitud visible.
- Lectura de paquetes y logs persistidos para detalle; paquetes representativos solo mediante fixtures de prueba.
- `LogSanitizer` reusable antes de persistir logs, con entrada tipada y saneado defensivo de texto libre.
- Tests de dominio, aplicacion, infraestructura, integracion y presentacion contra bases temporales.

### No incluye

- Llamadas SAT reales.
- Autenticacion, firma, certificados o Keychain.
- Worker, `OperacionExecutor`, backoff o recuperacion de operaciones SAT.
- Descarga de ZIP o escritura de archivos de paquetes.
- Parsing, extraccion o indexacion de XML.
- Implementar UI productiva de administracion de perfiles SAT; solo la accion explicita de perfil simulado para habilitar T003.
- Insercion/upsert productivo de paquetes SAT; eso queda para T007/T009.

## Dependencias

- `T001-modelo-fisico-sqlite.md`: debe entregar `docs/design/sqlite-physical-model.md` y `src/infrastructure/persistence/migrations/001_initial_schema.sql`.
- `T002-shell-qt-qml.md`: debe entregar shell ejecutable, composition root, view models y navegacion QML.
- `docs/design/operational-rules.md`.
- `docs/design/qt-project-structure.md`.
- `docs/adrs/0015-solicitudes-paquetes-y-eliminacion-local.md`.
- `docs/adrs/0016-sqlite-runtime-migrations-and-threading.md`.

## Contratos de aplicacion

### `SolicitudesService`

Expone operaciones asincronas mediante `QFuture`:

- `listar() -> Resultado<QList<SolicitudResumen>, ErrorPersistencia>`.
- `obtener(SolicitudId) -> Resultado<SolicitudDetalle, ErrorObtener>`.
- `evaluarDuplicado(NuevaSolicitudRequest) -> Resultado<EvaluacionDuplicado, ErrorPersistencia>`.
- `crearLocal(NuevaSolicitudRequest, ConfirmacionDuplicado) -> Resultado<SolicitudId, ErrorCrear>`.
- `eliminar(SolicitudId) -> Resultado<ResultadoEliminacion, ErrorPersistencia>`.

Senales:

- `listaCambiada()`.
- `solicitudActualizada(SolicitudId)`.
- `solicitudEliminada(SolicitudId)`.

Las senales se emiten solo despues del `commit`. Un rollback no emite senales. Una eliminacion idempotente con `cambio=false` no emite `solicitudEliminada`.

### `PerfilesSatService`

- `listarActivos() -> Resultado<QList<PerfilResumen>, ErrorPersistencia>`.
- `crearPerfilSimulado(NuevoPerfilSimuladoRequest) -> Resultado<PerfilId, ErrorCrearPerfil>`.

Cuando no haya perfiles activos, la UI muestra una accion visible "Crear perfil simulado". La accion invoca este servicio; no se siembra un perfil desde la migracion ni de forma silenciosa al arrancar.

## Repositorios minimos

Los repositorios son puertos sincronos. No exponen `QSqlDatabase`, `QSqlQuery` ni `QSqlError`.

- `PerfilSatRepository`: insertar perfil, obtener por id, obtener por RFC vigente y listar activos visibles.
- `SolicitudMasivaRepository`: listar visibles, obtener visible por id, clasificar por `dedup_key`, insertar solicitud `Creada` y marcar eliminada.
- `PaqueteSolicitudRepository`: listar visibles por solicitud y marcar eliminados por solicitud. No expone insercion productiva en T003.
- `LogSolicitudRepository`: listar visibles por solicitud, agregar `LogEntradaSaneada` y marcar eliminados por solicitud.
- `ConfiguracionAppRepository`: obtener la fila unica de configuracion.
- `UnitOfWork`: `begin`, `commit` y `rollback` con resultado tipado. `begin` usa `BEGIN IMMEDIATE`.

`Resultado<T,E>` debe ser un tipo propio compatible con C++20; no depende de `std::expected`.

## DTOs y errores minimos

Entradas principales:

- `NuevaSolicitudRequest`: perfil, operacion SAT, fecha inicial/final, RFC contraparte opcional, receptores opcionales, tipo de comprobante opcional y complemento opcional.
- `NuevoPerfilSimuladoRequest`: RFC, razon social y `activo=true`.

Salidas principales:

- `SolicitudResumen`: mantiene los roles definidos por T002.
- `SolicitudDetalle`: cabecera, paquetes y logs.
- `PerfilResumen`.
- `EvaluacionDuplicado`: estado, `dedupKey`, solicitud de referencia opcional y motivo.
- `ResultadoEliminacion`: `cambio` y `eliminadoEn` opcional.

Errores minimos:

- `ErrorValidacion`.
- `ErrorPerfilInvalido`.
- `ErrorDedupBloqueado`.
- `RequiereConfirmacion`.
- `NoEncontrado`.
- `ErrorIntegridad`.
- `ErrorPersistencia`.
- `ErrorMigracion`.

## Reglas de duplicados

`evaluarDuplicado` devuelve:

- `Libre`: no existe una solicitud equivalente no eliminada ni una historia que requiera advertencia.
- `Bloqueado`: existe una solicitud equivalente que no debe permitir nueva creacion.
- `RequiereConfirmacion`: se permite crear una solicitud equivalente solo con advertencia y confirmacion explicita.

Matriz T003:

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

Si hay varias coincidencias, la precedencia es `Bloqueado` > `RequiereConfirmacion` > `Libre`.

## `dedup_key v1`

Formato final:

```text
v1:<sha256-hex-lowercase>
```

El hash se calcula con SHA-256 sobre una serializacion canonica UTF-8 sin BOM, con pares `clave=valor` separados por LF y sin LF final. Orden fijo:

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

Reglas:

- Operacion SAT usa los literales `SolicitaDescargaEmitidos` o `SolicitaDescargaRecibidos`.
- RFCs se normalizan con trim, mayusculas y sin espacios internos.
- Las fechas se serializan como `YYYY-MM-DDThh:mm:ss` en hora Centro de Mexico, sin offset. Si la UI entrega fecha, dominio expande inicial a `T00:00:00` y final a `T23:59:59`.
- `rfc_receptores` se normaliza, deduplica, ordena por bytes UTF-8 y se une con coma sin espacios.
- Opcionales ausentes, string vacio o listas vacias omiten la linea completa; nunca se escribe `clave=`.
- La validacion de dominio rechaza valores con LF, `=` o `,`.
- No se incluye `perfil_sat_id`, UUID local, usuario ni timestamps.
- Los tests deben fijar vectores golden con el string canonico pre-hash y el SHA-256 esperado calculado fuera de la implementacion.

Cambiar campos, orden, normalizacion, separadores, hash, prefijo o filtros SAT que afecten equivalencia exige una nueva version (`v2`) y una migracion de datos. T003 solo implementa `v1` y no define una politica definitiva de comparacion entre versiones futuras.

## Transacciones

### Crear solicitud local

1. Validar dominio y perfil activo fuera de transaccion.
2. Calcular `dedup_key v1`.
3. Abrir transaccion con `BEGIN IMMEDIATE`.
4. Clasificar duplicado.
5. Si es `Bloqueado`, rollback y `ErrorDedupBloqueado`.
6. Si es `RequiereConfirmacion` y no hay confirmacion, rollback y `RequiereConfirmacion`.
7. Insertar solicitud en `estado_local=Creada`, con estado SAT, `id_solicitud_sat`, codigos y mensajes en `NULL`.
8. Agregar log inicial mediante `LogSanitizer`.
9. Commit y senales.

Una violacion del indice unico parcial se traduce a `ErrorDedupBloqueado`, no a `ErrorPersistencia`.

### Crear perfil simulado

1. Validar RFC y razon social.
2. Abrir transaccion.
3. Insertar perfil sin credencial.
4. Commit y `perfilesCambiaron()`.

Un RFC vigente duplicado devuelve `ErrorIntegridad`.

### Eliminar solicitud

- Si el id no existe o ya esta eliminado, devuelve exito idempotente con `cambio=false`.
- Si existe visible, marca `eliminado_en` en solicitud, paquetes y logs con el mismo timestamp en una sola transaccion.
- `obtener(id)` posterior devuelve `NoEncontrado`.

## LogSanitizer

`LogSanitizer` recibe `LogEntradaCruda` tipada y devuelve `LogEntradaSaneada`. El repositorio de logs solo acepta `LogEntradaSaneada`.

La entrada separa metadata, identificadores SAT, codigos SAT, operacion/filtros y texto libre (`mensaje`, `detalle`). No existen campos destino para tokens, passwords, llave privada, certificados completos, firmas, XML/SOAP crudo, contenido `Paquete`, ZIP o base64 de paquetes.

Reglas defensivas sobre texto libre:

- Redactar bloques XML sensibles: `SignatureValue`, `ds:Signature`, `X509Certificate` y `Paquete`.
- Redactar bloques PEM.
- Redactar pares clave/valor sensibles: token, access token, authorization, password, passwd, contrasena, contraseña, clave, pin, secret, api key.
- Redactar cabeceras Authorization completas.
- Redactar base64 huerfano de 64 caracteres o mas cuando no sea hexadecimal puro.
- Preservar RFCs, UUID/IdSolicitud, IdPaquete, codigos SAT, fechas, `dedup_key v1` y SHA-256 hex standalone.
- Mantener idempotencia: aplicar el sanitizer dos veces no cambia la salida.
- Limitar entrada antes de regex a 1 MiB.
- Limitar `mensaje` a 500 caracteres y `detalle` a 8192 caracteres sin partir UTF-8.
- Usar marcadores cerrados `[REDACTED:token]`, `[REDACTED:password]`, `[REDACTED:clave]`, `[REDACTED:secret]`, `[REDACTED:firma]`, `[REDACTED:certificado]`, `[REDACTED:pem]`, `[REDACTED:paquete]`, `[REDACTED:base64:<len>]` y `[TRUNCATED:<charsRecortados>]`.

## Persistencia, hilos y arranque

- La migracion corre antes de cargar el QML principal.
- Si la migracion falla, la app no abre UI funcional contra una base incompleta y muestra un error fatal minimo.
- La conexion de bootstrap se cierra en su hilo antes de crear `PersistenceDispatcher`.
- `PersistenceDispatcher` usa `QThread` dedicado y `QPromise`; no usa `OperacionExecutor`.
- Cada conexion se crea, usa, cierra y retira en su hilo propietario.
- Timestamps persistidos: UTC ISO-8601 con milisegundos y sufijo `Z` (`yyyy-MM-ddTHH:mm:ss.zzzZ`).
- El runner de migraciones recibe una lista inyectable para tests.
- `QSQLITE` ejecuta una sentencia por `exec`; el runner divide el SQL conforme a la convencion de migraciones.
- Cada conexion verifica `foreign_keys=ON` y `busy_timeout`; WAL se habilita al inicializar.
- El cierre controlado ejecuta `db.close()` y `QSqlDatabase::removeDatabase(name)` en el hilo propietario, sin `QSqlQuery` vivos.

## Cambios de UI

- La lista soporta estados `cargando`, `vacia`, `error` y `con datos`.
- Nueva solicitud conserva `ocupado` y `errorMessage`.
- Si no hay perfiles activos, muestra accion explicita "Crear perfil simulado".
- Si `crearLocal` devuelve `RequiereConfirmacion`, muestra advertencia con motivo y permite confirmar o cancelar.
- El detalle soporta `cargando`, `error`, `con datos` y "solicitud no encontrada".
- La UI expone accion visible para eliminar solicitud, con confirmacion.
- QML no conoce `eliminado_en`, SQL, rutas, Keychain, SAT, ZIPs ni deduplicacion interna.

## Criterios de aceptacion

- [ ] La app fija `organizationName="Adenium"` y `applicationName="SAT CFDI Downloader"` antes de resolver la ruta de datos.
- [ ] La app crea o abre `satcfdi.sqlite3` en `QStandardPaths::AppDataLocation` o en `--data-dir` cuando se use esa opcion.
- [ ] La migracion inicial se carga desde `:/migrations/001_initial_schema.sql`, se aplica una sola vez y una segunda apertura no duplica tablas, indices, `schema_migrations` ni `configuracion_app`.
- [ ] Una base con version futura se rechaza sin modificar el archivo.
- [ ] Cada conexion SQLite verifica `foreign_keys=ON`, `busy_timeout` y WAL cuando aplique.
- [ ] Se puede crear un perfil SAT simulado de forma visible cuando no hay perfiles activos, y consultarlo por RFC.
- [ ] Se puede crear una solicitud local simulada con `estado_local=Creada`, estado SAT, `id_solicitud_sat`, codigos y mensajes en `NULL`, `rfc_solicitante` del perfil y `dedup_key v1`.
- [ ] La clasificacion de duplicados cumple la matriz de esta tarea y la UI permite confirmar solo los casos `RequiereConfirmacion`.
- [ ] Una solicitud creada aparece en la lista y detalle despues de reiniciar la app.
- [ ] El detalle muestra filtros, estados, codigos y fechas persistidos; estados/codigos que T003 no produce se verifican con fixtures.
- [ ] Los paquetes y logs relacionados se consultan por solicitud; paquetes representativos provienen de fixtures de prueba.
- [ ] La accion visible de eliminar marca logicamente solicitud, paquetes y logs en una sola transaccion, sin borrar fisicamente registros.
- [ ] Una solicitud eliminada no aparece en consultas normales; `obtener(id)` devuelve `NoEncontrado` y el detalle muestra "solicitud no encontrada".
- [ ] Eliminar un id inexistente o ya eliminado devuelve exito idempotente con `cambio=false`.
- [ ] Las restricciones de RFC, credencial unica, paquete unico, configuracion unica y FK se verifican con SQLite real; las violaciones se traducen a errores tipados sin exponer `QSqlError`.
- [ ] Ninguna operacion SQL se ejecuta en el hilo grafico, y la UI procesa eventos mientras un repositorio fake esta bloqueado.
- [ ] `LogSanitizer` redacta con el catalogo cerrado de marcadores, preserva identificadores no sensibles, es idempotente y respeta limites de longitud.
- [ ] Los tests de centinela confirman que secretos no aparecen en `.sqlite3`, `-wal` ni `-shm` antes del cierre; tras cerrar, la ausencia de `-wal`/`-shm` es valida y el archivo principal queda limpio.
- [ ] No se agregan llamadas SAT, `Qt6::Network`, Keychain/Security framework, worker, `OperacionExecutor`, creacion de carpeta ZIP ni escritura de ZIP.

## Verificacion

Automatica:

1. `satcfdi_tests`: dominio y aplicacion, sin `Qt6::Sql`.
   - Vectores golden de `dedup_key v1` con hash calculado fuera de la implementacion.
   - Invariancias, sensibilidad y rechazos del canonico.
   - `LogSanitizer` con patrones, preservacion, base64, truncado, idempotencia y reporte.
   - Servicios con repositorios fake: duplicados, perfil invalido, senales solo despues de commit y no bloqueo del hilo grafico.
2. `satcfdi_infrastructure_tests`: SQLite real con `QTemporaryDir`.
   - Driver `QSQLITE`, JSON1 e indices parciales.
   - Migracion vacia, repetida, rota con rollback y version futura.
   - Recursos de migracion, PRAGMAs, `BEGIN IMMEDIATE`, cierre sin warnings, `integrity_check` y `foreign_key_check`.
   - Restricciones, clasificacion de duplicados con fixtures, creacion, atomicidad, eliminacion e idempotencia.
   - Centinelas en `.sqlite3`, `-wal` y `-shm`.
3. `satcfdi_integration_tests`: composition root sobre `QTemporaryDir`.
   - Crear perfil y solicitud, destruir grafo, reabrir y verificar datos identicos.
   - Eliminar, reiniciar y obtener `NoEncontrado`.
   - Confirmar que no se instancia `DemoSolicitudesService` ni se crea carpeta de ZIPs.
4. `satcfdi_presentation_tests`: servicio fake asincrono.
   - Lista, nueva solicitud, confirmacion de duplicado, detalle, eliminar y selector de perfiles.

Comandos esperados:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Manual:

1. Ejecutar la app con `--data-dir <dir-temporal>`.
2. Crear perfil simulado.
3. Crear solicitud local.
4. Cerrar y abrir la app.
5. Ver lista y detalle.
6. Eliminar solicitud.
7. Cerrar y abrir de nuevo para confirmar que no aparece y que el detalle por id muestra "solicitud no encontrada".

## Definicion de terminado

- La app persiste y recupera solicitudes locales simuladas correctamente.
- Lista, detalle, nueva solicitud, confirmacion de duplicado, perfil simulado y eliminacion usan servicios persistidos.
- Los repositorios cubren solo las operaciones necesarias para T003 y no exponen SQL fuera de infraestructura.
- Las reglas de hilos, transacciones, migraciones, deduplicacion y eliminacion logica estan probadas.
- `LogSanitizer` queda reusable para T005, T006, T007 y T009.
- La lista y el detalle ya no dependen de datos hardcodeados.
- `T004`, `T005`, `T006`, `T007`, `T008` y `T009` pueden consumir persistencia mediante contratos sin inventar acceso directo a SQLite.

## Resultado

Pendiente.

## Riesgos y notas

- T003 no demuestra que el contrato SAT sea correcto; eso corresponde a T006.
- T003 depende de artefactos fisicos de T001 y T002; no debe considerarse implementable solo con documentos.
- La migracion futura de `dedup_key v1` a otra version queda fuera de T003 y debe disenar comparacion entre versiones sin permitir duplicados reales.
- El saneado de base64 preserva cadenas hexadecimales de 64 caracteres para no romper hashes; un secreto codificado como hex podria no redactarse.
- Packaging debe incluir `libqsqlite.dylib` en `PlugIns/sqldrivers`; la distribucion formal queda fuera de T003.

## Referencias

- `T001-modelo-fisico-sqlite.md`
- `T002-shell-qt-qml.md`
- `docs/architecture.md`
- `docs/design/operational-rules.md`
- `docs/design/qt-project-structure.md`
- `docs/adrs/0015-solicitudes-paquetes-y-eliminacion-local.md`
- `docs/adrs/0016-sqlite-runtime-migrations-and-threading.md`
