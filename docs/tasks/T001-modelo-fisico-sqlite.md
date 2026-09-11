# T001: Modelo fisico SQLite

## Estado

Pendiente

## Prioridad y tamano

- Prioridad: Alta.
- Tamano: Mediana.

## Objetivo

Definir y dejar reproducible el esquema fisico SQLite que soportara la persistencia local del MVP.

## Contexto

La app necesita conservar solicitudes, paquetes, logs, perfiles y configuracion despues de reinicios. El esquema debe reflejar las reglas ya aprobadas: estado SAT separado del ciclo local, eliminacion logica, deduplicacion, acciones pendientes y reconciliacion de archivos ZIP.

## Alcance

### Incluye

- Tablas `perfil_sat`, `credencial_sat`, `solicitud_masiva`, `paquete_solicitud`, `log_solicitud` y `configuracion_app`.
- Claves primarias, claves foraneas, nulabilidad, tipos y valores validos.
- Separacion entre `estado_local` y `estado_solicitud_sat`.
- Codigos y mensajes separados para creacion, verificacion y descarga.
- Eliminacion logica mediante `eliminado_en`.
- `dedup_key`, banderas de acciones pendientes y datos de reconciliacion de archivos.
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
- ADR 0004 y ADR 0014.

## Trabajo esperado

1. Crear `docs/design/sqlite-physical-model.md` con el esquema tabular completo.
2. Resolver y documentar la representacion de UUID, fechas, booleanos, JSON y valores enumerados.
3. Definir restricciones de unicidad para perfiles, credenciales, solicitudes SAT y paquetes.
4. Definir el indice unico parcial para `dedup_key` y los estados que bloquean duplicados.
5. Definir la tabla o mecanismo de versionado de migraciones.
6. Crear la migracion inicial SQLite en la ubicacion establecida por la estructura Qt.
7. Documentar las transacciones y la recuperacion al arrancar.

## Decisiones que debe cerrar esta tarea

- Representacion fisica de identificadores: texto UUID u otra representacion compatible con Qt/SQLite.
- Zona horaria y formato de fechas persistidas.
- Politica de claves foraneas y comportamiento ante eliminacion logica.
- Forma de garantizar que `configuracion_app` tenga un solo registro.
- Forma de almacenar `filtros_sat_json` y `payload_resumen_json` sin guardar secretos.
- Valores exactos de `estado_local`, `estado_solicitud_sat`, `estado_descarga`, `origen_vencimiento` y `motivo_vencimiento`.

## Criterios de aceptacion

- [ ] El documento fisico describe las seis tablas, todas sus columnas, tipos, nulabilidad y restricciones.
- [ ] `solicitud_masiva` separa `estado_local` de `estado_solicitud_sat`.
- [ ] Los codigos y mensajes de creacion, verificacion y descarga no se mezclan.
- [ ] `paquete_solicitud` conserva ruta local, estado de descarga, motivo/origen de vencimiento y datos de reconciliacion.
- [ ] Las cuatro entidades con eliminacion logica tienen `eliminado_en` y reglas de consulta documentadas.
- [ ] El esquema impide dos credenciales activas para el mismo perfil.
- [ ] El esquema impide perfiles activos duplicados por RFC.
- [ ] La regla de solicitudes equivalentes por `dedup_key` queda expresada mediante indice o validacion documentada.
- [ ] `configuracion_app` queda restringida a un solo registro.
- [ ] Los indices cubren monitoreo, paquetes pendientes y logs del detalle.
- [ ] La migracion se aplica correctamente sobre una base vacia.
- [ ] La migracion no modifica una base ya actualizada cuando se ejecuta nuevamente.
- [ ] Las transacciones de crear, verificar, descargar, recuperar y eliminar estan descritas y son compatibles con el esquema.
- [ ] No existe ninguna columna para XML, CFDI individual, clientes, usuarios o funcionalidades fuera del MVP.

## Verificacion

- Aplicar la migracion sobre una base SQLite vacia.
- Ejecutar una segunda vez el mecanismo de migraciones y comprobar que no duplica cambios.
- Inspeccionar tablas, indices y restricciones con `.schema` y consultas SQLite.
- Crear datos representativos para validar unicidad, eliminacion logica y relaciones.
- Comparar el resultado contra `docs/design/operational-rules.md` y `docs/architecture.md`.

## Definicion de terminado

- El esquema fisico y la migracion inicial estan en el repositorio.
- Las decisiones fisicas quedan documentadas.
- Los criterios de aceptacion estan verificados.
- `T003` puede implementar repositorios sin inventar tablas o columnas.

## Resultado

Pendiente.

## Riesgos y notas

- El esquema no debe optimizarse para 10,000 filas de CFDI individuales: el MVP no almacena CFDI, solo solicitudes, paquetes y logs.
- Los ZIP permanecen fuera de SQLite; la base conserva metadata y rutas.
- El modelo debe poder evolucionar mediante migraciones sin requerir reconstruir la base manualmente.

## Referencias

- `docs/design/operational-rules.md`
- `docs/architecture.md`
- ADR 0004
- ADR 0014

