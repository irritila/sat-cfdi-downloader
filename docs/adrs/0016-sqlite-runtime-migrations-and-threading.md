# ADR 0016: Runtime SQLite, migraciones y conexiones por hilo

## Estado

Accepted

## Contexto

El MVP persiste perfiles, solicitudes, paquetes, logs y configuracion en SQLite local. ADR 0004 separa la base local de secretos y paquetes ZIP, pero no fija la ruta concreta de la base, el mecanismo de migracion, la configuracion runtime de SQLite ni la propiedad de conexiones en Qt.

T003 introduce repositorios SQLite reales y conecta la UI de T002 con persistencia local. Esa tarea necesita decisiones estables para evitar que QML, aplicacion e infraestructura compartan conexiones incorrectamente o que la app abra una base parcialmente migrada.

## Decision

La base productiva se guarda en:

```text
QStandardPaths::AppDataLocation/satcfdi.sqlite3
```

La app debe fijar estos valores antes de resolver `AppDataLocation`:

```text
organizationName = "Adenium"
applicationName = "SAT CFDI Downloader"
```

El ejecutable puede aceptar `--data-dir <dir>` para desarrollo, pruebas manuales y escenarios controlados. Los tests usan `QTemporaryDir` y no tocan datos de usuario.

Las migraciones viven como archivos fuente bajo:

```text
src/infrastructure/persistence/migrations/
```

La migracion inicial se embebe como recurso Qt:

```text
:/migrations/001_initial_schema.sql
```

El migrador aplica versiones de forma idempotente con una tabla `schema_migrations`. Cada migracion se ejecuta en su propia transaccion. Una base con version mayor a la conocida se rechaza sin modificar el archivo.

Cada conexion SQLite debe pertenecer a un solo hilo. La conexion se crea, usa, cierra y retira en su hilo propietario. `QSqlDatabase` y `QSqlQuery` no cruzan hilos ni se exponen fuera de infraestructura.

Cada conexion activa y verifica:

- `PRAGMA foreign_keys=ON`.
- `busy_timeout`.

La base usa WAL para permitir lecturas concurrentes con escrituras futuras. Las transacciones de escritura que leen antes de escribir usan `BEGIN IMMEDIATE` mediante `UnitOfWork`, no `QSqlDatabase::transaction()`.

T003 introduce `PersistenceDispatcher` como mecanismo tecnico para ejecutar persistencia local fuera del hilo grafico. Usa un `QThread` dedicado y devuelve resultados con `QPromise`/`QFuture` o senales encoladas. `PersistenceDispatcher` no es el `OperacionExecutor` de T007. T007 introducira el ejecutor serial para operaciones SAT, worker, recuperacion y acciones manuales criticas.

Los timestamps persistidos por la app usan UTC ISO-8601 con milisegundos y sufijo `Z`:

```text
yyyy-MM-ddTHH:mm:ss.zzzZ
```

## Consecuencias

- La base local queda separada de la carpeta de paquetes ZIP y del almacen de secretos.
- Cambiar `organizationName` o `applicationName` cambia la ruta de la base; debe tratarse como migracion de datos o decision explicita.
- Respaldos en caliente deben considerar `satcfdi.sqlite3`, `satcfdi.sqlite3-wal` y `satcfdi.sqlite3-shm`; con la app cerrada, el checkpoint puede dejar solo el archivo principal.
- Los tests de infraestructura pueden inyectar ruta de base y lista de migraciones para probar rollback, version futura y recursos embebidos.
- `satcfdi_infrastructure` es el unico target que enlaza `Qt6::Sql`; ninguna capa superior incluye tipos `QSql*`.
- El bundle debe incluir el plugin `QSQLITE` bajo `PlugIns/sqldrivers`. La distribucion final debe revisar que `macdeployqt` no arrastre drivers SQL ajenos o dependencias innecesarias.
- Si alguna vez se compila Qt de forma estatica, el build debe importar explicitamente el plugin `QSQLiteDriverPlugin`.

## Verificacion esperada

- Abrir base vacia y aplicar migracion inicial.
- Abrir la misma base una segunda vez sin duplicar objetos ni configuracion.
- Rechazar una base con version futura sin modificarla.
- Probar rollback ante migracion rota inyectada.
- Verificar `foreign_keys`, `busy_timeout`, WAL y `BEGIN IMMEDIATE` con SQLite real.
- Cerrar conexiones sin warnings de `removeDatabase` ni queries vivas.
- Ejecutar pruebas con `QTemporaryDir` y `QStandardPaths::setTestModeEnabled(true)` como red de seguridad.
