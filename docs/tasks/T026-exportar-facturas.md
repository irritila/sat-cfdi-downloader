# T026: Exportar facturas

## Estado

Completada (2026-10-26).

## Prioridad y tamano

Media. Mediana: tres subtareas.

## Objetivo

Que el usuario saque de la app las facturas de un perfil fiscal en un solo ZIP con los XML originales y, si lo elige, su PDF, por mes, rango de meses o facturas elegidas.

## Contexto

Propuesta del usuario (2026-10-24), antes de T024. Hoy la pantalla Facturas (T023.4) permite ver cada CFDI y guardar su PDF de uno en uno (T015.4); no hay forma de obtener varias facturas a la vez. `docs/requirements.md` deja fuera del MVP las "exportaciones avanzadas"; D6 registra esta exportacion simple como ampliacion aceptada. Sesion: `docs/meetings/T026-refinamiento/`.

## Decisiones

| ID | Decision | Estado | Fuente |
|---|---|---|---|
| D1 | La exportacion produce un solo archivo ZIP en la ubicacion que elige el usuario. | Aprobada por usuario | 2026-10-24 |
| D2 | El ZIP contiene los CFDI en XML original; incluir su representacion PDF (T015.4) es opcional. No hay resumen CSV o XLSX. | Aprobada por usuario | 2026-10-24 |
| D3 | Se exporta por mes, por rango de meses o eligiendo facturas. | Aprobada por usuario | 2026-10-24 |
| D4 | Una factura cuyo paquete ZIP falta o esta en descarga no se exporta. | Aprobada por usuario | 2026-10-24 |
| D5 | Antes de exportar se muestra un aviso de que el ZIP contiene datos fiscales sin cifrar. | Aprobada por usuario | 2026-10-24 |
| D6 | Esta exportacion simple es una ampliacion de alcance aceptada; las exportaciones avanzadas e integraciones contables siguen fuera (`docs/requirements.md`). | Aprobada por usuario | 2026-10-24, al definir D1-D5 |
| D7 | Entradas del ZIP: `emitidas/AAAA-MM/<UUID>.xml` y `<UUID>.pdf` junto a su XML; igual bajo `recibidas/`. AAAA-MM es el mes del catalogo mensual (ADR 0019). XML y PDF comparten el nombre resuelto. | Aprobada por usuario | 2026-10-24 |
| D8 | Para mes o rango se elige Emitidas, Recibidas o Ambas; por omision la pestana actual. Con facturas elegidas no hay selector: se exporta exactamente lo elegido, de una o ambas direcciones. | Aprobada por usuario (selector); facturas elegidas: recomendacion tecnica | 2026-10-24; ronda 02 calidad |
| D9 | Al terminar, la app muestra dos conteos separados: CFDI indexados cuyo XML no se exporto (omitidas) y CFDI exportados sin PDF; el ZIP lleva `omitidas.txt` (D17). De un paquete no descargado no se conoce su contenido: los meses con descargas pendientes o cobertura incompleta se listan como lineas de mes y no cuentan como CFDI omitidos. | Aprobada por usuario (lista en el ZIP); limite: recomendacion tecnica | 2026-10-24; rondas 01 y 02 |
| D10 | Aviso fijo en el dialogo de exportar, sin casilla para ocultarlo ni confirmacion aparte: "El archivo que vas a crear contiene tus facturas completas y no está protegido con contraseña. Cualquier persona que lo abra podrá verlas. Si lo guardas en iCloud Drive, Dropbox o una carpeta compartida, se copiará fuera de este equipo. Guárdalo en un lugar de confianza y bórralo cuando ya no lo necesites." | Aprobada por usuario (forma); texto: recomendacion de seguridad | 2026-10-24; ronda 02 seguridad H3 |
| D11 | La exportacion corre en un hilo propio, una a la vez, fuera del executor SAT (ADR 0014): no llama al SAT, no cambia estados persistidos ni modifica los ZIP de paquetes. Toma una instantanea del plan por `PersistenceDispatcher` y, antes de cada lectura, revalida que el paquete siga visible; relee cada XML por `(paqueteId, posicion)` con el lector seguro (T015.1), lo vuelve a parsear con `ParserCfdi` y compara el UUID esperado (y el SHA-256 si el indice lo guarda). Si no coincide, se omite con motivo "cambió desde que se indexó". ADR nuevo "Exportacion local atomica de CFDI"; aclara la regla de `docs/architecture.md` sobre ZIP de paquetes. | Recomendacion tecnica | Rondas 01 y 02 |
| D12 | Escritura: escritor ZIP propio en streaming (`EscritorZipStreaming`, ADR 0023) sobre el descriptor ya reservado: cada entrada se escribe al agregarla con bytes ya validados, sin temporales ni buffers con todo el contenido; libzip queda como lector. El `.partial` se crea con `O_EXCL|O_NOFOLLOW` y `0600` en una carpeta privada de la app (`0700`, fuera de lo que respalda T025 y sustituye T025.5) si esta en el mismo volumen que el destino; si no, en la carpeta destino, con un registro local minimo para limpiarlo. Publicacion: `F_FULLFSYNC`, `renamex_np(RENAME_EXCL)` y `fsync` de los directorios. Nunca sobrescribe; ante colision pide otro nombre. Cancelar o fallar antes del rename borra solo el `.partial` propio; si solo falla el `fsync` del directorio despues del rename, el ZIP queda publicado y se informa con advertencia. Al arrancar se borran los `.partial` propios huerfanos (archivos regulares, patron exacto, `O_NOFOLLOW`). | Recomendacion tecnica | Ronda 01; ronda 02 seguridad H1, H2, calidad |
| D13 | PDF con una instancia propia del renderer por exportacion, en memoria, sin `PrevisualizacionCfdiService` ni su cache; cancelacion antes de cada render; metadatos del PDF sin RFC ni nombre. Un CFDI sin PDF posible (`VersionNoSoportada`, fallo de render) exporta su XML y cuenta como "sin PDF". | Recomendacion tecnica | Ronda 01; ronda 02 seguridad H6 |
| D14 | Seleccion en un `ExportacionFacturasViewModel` aparte, por identidad `(paqueteId, posicion)`, que sobrevive filtros, pestanas y recargas; `FacturasViewModel` y `FacturasListModel` no cambian su contrato. Cambiar de perfil vacia la seleccion. Una factura elegida que ya no esta disponible al exportar se omite con motivo "ya no disponible"; si su UUID sigue en el mes y direccion desde otra fuente canonica (ADR 0019), se exporta esa fuente. | Recomendacion tecnica | Rondas 01 y 02 |
| D15 | Nombre sugerido del ZIP sin RFC: `Archivo-Fiscal-<AAAA-MM>.zip`, `Archivo-Fiscal-<AAAA-MM>_<AAAA-MM>.zip` para rango y `Archivo-Fiscal-seleccion-<AAAA-MM-DD>.zip` para facturas elegidas. El usuario puede cambiarlo; se sanea: sin `/` ni `:`, no empieza con `.`, extension `.zip` unica, maximo 255 bytes UTF-8. | Aprobada por usuario (sin RFC); formato: recomendacion tecnica | 2026-10-24; ronda 02 seguridad H8 |
| D16 | Antes de elegir destino el dialogo muestra los CFDI locales que se exportaran. Con 0 (alcance vacio, mes sin facturas acreditado por 5004, seleccion vacia) "Exportar" queda deshabilitado con el texto "No hay facturas para exportar"; no se crea un ZIP vacio. | Recomendacion tecnica | Ronda 02 calidad |
| D17 | `omitidas.txt`, UTF-8 con LF: una linea `<direccion>/<AAAA-MM>/<UUID>: <motivo>` por CFDI, con motivo del catalogo fijo ("no disponible en este equipo", "ilegible", "cambió desde que se indexó", "ya no disponible", "sin PDF" cuando el XML si se incluyo), y una linea `<direccion>/<AAAA-MM>: descargas pendientes` o `: cobertura incompleta` por mes. Solo UUID canonico (`normalizarUuid`); nunca ids internos, rutas, RFC, mensajes de error ni texto del XML. Sin lineas, no hay archivo. | Recomendacion tecnica | Ronda 02 seguridad H7, calidad |
| D18 | Nombres de entrada solo con UUID canonico; otro valor o UUID vacio usa `sin-uuid-<n>`; una colision usa el sufijo `-<n>` (n desde 2 en el orden del plan), igual para XML y PDF. Entradas regulares con atributos `0600`, fecha de la exportacion, sin comentarios, campos extra, `__MACOSX` ni `._*`; ZIP64 solo si hace falta; el ZIP final `0600`, sin ACL y sin atributos extendidos salvo `com.apple.provenance` (lo asigna macOS). Las entradas usan solo `[A-Za-z0-9._-]` y `/`. | Recomendacion tecnica | Ronda 02 seguridad H5, H9, calidad |
| D19 | Coordinacion: eliminar un paquete localmente (ADR 0015) hace que sus CFDI pendientes se omitan; una restauracion (T025.5) o salir de la app cancela la exportacion y limpia el `.partial` antes de sustituir datos o terminar. | Recomendacion tecnica | Ronda 02 calidad, seguridad H10 |
| D21 | `omitidas.txt` va en la raiz del ZIP. La carpeta destino no puede estar dentro de los datos de la app. | Recomendacion tecnica | T026.1 (corte 3, seguridad 6) |
| D22 | Si todas las facturas planeadas fallan al leerse, se publica un ZIP que solo contiene `omitidas.txt`; la UI muestra un texto distinto cuando `exportadas == 0`. | Recomendacion tecnica | T026.2 (seguridad, coordinador) |
| D20 | Logs y notificaciones: solo fases, conteos y codigos (`errno`, codigo de libzip); nunca nombre del ZIP, rutas destino o temporal, `IdPaquete`, nombres de entradas, UUID, RFC ni texto de error con ruta. | Recomendacion tecnica | Ronda 02 seguridad H4 |

## Subtareas

| Tarea | Resumen | Depende de |
|---|---|---|
| [T026.1](T026.1-plan-y-escritura-zip.md) | ADR, plan de exportacion (mes, rango, seleccion, direccion, omitidas, conteo previsto), asignador de nombres, puertos, `EscritorZipStreaming`, destino atomico, limpieza al arrancar; fakes de destino con fallas y de lectura por posicion con compuerta | T015.1, T015.2 |
| [T026.2](T026.2-servicio-exportacion.md) | `ExportacionFacturasService`: XML revalidado, PDF, `omitidas.txt`, progreso, cancelacion, coordinacion D19, resultado; pruebas de integracion con executor real y SAT fake | T026.1, T015.4 |
| [T026.3](T026.3-ui-exportacion.md) | `ExportacionFacturasViewModel`, seleccion multiple, rango, dialogo con aviso, conteo y destino, progreso y resultado; pruebas Qt Quick; manual, requirements y architecture | T026.2, T023.4 |

## Alcance

### Incluye

- Seleccion por perfil y por mes, rango de meses o facturas elegidas en la pantalla Facturas; direccion segun D8.
- Un ZIP con la estructura D7 y el nombre D15, PDF opcional, `omitidas.txt` segun D9 y D17, y aviso D10.
- Progreso por fases (preparando, XML, PDF, finalizando) con conteo y cancelacion cooperativa hasta antes de publicar.
- Resultado: XML y PDF exportados, facturas sin PDF y omitidas.

### No incluye

- Resumen CSV o XLSX.
- Integraciones con sistemas contables, polizas, conciliacion o validacion de vigencia o cancelacion.
- Envio por correo o subida a servicios externos.
- Exportar meses futuros o fuera del horizonte navegable de Facturas.
- Cambios al vocabulario y materiales transversales (T024).

## Dependencias

- T015.1 (lector ZIP seguro), T015.2 (indice), T015.4 (PDF), T023.4 (pantalla Facturas), ADR 0014, 0015, 0019.

## Trabajo esperado

- ADR "Exportacion local atomica de CFDI" y ajuste de la regla de ejecucion en `docs/architecture.md`.
- Dominio, puertos, servicio, adaptadores libzip y de sistema de archivos, view model y QML segun D11-D20.
- Fakes: destino con fallas inyectables y lector con compuerta en la lectura por posicion (los actuales solo bloquean `recorrer()`).
- `docs/requirements.md` (RF nuevo y nota en fuera del MVP), `docs/manual/usuario.md`.

## Criterios de aceptacion

Subtarea primaria entre corchetes. Datos sinteticos (`XAXX010101000`, `XEXX010101000`) y `QTemporaryDir`.

- [x] [T026.1/T026.2] Dado un perfil con 2 meses indexados de emitidas y recibidas, exportar el rango con Ambas produce un ZIP cuyas entradas son exactamente `emitidas|recibidas/AAAA-MM/<UUID>.xml`, una por CFDI canonico, con SHA-256 identico al XML del paquete fuente; un CFDI presente en dos paquetes aparece una vez. Con Emitidas solo hay entradas `emitidas/`.
- [x] [T026.2] Con PDF activado cada XML tiene su `.pdf` con el mismo nombre resuelto; con renderer fake que falla o un CFDI `VersionNoSoportada` solo va el XML y `omitidas.txt` dice "sin PDF". Con PDF desactivado no hay `.pdf`. Un XML con textos `<b>`, `<img src=http://...>` y `file://` se dibuja literal y no hace peticiones de red.
- [x] [T026.1/T026.2] Con fakes, un CFDI cuyo paquete esta `OrigenNoDisponible`, `Ilegible`, desaparece antes de su lectura, cuyo XML releido tiene otro UUID o una factura elegida que ya no existe no se exporta; `omitidas.txt` coincide byte a byte con el texto esperado (D17), incluidas las lineas de mes con descargas pendientes o cobertura incompleta; sin lineas no hay archivo. El conteo de omitidas de la app es igual al numero de lineas de CFDI con motivo distinto de "sin PDF", y el de "sin PDF" al de esas lineas.
- [x] [T026.1] El asignador produce `sin-uuid-<n>` para UUID vacio o no canonico y `-<n>` en colisiones, igual para XML y PDF; ninguna entrada contiene nombres internos de paquetes, `..`, rutas absolutas ni enlaces. La inspeccion del ZIP muestra comentario vacio, atributos `0100600` y fecha de la exportacion.
- [x] [T026.1] Con fallas inyectadas en crear, escribir, cerrar, `F_FULLFSYNC`, rename y disco lleno, o cancelacion en cada fase previa al rename: no hay ZIP final, el `.partial` propio se borra y un archivo previo con ese nombre queda intacto. Con falla solo en el `fsync` del directorio tras el rename, el ZIP queda publicado y el resultado trae advertencia. Durante y despues de exportar, en la carpeta privada y en la destino no hay otros archivos que el `.partial` propio y el ZIP final. El ZIP final tiene `0600` (`stat`), sin ACL y sin atributos extendidos salvo `com.apple.provenance`, que macOS asigna y no se puede retirar.
- [x] [T026.1] Un destino existente no se reemplaza, tampoco si aparece entre el dialogo y el rename; se pide otro nombre. El nombre se sanea segun D15 (vacio, sin extension, doble extension, `/`, `:`, inicio con `.`, mas de 255 bytes).
- [x] [T026.1] Si el proceso termina durante la exportacion (simulado dejando un `.partial` propio), el siguiente arranque lo borra; un archivo ajeno, un enlace o un nombre que no sigue el patron permanecen.
- [x] [T026.2] Con un lector que se bloquea en la lectura por posicion, la exportacion no corre en el hilo de UI ni en el executor: el VM procesa cancelar y una operacion SAT fake encolada en el executor real termina mientras la exportacion sigue bloqueada. Pedir otra exportacion durante una en curso no inicia otra. Cancelar dos veces o despues del rename no cambia el resultado.
- [x] [T026.2] Eliminar localmente un paquete durante la exportacion omite sus CFDI pendientes; iniciar una restauracion o salir de la app cancela, borra el `.partial` y termina antes de sustituir datos o cerrar; nunca se publica un ZIP con entradas truncadas.
- [x] [T026.2] Memoria maxima medida con 10,000 CFDI sinteticos con PDF y reportada; ningun buffer acumula todo el contenido (prueba omitible en CI).
- [x] [T026.3] En Facturas: "Exportar este mes"; rango de meses que rechaza futuros, invertidos y fuera del horizonte navegable y acepta el mismo mes en ambos extremos; modo de seleccion que conserva lo elegido al cambiar de pestana, filtro o recarga y se vacia al cambiar de perfil. El dialogo muestra el aviso D10, el selector de direccion solo para mes o rango, la opcion de PDF y los CFDI que se exportaran; con 0, "Exportar" esta deshabilitado con "No hay facturas para exportar".
- [x] [T026.3] Progreso por fase y conteo, cancelar y resultado (exportadas, sin PDF, omitidas, advertencia) visibles; pruebas Qt Quick de foco, Tab, Escape y nombres accesibles; textos sin "solicitud", "paquete" ni "verificacion". VoiceOver va en la lista manual.
- [x] [T026.1-T026.3] Con datos sonda (`XAXX010101000`, un UUID sintetico, `/Users/usuario`, un nombre de archivo hostil) los logs y notificaciones capturados no contienen ninguno; solo fases, conteos y codigos.
- [x] [T026.1/T026.3] ADR registrado; `docs/architecture.md`, `docs/requirements.md` y `docs/manual/usuario.md` actualizados.
- [x] [T026.1-T026.3] Build `-Werror`, `ctest` y `git diff --check` pasan.
- [x] Prueba manual del usuario con un perfil real (cierra la epica, no bloquea las subtareas): exportar un mes con PDF a una carpeta local, abrir el ZIP en Finder, comparar 3 CFDI con los de la app y revisar el aviso con VoiceOver.

## Verificacion

`ctest` con ZIP sinteticos y adaptadores de sistema de archivos con fallas inyectadas; pruebas Qt Quick de la pantalla y el dialogo; build `-Werror` y `git diff --check`. Manual del usuario en la app productiva. Solo el usuario hace operaciones SAT reales.

## Resultado

T026.1 (plan y escritura atomica del ZIP), T026.2 (servicio en hilo propio) y T026.3 (UI, dialogo con aviso, seleccion y documentacion). Prueba manual del usuario con un perfil real (2026-10-26): exportacion de un mes con PDF a una carpeta local, ZIP revisado en Finder, 3 CFDI comparados con la app y dialogo con VoiceOver: funciona bien.

## Riesgos

- El ZIP exportado queda sin cifrar donde el usuario lo guarde (mitigado con D5, D10 y D15).
- El comportamiento de libzip al escribir (D12) se infirio; T026.1 lo verifica con la version enlazada.
- Exportaciones grandes con PDF tardan: el render es por CFDI.
- Concurrencia con eliminacion local, restauracion o migracion de carpeta de paquetes (T019).

## Referencias

- `docs/requirements.md` (fuera del MVP; D6), `docs/architecture.md` (regla de ejecucion).
- ADR 0014, 0015, 0019.
- T015.1, T015.2, T015.4, T023.4, T024, T025.5.
- `docs/meetings/T026-refinamiento/`.
