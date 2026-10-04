# T008: Almacenamiento local de paquetes ZIP

## Estado

Pendiente (refinada 2026-10-04; lista para implementar).

## Prioridad y tamano

- Prioridad: Alta.
- Tamano: Mediana.

## Objetivo

Implementar `PackageStorage`, el almacenamiento de paquetes ZIP en el sistema de archivos. Escribe en streaming a un temporal, lo promueve sin reemplazo y con durabilidad, consulta existencia y escanea la carpeta para la recuperacion. Devuelve hechos y nunca cambia estados ni escribe logs de solicitud.

## Contexto

El MVP guarda los ZIP fuera de SQLite, en una ruta fija (`ADR 0004`). Desde la refinacion de `T007`, el ejecutor serial aplica las transiciones de `PaqueteSolicitud`, los logs y la recuperacion; `T009` compone el puerto `OperacionesSat` con `SatGateway` y `PackageStorage`. Por eso T008 se limita a la frontera de filesystem.

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | `{yyyy-mm}` es el mes de `FechaInicial` de la solicitud. Un rango que cruza meses queda en el mes de inicio. | Aprobada por usuario | Refinamiento 2026-10-04. Agrupa por periodo fiscal. |
| D2 | La carpeta de cada solicitud se llama con el id local (UUID canonico) de la app. | Aprobada por usuario | `ADR 0004` (`{solicitud_id}`). Arquitectura proponia `IdSolicitud` SAT saneado; el UUID local siempre existe y no requiere saneamiento. |
| D3 | Un ZIP final en una carpeta sin solicitud asociable se conserva y solo produce un diagnostico saneado en el log de la app (categoria Qt / log unificado de macOS), sin migracion ni tabla nueva. | Aprobada por usuario | `log_solicitud` exige `solicitud_masiva_id`. Como las solicitudes nunca se borran fisicamente, el caso solo ocurre por intervencion manual. |
| D4 | T008 no modifica `PaqueteSolicitud` ni escribe `LogSolicitud`: devuelve hechos. `T007` aplica las transiciones, los logs y la recuperacion; `T009` traduce SAT + almacenamiento a `OperacionesSat`. | Recomendacion tecnica | `qt-architecture-lead`. Elimina el solapamiento con T007 (D3, D5, D14). |
| D5 | Ruta: `<raiz>/<RFC>/<yyyy-mm>/<UUID solicitud>/<archivo>.zip`. Si `id_paquete_sat` cumple `[A-Za-z0-9._-]{1,100}` y no empieza por `.`, el archivo se llama `<id>.zip`. Si no, cada caracter no permitido se sustituye por `_`, el prefijo se trunca a 100 y se agrega `--<16 hex de SHA-256 del id original en UTF-8>`. Cada componente mide como maximo 255 bytes. La ruta es siempre relativa y queda bajo la raiz. | Recomendacion tecnica | `qt-architecture-lead`; coordinador: el hash solo aparece cuando el saneamiento altera el id, asi los nombres normales (`uuid_01.zip`) quedan legibles. |
| D6 | Temporal en la misma carpeta, con nombre estricto `.<archivo>.<16 hex aleatorios>.part`, creado de forma exclusiva con permisos 0600. Directorios nuevos con 0700. Escritura por chunks, `fsync` + `F_FULLFSYNC`, promocion sin reemplazo (`renamex_np` con `RENAME_EXCL`) y `fsync` del directorio. Si el destino existe, el resultado es `ColisionDestino` y nunca se sobrescribe. | Recomendacion tecnica | `qt-architecture-lead`, `ADR 0014`. |
| D7 | Si el `fsync` del archivo falla antes de promover, el resultado es `Durabilidad` y no se promueve. Si la promocion ya ocurrio y falla el `fsync` del directorio, el resultado es exito con `advertenciaDurabilidad=true`: el archivo final existe y el ejecutor lo trata como `Descargado` con una advertencia en su log. | Recomendacion tecnica | Coordinador, ante el riesgo 2 de `qt-quality-engineer`. Devolver error con el final ya presente provocaria una `ColisionDestino` permanente en cada reintento. |
| D8 | `ruta_local` se persiste relativa a la raiz y se valida sin `..` ni componentes absolutos. Raiz inyectable: en produccion `~/SAT-CFDI-Downloader/paquetes`; con `--data-dir`, `<data-dir>/paquetes`; en pruebas, un `QTemporaryDir`. | Recomendacion tecnica | `qt-architecture-lead`, `ADR 0016`. Desarrollo y pruebas nunca tocan la carpeta del usuario. |
| D9 | `escanearRecuperacion()` solo reconoce como propios los archivos dentro de `<RFC>/<yyyy-mm>/<UUID canonico>/`, y como temporales solo los que cumplen el patron de D6. Cada `HallazgoFilesystem` trae `tipo` (`Temporal` o `Final`), `rutaRelativa`, `solicitudId` (UUID) y el nombre del archivo final asociado; no consulta SQLite. Todo lo que no encaja se ignora y nunca se borra. | Recomendacion tecnica | Coordinador, ante el riesgo 1 de `qt-quality-engineer`. Asi T007 asocia hallazgos sin que T008 conozca la base. |
| D10 | Recuperacion, orquestada por T007 antes del primer ciclo: a un temporal asociado a un paquete `Descargando`, T007 le aplica su regla y ordena `eliminarTemporal`. Un temporal propio no asociable se elimina con un diagnostico de app. Un final con solicitud pero sin paquete se conserva y T007 registra `archivo_huerfano`. Un final sin solicitud sigue D3. | Recomendacion tecnica | `qt-architecture-lead`, `operational-rules.md` §Recuperacion. |
| D11 | Detalle: la consulta de existencia se encola en `OperacionExecutor` (solo lectura del filesystem, sin estado ni log) y regresa por una senal encolada. El view model expone `Presente`, `NoEncontrado` o `ErrorComprobacion`, y nunca cambia el estado persistido. | Recomendacion tecnica | `qt-architecture-lead`, `requirements.md` (archivo no encontrado sin cambiar el estado). |
| D12 | Enmienda a `T007` (ya aplicada en `T007` D3 y en su criterio de descarga durante este refinamiento): `FallaOperacion` agrega la fase `Almacenamiento`. Los errores locales de guardado (incluida `ColisionDestino`) llevan el paquete a `Error` con un log `descarga_fallida` saneado. | Recomendacion tecnica | `qt-architecture-lead`. Las fases de T007 no representaban bien los fallos locales. |

## Alcance

### Incluye

- El contrato `PackageStorage` en `src/ports/PackageStorage.h`:
  - `guardarAtomico(UbicacionPaquete, FuenteZipPorChunks&, Cancelacion) -> Resultado<ArchivoFinal{rutaRelativa, advertenciaDurabilidad}, ErrorGuardarZip>`. `ErrorGuardarZip` distingue el error de la fuente de un `ErrorAlmacenamiento`.
  - `existeArchivoFinal(rutaRelativa) -> Resultado<bool, ErrorAlmacenamiento>`.
  - `escanearRecuperacion() -> Resultado<QList<HallazgoFilesystem>, ErrorAlmacenamiento>`.
  - `eliminarTemporal(HallazgoFilesystem)`, que solo acepta temporales propios.
  - `derivarRutaRelativa(UbicacionPaquete)`, pura y determinista.
  - `ErrorAlmacenamiento`: `EntradaInvalida`, `Permiso`, `SinEspacio`, `Escritura`, `Durabilidad`, `Promocion`, `ColisionDestino`, `LecturaRaiz`, `Cancelada`.
- El adaptador de filesystem para macOS/POSIX, con una costura `FileOps` inyectable, y `FakePackageStorage`.
- La resolucion de la raiz (D8) en el composition root.
- La documentacion: D1, D2 y D5 en `ADR 0004` (nota de detalle, sin cambiar la decision) y la seccion de recuperacion de `operational-rules.md`.

### No incluye

- Transiciones de `PaqueteSolicitud`, `LogSolicitud` y orquestacion de la recuperacion (`T007`).
- Descarga SAT y composicion de `OperacionesSat` (`T009`).
- Una operacion para borrar archivos finales, o el borrado de ZIP al eliminar una solicitud o por limpieza automatica.
- Abrir, validar, extraer o indexar el ZIP.
- Cambiar la ruta desde preferencias.
- La pantalla de detalle mas alla de exponer el estado de existencia (la UI la consume en `T009`), y "Mostrar en Finder".

## Dependencias

- `T003` (`Resultado`, logging), `T004` (`--data-dir`, composition root) y `T007` (ejecutor, recuperacion, `FallaOperacion`).
- `ADR 0004`, `ADR 0014`, `ADR 0016`; `docs/design/operational-rules.md`.
- T008 puede implementarse en paralelo a T007, porque el puerto no depende del ejecutor. La conexion de la recuperacion y la consulta de detalle requiere que T007 este integrado.

## Reglas

- El contenido del ZIP nunca aparece en logs ni en diagnosticos; las rutas en diagnosticos son relativas.
- Ninguna operacion del adaptador sobrescribe ni borra un archivo final.
- Toda la E/S corre en el hilo del `OperacionExecutor`, nunca en el hilo grafico.
- Una entrada invalida (RFC no canonico, periodo mal formado, UUID no canonico, id vacio) devuelve `EntradaInvalida` sin tocar el filesystem.

## Trabajo esperado

1. Definir los tipos y el puerto en `src/ports/PackageStorage.h`.
2. Implementar la derivacion de rutas y el saneamiento (D5).
3. Implementar `FileOps` (crear el directorio y el archivo exclusivo, escribir, `fsync`/`F_FULLFSYNC`, promover sin reemplazo, sincronizar el directorio, enumerar, eliminar temporales) y el adaptador sobre esa costura.
4. Implementar `escanearRecuperacion` y `eliminarTemporal` (D9, D10).
5. Implementar `FakePackageStorage` para las pruebas de T007 y T009.
6. Resolver la raiz en el composition root (D8).
7. Actualizar `ADR 0004` y `operational-rules.md`.
8. Escribir las pruebas descritas en Verificacion.

## Criterios de aceptacion

- [ ] Dada la misma `UbicacionPaquete`, cuando se deriva la ruta dos veces, entonces se obtiene la misma ruta relativa `<RFC>/<yyyy-mm de FechaInicial>/<UUID>/<archivo>.zip` bajo la raiz.
- [ ] Dado un `id_paquete_sat` valido (`<uuid>_01`), cuando se deriva el archivo, entonces se llama `<id>.zip` sin sufijo hash.
- [ ] Dados ids con `/`, `..`, espacios, Unicode o un `.` inicial, cuando se deriva la ruta, entonces ningun componente sale de la raiz ni supera 255 bytes. Dos ids distintos que se sanean igual producen nombres distintos gracias al hash.
- [ ] Dada una entrada invalida, cuando se llama a cualquier operacion, entonces devuelve `EntradaInvalida` y no crea nada.
- [ ] Dada una fuente de N chunks, cuando `guardarAtomico` termina con exito, entonces el temporal estuvo en la carpeta final, el final contiene exactamente esos bytes con permisos 0600 (directorios nuevos 0700), no queda el temporal y se devuelve la ruta relativa.
- [ ] Dado un fallo de escritura, `ENOSPC` simulado o una cancelacion en el chunk K, cuando termina `guardarAtomico`, entonces devuelve `Escritura`, `SinEspacio` o `Cancelada` y no existe el archivo final.
- [ ] Dado un directorio sin permiso de escritura (`chmod 0500` en `QTemporaryDir`), cuando se guarda, entonces devuelve `Permiso` sin archivo final.
- [ ] Dado un fallo del `fsync` del archivo, cuando se guarda, entonces devuelve `Durabilidad` y no promueve. Dado un fallo de promocion, devuelve `Promocion` sin archivo final.
- [ ] Dado un fallo del `fsync` del directorio despues de promover, cuando se guarda, entonces devuelve exito con `advertenciaDurabilidad=true` y el final existe.
- [ ] Dado un final preexistente con bytes centinela, cuando se guarda el mismo paquete, entonces devuelve `ColisionDestino` y el archivo no cambia.
- [ ] Dada una raiz con temporales propios, temporales ajenos, finales con UUID valido y archivos fuera de la estructura, cuando se escanea, entonces solo se reportan los propios con sus campos de D9, y los ajenos no se reportan ni se borran.
- [ ] Dado un hallazgo temporal propio, cuando se llama a `eliminarTemporal`, entonces se borra solo ese temporal. Dado un hallazgo final, la llamada se rechaza y el archivo permanece.
- [ ] Dado un final sin solicitud asociable, cuando T007 procesa el escaneo, entonces el archivo permanece y se emite un diagnostico saneado en el log de la app, sin escribir en `log_solicitud`.
- [ ] Dada una ruta relativa existente o ausente, cuando el detalle consulta la existencia, entonces recibe `Presente` o `NoEncontrado` en el hilo grafico y el estado persistido del paquete no cambia. Un error de lectura produce `ErrorComprobacion`.
- [ ] Con `--data-dir <dir>`, la raiz efectiva es `<dir>/paquetes`; ninguna prueba crea nada bajo `~/SAT-CFDI-Downloader`.
- [ ] El puerto no expone ninguna operacion para borrar archivos finales, y ningun log ni diagnostico contiene bytes del ZIP.

## Verificacion

1. `ctest --test-dir build --output-on-failure`:
   - `unit`: derivacion de rutas, saneamiento y `FakePackageStorage`.
   - `infrastructure`: el adaptador real sobre `QTemporaryDir`, con `FileOps` programable para fallar por operacion y por llamada (`ENOSPC`, `fsync`, promocion, `fsync` del directorio) y con fuente por chunks y cancelacion controlables.
   - `integration`: la recuperacion y la consulta de detalle orquestadas por T007, una vez integrado.
2. Los permisos se prueban con `chmod` dentro de `QTemporaryDir`, restaurados en la limpieza; no se llena el disco real.
3. Prueba manual: ejecutar la app con `--data-dir` y comprobar que la carpeta `paquetes` se crea bajo ese directorio.

## Definicion de terminado

- `PackageStorage` cubre derivacion, guardado atomico, existencia, escaneo y eliminacion de temporales, con pruebas deterministas.
- Ninguna ruta de codigo sobrescribe ni borra archivos finales.
- `T007` y `T009` consumen el puerto sin conocer los detalles del filesystem.

## Resultado

Pendiente.

## Riesgos y notas

- Un rename atomico no sustituye la transaccion SQLite: la coherencia final la dan T007 (marcar `Descargando` antes, aplicar `Descargado` despues y recuperar al arrancar).
- `ColisionDestino` para un paquete descargable deja el paquete en `Error` hasta que el usuario mueva el archivo. Es un caso raro (archivo puesto a mano) y se prefiere a sobrescribir.
- El MVP no garantiza que el ZIP sea valido internamente, y la retencion de ZIP es responsabilidad del usuario.
- La comprobacion completa de que eliminar una solicitud no borra los ZIP se hace en `T009` y `T010`.

## Referencias

- `ADR 0004`, `ADR 0014`, `ADR 0016`
- `docs/design/operational-rules.md`, `docs/requirements.md` (ZIP, detalle, eliminacion)
- `T007` (D3, D5, D6, D14), `T009`
- `docs/meetings/T008-refinamiento/` (bitacora y rondas del refinamiento)
