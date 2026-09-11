# T003: Persistencia local de solicitudes

## Estado

Pendiente

## Prioridad y tamano

- Prioridad: Alta.
- Tamano: Grande.

## Objetivo

Implementar la persistencia SQLite local y conectar las vistas del shell con repositorios que permitan crear, listar, consultar y eliminar logicamente datos del MVP.

## Contexto

La app debe conservar metadata local aunque el SAT no este disponible y debe mostrarla al reiniciar. Esta tarea convierte el esquema de `T001` en repositorios utilizables por la aplicacion, usando datos simulados y sin depender de llamadas SAT.

## Alcance

### Incluye

- Inicializacion de la base local.
- Aplicacion de migraciones.
- Proveedor de conexiones SQLite por hilo.
- Repositorios de perfiles, solicitudes, paquetes, logs y configuracion.
- Crear, listar y consultar detalle de solicitudes.
- Persistir solicitudes simuladas con estado local/SAT.
- Eliminacion logica de solicitudes y registros relacionados.
- Tests de repositorios contra bases temporales.
- Conexion de la lista y detalle QML con datos persistidos.

### No incluye

- Llamadas SAT reales.
- Autenticacion o firma.
- Worker, ejecutor serial o backoff.
- Descarga de ZIP o escritura de archivos.
- Parsing, extraccion o indexacion de XML.
- Implementacion de Keychain.

## Dependencias

- `T001-modelo-fisico-sqlite.md`.
- `T002-shell-qt-qml.md`.
- `docs/design/operational-rules.md`.
- `docs/design/qt-project-structure.md`.

## Trabajo esperado

1. Crear el adaptador/proveedor de base SQLite en infraestructura.
2. Aplicar la migracion inicial al iniciar la aplicacion.
3. Implementar conexiones independientes por hilo y cierre controlado.
4. Implementar los contratos de repositorio definidos en arquitectura.
5. Implementar consultas normales que oculten registros con `eliminado_en`.
6. Implementar insercion y consulta de solicitudes simuladas.
7. Implementar eliminacion logica de solicitud, paquetes y logs relacionados.
8. Conectar view models de lista y detalle a los repositorios.
9. Agregar tests de restricciones, consultas y transacciones locales.

## Decisiones que debe respetar

- La UI no comparte `QSqlDatabase` con operaciones fuera del hilo grafico.
- Los repositorios no contienen reglas SAT ni llaman servicios externos.
- El repositorio no elimina fisicamente registros del dominio.
- La carpeta de ZIPs no se crea ni se modifica en esta tarea.
- La configuracion de la app se conserva como registro unico.
- Los logs persistidos deben ser sanitizados, aunque en esta tarea solo se usen datos simulados.

## Criterios de aceptacion

- [ ] La aplicacion crea o abre la base local y aplica la migracion inicial.
- [ ] Una segunda apertura no duplica tablas, indices ni registros de configuracion.
- [ ] Se puede crear un perfil SAT simulado y consultarlo por RFC.
- [ ] Se puede crear una solicitud simulada con sus filtros, `dedup_key` y estados separados.
- [ ] Una solicitud creada aparece en la lista despues de reiniciar la app.
- [ ] El detalle muestra filtros, estados, codigos y fechas persistidos.
- [ ] Los paquetes y logs relacionados pueden consultarse por solicitud.
- [ ] Una solicitud eliminada logicamente no aparece en consultas normales.
- [ ] La eliminacion logica tambien oculta sus paquetes y logs relacionados sin borrar fisicamente los registros.
- [ ] Las restricciones de RFC, credencial unica, paquete unico y configuracion unica son verificadas por tests.
- [ ] Las operaciones de repositorio no bloquean el hilo grafico.
- [ ] Los tests usan una base temporal y no modifican datos de usuario.
- [ ] No se introducen llamadas SAT, Keychain, worker ni escritura de ZIP.

## Verificacion

1. Ejecutar la suite de repositorios contra una base SQLite temporal.
2. Verificar migracion sobre base vacia y sobre base ya migrada.
3. Crear un perfil y una solicitud simulada desde la app.
4. Cerrar y abrir la app.
5. Consultar lista y detalle.
6. Eliminar logicamente la solicitud y confirmar que deja de aparecer.
7. Inspeccionar la base para confirmar que los registros eliminados siguen presentes con `eliminado_en`.
8. Ejecutar `ctest --test-dir build`.

## Definicion de terminado

- La app persiste y recupera solicitudes simuladas correctamente.
- Los repositorios cubren las entidades necesarias para el siguiente incremento.
- Las reglas de hilo y eliminacion logica estan probadas.
- La lista y el detalle ya no dependen de datos hardcodeados.
- `T004`, `T005` y `T007` pueden consumir persistencia mediante contratos sin inventar acceso directo a SQLite.

## Resultado

Pendiente.

## Riesgos y notas

- Esta tarea no demuestra que el contrato SAT sea correcto; eso corresponde a `T006`.
- El almacenamiento de credenciales sigue siendo solo referencia hasta `T005`.
- La concurrencia entre worker y acciones manuales se resolvera en `T007`, no dentro de los repositorios.

## Referencias

- `T001-modelo-fisico-sqlite.md`
- `T002-shell-qt-qml.md`
- `docs/architecture.md`
- `docs/design/operational-rules.md`
- `docs/design/qt-project-structure.md`

