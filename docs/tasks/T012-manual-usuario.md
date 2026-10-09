# T012: Manual de usuario con capturas

## Estado

Completada (2026-10-09). Refinada 2026-10-08; implementada en la rama `t012-manual-usuario`.

## Prioridad y tamano

- Prioridad: Media.
- Tamano: Mediano.

## Objetivo

Generar un manual de usuario en Markdown con capturas PNG locales y reproducibles de la UI, que sirva de base para refinar la interfaz.

## Contexto

El usuario quiere refinar la UI y necesita primero un manual que muestre como se ve y se usa hoy la app (pedido del 2026-10-08). Las capturas se generan con una herramienta para poder repetirlas tras cada cambio de UI sin exponer datos reales.

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | El manual vive en `docs/manual/usuario.md` (Markdown, espanol) y sus imagenes en `docs/manual/img/*.png`, con nombres ordenados y estables (`NN-tema.png`). | Aprobada por usuario | Pedido del usuario: Markdown e imagenes PNG locales. Ubicacion de `qt-architecture-lead`. |
| D2 | Las capturas de pantallas QML se generan con una herramienta automatica: target CMake opcional (`SATCFDI_BUILD_MANUAL_CAPTURAS`, target `manual_capturas`) en `tools/capturas-manual/`, que no forma parte de la app ni de su bundle. | Aprobada por usuario | Refinamiento 2026-10-08. |
| D3 | La herramienta carga el modulo QML real con `PresentacionViewModels` sobre fakes deterministas, como las pruebas de presentacion. No usa el composition root productivo, SQLite real, el worker, el SAT ni Keychain, y no agrega un "modo demo" a los view models productivos. | Recomendacion tecnica | `qt-architecture-lead`: el root productivo construiria servicios reales aun con `--data-dir`. |
| D4 | Render en macOS con la plataforma `cocoa` (no `offscreen`), ventana de 960x640, tema claro, locale `es_MX`, zona UTC y reloj fijo. Se espera un frame estable antes de `QQuickWindow::grabWindow()` y el PNG se normaliza a una escala fija (2x, 1920x1280) sin metadatos variables. | Recomendacion tecnica | `qt-architecture-lead`: `offscreen` no reproduce controles ni fuentes de macOS. |
| D5 | Los elementos nativos (menu del menu bar, notificacion, Finder y selector de archivos) se generan como ilustraciones QML dibujadas por la misma herramienta, con datos ficticios y marcadas como "Ilustracion" en el manual. | Aprobada por usuario | Refinamiento 2026-10-08: el usuario pidio generarlas en lugar de capturarlas a mano. |
| D6 | Las ilustraciones no duplican textos. Las opciones del menu bar salen de una definicion compartida con el adaptador macOS: si hoy estan solo en `MacOSIntegration.mm`, se extraen a una fuente comun sin cambiar el comportamiento. Los textos de notificacion se obtienen de `ServicioNotificaciones` con un `Notificador` fake. | Recomendacion tecnica | Coordinador: evita que las ilustraciones se desactualicen respecto de la app. |
| D7 | Datos sinteticos solamente: RFC genericos del SAT (`XAXX010101000`), UUID ficticios fijos, nombres `demo.cer`/`demo.key`, rutas bajo `/Users/usuario/...` ficticias. Nunca datos, rutas ni e.firmas reales. | Recomendacion tecnica | Reglas de T006 D6; `qt-quality-engineer`. |
| D8 | La generacion no forma parte de `ctest` (requiere sesion grafica cocoa y el render varia por version de Qt, macOS y fuentes). El determinismo exigido es de contenido (mismos archivos, escenarios y textos), no de bytes. Se agrega una prueba ligera, sin render, que valida que cada imagen enlazada en el manual existe y que no hay PNG huerfanos. | Recomendacion tecnica | `qt-architecture-lead`; el coordinador relajo la comparacion por hash propuesta por calidad. |

## Alcance

### Incluye

- La herramienta `tools/capturas-manual/` y su fixture sintetico (D2-D4, D7).
- Las capturas de pantallas QML y las ilustraciones nativas (D5, D6).
- La extraccion minima de la definicion del menu bar si hace falta para D6, sin cambiar textos ni comportamiento.
- El manual `docs/manual/usuario.md` y la prueba de enlaces (D8).
- Instrucciones de regeneracion en `docs/development.md`.

### No incluye

- Cambios de UI: los decide un refinamiento posterior a partir del manual.
- Capturas manuales o automatizadas de la UI nativa real (`screencapture`).
- Traducciones o manual para terceros.
- Comparacion visual por pixeles en CI.

## Contenido del manual

Secciones (orden orientado a flujos):

1. Perfiles SAT y registro o reemplazo de la e.firma.
2. Crear una nueva solicitud y resolver duplicados.
3. Envio y monitoreo.
4. Seguimiento en la lista y acciones disponibles.
5. Detalle: filtros, estados, historial y paquetes.
6. Acceso a paquetes en Finder (T009.1).
7. Menu bar: monitoreo, pausa, carpeta de paquetes y salida.
8. Notificaciones de macOS.
9. Mensajes y errores frecuentes, y como recuperarse.

Imagenes minimas (los nombres finales pueden ajustarse manteniendo el orden):

| Imagen | Tipo | Estado de datos |
| --- | --- | --- |
| `01-perfiles.png` | Captura | Un perfil con credencial `Lista` y otro inactivo o sin e.firma |
| `02-efirma.png` | Captura | Dialogo QML de e.firma con `demo.cer`/`demo.key`, sin contrasena visible |
| `03-selector-archivos.png` | Ilustracion | Selector de archivos con `demo.cer` |
| `04-nueva-solicitud.png` | Captura | Perfil listo, fechas fijas, filtros de ejemplo |
| `05-duplicado.png` | Captura | Confirmacion de solicitud equivalente |
| `06-lista.png` | Captura | Solicitudes en `Creada`, `Enviada`, `Terminada` y `ErrorSat` |
| `07-lista-vacia.png` | Captura | Sin solicitudes |
| `08-detalle-terminada.png` | Captura | Paquetes `Descargado`, `Disponible` y `Vencido`, con historial |
| `09-detalle-incidencia.png` | Captura | Paquete con error o ZIP no encontrado y su mensaje |
| `10-finder.png` | Ilustracion | Finder con el ZIP ficticio seleccionado |
| `11-menu-bar.png` | Ilustracion | Menu con monitoreo activo, pendientes y acciones |
| `12-notificacion.png` | Ilustracion | `Descarga completa: 1 de 1` sin RFC |

## Dependencias

- T001-T009.1 (UI vigente). Si T009.1 no esta integrada en `main`, la rama de T012 parte de `t009.1-acceso-paquetes`.
- ADR 0011, 0012.

## Trabajo esperado

1. Fixture sintetico y herramienta de capturas (D2-D4, D7).
2. Ilustraciones QML de los elementos nativos y, si hace falta, la definicion compartida del menu bar (D5, D6).
3. Generar las imagenes y redactar `docs/manual/usuario.md`.
4. Prueba de enlaces e inventario (D8) e instrucciones de regeneracion.

## Criterios de aceptacion

- [x] Dado `-DSATCFDI_BUILD_MANUAL_CAPTURAS=ON`, cuando se ejecuta el target `manual_capturas` en macOS, entonces se escriben todas las imagenes de la tabla en `docs/manual/img/`, con la escala y dimensiones de D4, y el proceso termina con codigo 0. Sin la opcion, la app y `ctest` no cambian.
- [x] Dada la herramienta, cuando se ejecuta, entonces no abre la base real, no toca Keychain, no inicia el worker ni hace trafico de red.
- [x] Dadas dos ejecuciones seguidas en el mismo equipo, cuando se comparan, entonces producen los mismos archivos con los mismos escenarios y textos visibles.
- [x] Dado el manual, cuando la prueba de enlaces corre en `ctest`, entonces cada imagen referenciada existe y no hay PNG sin referenciar en `docs/manual/img/`.
- [x] Dadas las imagenes y el manual, cuando se inspeccionan, entonces no contienen RFC, Ids, rutas, e.firmas ni contrasenas reales; solo los datos de D7.
- [x] Dada una ilustracion nativa, cuando se ve en el manual, entonces esta rotulada como "Ilustracion", y sus textos coinciden con los del menu bar y las notificaciones de la app (D6).
- [x] Dado un cambio de texto en el menu bar o en una notificacion, cuando se regeneran las imagenes, entonces la ilustracion refleja el texto nuevo sin editar la herramienta.
- [x] El manual cubre las nueve secciones con al menos una imagen o un paso descrito en cada una.
- [x] `docs/development.md` explica como regenerar el manual.

## Verificacion

1. `cmake -DSATCFDI_BUILD_MANUAL_CAPTURAS=ON ...` y `cmake --build <dir> --target manual_capturas` en macOS; inventario de PNG y dimensiones.
2. Dos ejecuciones y comparacion de nombres, escenarios y textos.
3. `ctest --test-dir build` incluida la prueba de enlaces.
4. Revision visual de las imagenes por el usuario y escaneo de datos reales (RFC, Ids, rutas personales).

## Resultado

- Herramienta `tools/capturas-manual/` (opcion `SATCFDI_BUILD_MANUAL_CAPTURAS`, target `manual_capturas`) sobre fakes deterministas, render
  cocoa, 12 PNG de 1920x1280 comprimidos sin perdida (maximo 354 KB, limite de 1 MB en la herramienta).
- Ilustraciones QML del menu bar, notificacion, Finder y selector de archivos; el menu sale de `src/infrastructure/os/MenuBarDefinicion.h`,
  compartida con `MacOSIntegration.mm`, y la notificacion de `ServicioNotificaciones`.
- `docs/manual/usuario.md` con 9 secciones; prueba `satcfdi_manual_enlaces` (label `docs`) en `ctest`.
- Bug corregido durante la tarea (aprobado por el usuario): los ComboBox "Tipo de descarga" y "Tipo de comprobante" de Nueva solicitud se
  mostraban vacios al abrir; prueba `combosMuestranValorInicial`.
- `ctest` 13/13; revision Codex de calidad aprobada sin bloqueantes; el usuario reviso el manual, el menu bar real y la correccion (2026-10-09).
- Limitaciones: render no identico en bytes entre corridas (aceptado por D8); la captura 08 recorta la fila "Resumen".

## Riesgos y notas

- El render varia segun Qt, macOS y fuentes; por eso el determinismo es de contenido, no de bytes.
- Las ilustraciones pueden diferir visualmente de macOS; el rotulo "Ilustracion" lo hace explicito.
- Los PNG aumentan el tamano del repositorio; se mantienen a escala fija y se regeneran solo cuando cambia la UI.

## Referencias

- `src/presentation/qml/**`, `tests/presentation/` (fixtures)
- `src/infrastructure/os/macos/MacOSIntegration.mm` (menu bar)
- `src/application/notificaciones/ServicioNotificaciones.*`
- `docs/meetings/T012-refinamiento/` (bitacora y rondas)
