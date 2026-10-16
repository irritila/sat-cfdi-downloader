# T017: Video tutorial de la aplicacion

## Estado

Pendiente (refinada 2026-10-15). Bloqueada por T018 (roles especialistas); lista para `/development-cycle T017` cuando T018 se complete.

## Prioridad y tamano

- Prioridad: Media.
- Tamano: Grande (herramienta nueva de grabacion, guion, voz y ensamblado). Puede dividirse en el ciclo de desarrollo (ver Trabajo esperado).

## Objetivo

Generar con un procedimiento automatizado y reproducible un video tutorial personal de SAT CFDI Downloader, con capitulos, voz sintetica y subtitulos, que muestre el flujo completo de la app con datos sinteticos y sin tocar el SAT, el Keychain ni datos reales.

## Contexto

El manual (T012, T016) describe la app con capturas estaticas generadas por `tools/capturas-manual`. El video agrega el flujo en movimiento. El flujo real requiere e.firma y respuestas del SAT, que no pueden aparecer. En el refinamiento, una prueba de concepto de `qt-platform-engineer` grabo cuadro a cuadro una ventana QML (1920x1080, 30 fps) y mezclo voz con resultados deterministas; `qt-architecture-lead` (Codex) confirmo que worker, `Programador`, indexador y visor PDF no son deterministas con tiempo logico, de ahi la arquitectura hibrida (D6).

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | Video personal: MP4 local fuera de git (en el directorio de build, por ejemplo `build/video/`). No se publica. | Aprobada por usuario | Ronda 1. Uso personal; evita peso en la historia de git. |
| D2 | Datos en pantalla de un modo demo sintetico: RFC `XAXX010101000` / `XEXX010101000`, nombres inventados, rutas `/Users/usuario`, UUID y montos ficticios. | Aprobada por usuario | Ronda 1. Sin riesgo de filtrar datos y reproducible. |
| D3 | Titulos de capitulo quemados en el video (overlay QML), subtitulos `.srt` aparte y voz sintetica. Subtitulos y voz salen del mismo guion. | Aprobada por usuario | Rondas 1 y 3. |
| D4 | Grabacion automatizada dentro del proceso: `grabWindow` por cuadro con `QAnimationDriver` propio, `QSG_RENDER_LOOP=basic`, cursor simulado en QML y cuadros RGB enviados por stdin a ffmpeg. Sin ScreenCaptureKit ni permiso de Grabacion de pantalla. | Aprobada por usuario (automatizada); metodo por recomendacion tecnica | `qt-platform-engineer`, PoC: PSNR ~100 dB entre corridas con loop `basic`. |
| D5 | Voz con la API TTS de OpenAI, en espanol de Mexico. Modelo, voz e instrucciones de tono se fijan en el guion. Los clips se guardan en un cache por hash (texto hablado, modelo, voz, instrucciones), unico y por omision en `build/video/tts-cache/` (ignorado por git); la grabacion solo usa el cache. El paso de voz sintetiza en un directorio de staging y publica en el cache de forma atomica solo si todo el lote termina; ante error conserva el cache previo y no deja clips nuevos visibles. | Aprobada por usuario | El usuario pidio un SDK de Claude/Codex; ninguno sintetiza audio. Consecuencias aceptadas: costo por uso, el guion (solo datos sinteticos) sale del equipo, el audio no es determinista entre llamadas (el cache lo fija). |
| D6 | Arquitectura hibrida: servicios fijos mutables, gobernados por el guion, para perfiles, solicitudes, transiciones y lista (sin SQLite, worker, `Programador` ni `OperacionExecutor`). Lector ZIP, parser y `RenderizadorPdfCfdi` reales sobre un ZIP sintetico preparado antes de grabar. El guion espera la senal `documentoListo` del visor, nunca un numero de cuadros; si llega `documentoError` o vence un timeout acotado (fijado en el guion), la grabacion falla con mensaje que nombra la escena. | Recomendacion tecnica | `qt-architecture-lead` (ronda 04) y `qt-platform-engineer` coinciden. Determinismo y menor costo; fidelidad real donde importa (explorador y PDF). |
| D7 | Un solo MP4 de 5 a 7 minutos con capitulos (metadatos de capitulo en el MP4 y titulo en pantalla): perfil y e.firma; nueva solicitud; seguimiento y estados; paquetes; explorador y PDF; menu bar y notificaciones. | Aprobada por usuario | Ronda 2. |
| D8 | Formato: 1920x1080 (ventana logica 960x540 a 2x), 30 fps, H.264 `yuv420p` con `+faststart`, audio AAC 48 kHz, loudness integrado de -16 LUFS, tema claro, `es_MX`, UTC. | Recomendacion tecnica | `qt-platform-engineer`. |
| D9 | `OPENAI_API_KEY` se lee de una variable de entorno solo en el paso de generacion de voz. Nunca aparece en el repo, CMake, logs, manifiesto ni salida de error. | Aprobada por usuario | Ronda 2. Evita el Keychain en una herramienta que debe evitarlo. |
| D10 | Ejecutable propio `tools/video-tutorial` con la opcion `SATCFDI_BUILD_VIDEO_TUTORIAL` (OFF por omision). No enlaza `satcfdi_sat`, `satcfdi_secrets_macos` ni `satcfdi_os_macos`, ni usa factories productivas. Selector de archivos, Finder, menu bar y notificaciones se muestran con ilustraciones QML marcadas como demostracion. Puede reutilizar codigo de `tools/capturas-manual` sin cambiar su salida. | Recomendacion tecnica | `qt-architecture-lead` (ronda 02). La app productiva no cambia. |
| D11 | Los roles especialistas que pidio la reunion (guionista/disenador instruccional, ingeniero de audio y voz sintetica, motion design opcional) se crean antes en T018 y participan en el ciclo de desarrollo. | Aprobada por usuario | Ronda 2. |
| D12 | El paso que llama a OpenAI lo ejecuta el usuario, o el coordinador con autorizacion explicita en ese momento: usa la cuenta y el costo del usuario. | Recomendacion tecnica | Accion externa con costo. |
| D13 | Toda linea del guion con `texto` tiene clip de voz y genera exactamente un cue del `.srt` (el campo `voz` solo cambia la pronunciacion, no si hay clip). Inicio y duracion de cada linea y escena se expresan en cuadros enteros a 30 fps. Cada escena declara un presupuesto de cuadros (minimo y maximo); una validacion previa a grabar falla si alguna escena o la duracion total (5 a 7 min) queda fuera. | Recomendacion tecnica | `qt-quality-engineer` (ronda 05). Elimina la ambiguedad voz/subtitulos y hace verificable la duracion. |
| D14 | Las escenas de menu bar, selector de archivos, Finder y notificaciones usan ilustraciones con el rotulo visible "Ilustracion"; el guion no afirma que sean capturas reales del sistema. | Recomendacion tecnica | `qt-quality-engineer` (ronda 05), compatible con D7 y D10. |
| D15 | La grabacion acepta `--offline`: cualquier intento de red falla de inmediato (sin cliente HTTP enlazado en el paso de grabacion). La repeticion de verificacion se hace ademas con la red del equipo desconectada. | Recomendacion tecnica | `qt-quality-engineer` (ronda 05). |
| D16 | Un manifiesto de verificacion versionado fija: indices de cuadro muestreados por capitulo, PSNR minimo de 60 dB por cuadro muestreado, decodificacion de audio para el MD5 (`-ar 48000 -ac 2 -f s16le`, o el formato que fije la implementacion y quede en el manifiesto) y la lista esperada de capitulos (titulo, inicio y fin en cuadros) contra la salida de `ffprobe -show_chapters`. Un script lo comprueba y termina con codigo distinto de 0 si algo no coincide. | Recomendacion tecnica | `qt-quality-engineer` (ronda 05). Da un oraculo ejecutable a la reproducibilidad. |
| D17 | La prueba grafica de la herramienta en `ctest` se marca SKIP sin sesion grafica; un SKIP no cuenta como aprobacion y la evidencia grafica se registra aparte. | Recomendacion tecnica | `qt-quality-engineer` (ronda 05). |

## Alcance

### Incluye

- Herramienta `tools/video-tutorial`: composition root de demo, servicios fijos mutables, ZIP sintetico, cursor simulado, overlay de titulos y controlador de tiempo por cuadro.
- Guion versionado unico (por ejemplo JSON) con escenas, acciones, `texto` (pantalla y `.srt`) y `voz` opcional para la pronunciacion (RFC, "CFDI", montos).
- Paso de voz: genera los clips faltantes con OpenAI, mide su duracion y escribe un manifiesto (hash, modelo, voz, duracion). La duracion de cada escena es la mayor entre sus acciones y su voz mas un colchon.
- Paso de grabacion, sin red: cuadros a ffmpeg, mezcla de clips por cuadro, normalizacion de loudness, capitulos y `.srt`.
- Targets CMake documentados en `docs/development.md` (comando de voz y comando de video), salida en `build/video/` y `.gitignore` para MP4, cuadros y cache.
- Revision del guion por el guionista, de la voz por el especialista de audio y aprobacion del corte final por el usuario.

### No incluye

- Cambios de comportamiento o del binario de `satcfdi_app`.
- Publicar el video, versionarlo en git o enlazarlo desde el manual.
- Grabaciones con la app real, e.firma real o el SAT.
- Ejecucion en CI: requiere una sesion grafica.
- Version en ingles u otros idiomas.

## Dependencias

- T018 (roles especialistas).
- T012 y T016 (manual vigente y herramienta de capturas como base de fixtures e ilustraciones).
- T015.1 a T015.4 (lector ZIP, parser, `RenderizadorPdfCfdi` y visor).
- Del usuario: cuenta de OpenAI con acceso a la API de audio, `OPENAI_API_KEY` exportada para el paso de voz y una sesion grafica con pantalla Retina (sin Retina se reescala).

## Trabajo esperado

- Incremento sugerido: (1) herramienta, guion y video sin voz; (2) paso de voz, mezcla, capitulos y `.srt`. El ciclo de desarrollo puede dividirlo en subtareas.
- `objectName` adicionales en QML solo si hacen falta para que el guion localice controles, sin cambiar el comportamiento visible (confirmar con `qt-interface-engineer`).
- Prueba automatizada de enlace/ejecucion de la herramienta en modo rapido (pocos cuadros, sin voz) dentro de `ctest`, cuando haya sesion grafica, o como verificacion manual documentada si no la hay.

## Criterios de aceptacion

- [ ] Dado el cache de voz completo, cuando se ejecuta el target de video con `--offline`, entonces termina con codigo 0 y deja en `build/video/` un MP4 y un `.srt`. La prueba se repite con la red del equipo desconectada con el mismo resultado.
- [ ] `ffprobe` del MP4 muestra video h264 1920x1080 a 30/1 y audio aac a 48 kHz, ambas pistas con la misma duracion ±0.1 s, una duracion total de 5 a 7 minutos, y `ffprobe -show_chapters` coincide con la lista de capitulos del manifiesto de verificacion (D16).
- [ ] Cada linea del guion con `texto` tiene exactamente un cue en el `.srt` con ese texto y un clip de voz, y el cue empieza en el mismo cuadro que su clip (±1 cuadro).
- [ ] La validacion previa falla, antes de grabar y con mensaje que nombra la escena, si una escena excede su presupuesto de cuadros o la duracion total sale de 5 a 7 min.
- [ ] El script del manifiesto de verificacion (D16) pasa sobre dos grabaciones seguidas con el mismo cache: mismo numero de cuadros, mismo MD5 del audio decodificado y PSNR de al menos 60 dB en cada cuadro muestreado.
- [ ] Loudness integrado entre -17 y -15 LUFS (`ebur128`).
- [ ] Si falta un clip en el cache, la grabacion falla con un mensaje que nombra la escena y no llama a OpenAI. Si falta `OPENAI_API_KEY` o la API responde con error a mitad del lote, el paso de voz falla con un mensaje claro sin imprimir la clave, el cache queda igual que antes y no hay clips nuevos visibles.
- [ ] Con un cache completo, el paso de voz no hace llamadas; si cambia el texto de una linea, solo se sintetiza esa linea.
- [ ] El target de la herramienta no enlaza `satcfdi_sat`, `satcfdi_secrets_macos` ni `satcfdi_os_macos` (comprobado con `otool -L` o el grafo de CMake). Una corrida no crea ni modifica archivos fuera de su directorio temporal y de `build/video/`; en particular, deja intactos la base, los paquetes y el Keychain del usuario.
- [ ] El explorador y el PDF del video salen del lector ZIP, el parser y el renderer reales sobre el ZIP sintetico. La escena del PDF empieza despues de `documentoListo`. Con un PDF forzado a fallar o a no responder en el modo rapido de prueba, la grabacion termina con error por `documentoError` o por timeout, respectivamente.
- [ ] Las escenas de menu bar, selector, Finder y notificaciones muestran el rotulo "Ilustracion" (revision de un cuadro por escena).
- [ ] Solo datos sinteticos: un escaneo del guion, del `.srt` y de los fixtures contra una lista de RFC, nombres y rutas permitidos pasa. La revision visual de un cuadro por escena no muestra datos reales.
- [ ] `git status` tras generar el video no muestra MP4, cuadros, clips ni manifiesto. La clave no aparece en ningun archivo generado (grep).
- [ ] `satcfdi_app`, `satcfdi_manual_capturas` y sus salidas no cambian (las capturas del manual se regeneran identicas o solo con diferencias justificadas). `ctest` pasa completo (la prueba grafica en SKIP sin sesion grafica no cuenta como aprobacion; D17) y la herramienta compila con `-Werror`.
- [ ] Los capitulos y textos coinciden con la app y con `docs/manual/usuario.md` vigentes (revision del guionista y del coordinador).
- [ ] El usuario aprueba una muestra de voz antes de sintetizar el guion completo y aprueba el corte final.
- [ ] `docs/development.md` documenta los dos comandos, la variable de entorno, el cache y los requisitos (sesion grafica, Retina).

## Verificacion

1. Build `-Werror` con `SATCFDI_BUILD_VIDEO_TUTORIAL=ON` y `ctest` completo.
2. Paso de voz ejecutado por el usuario (D12), primero con una muestra y luego con el guion completo; segunda corrida sin llamadas (log del paso).
3. Dos grabaciones con `--offline` y la red desconectada; script del manifiesto de verificacion (capitulos, cuadros, MD5 de audio, PSNR) y `ebur128`.
4. `otool -L` sobre la herramienta, escaneo de datos sinteticos, grep de la clave y `git status`.
5. Revision del guion (guionista), de la voz y la mezcla (audio), de los textos frente a la app (coordinador y Codex) y del corte final (usuario).

## Resultado

Pendiente.

## Riesgos

- Cambios en la UI o en los `objectName` desalinean el guion: se detectan porque la grabacion falla si no encuentra un control.
- Cambios de modelo o voz de OpenAI alteran el audio: el cache y el manifiesto fijan la version usada; regenerar es una decision explicita.
- Componentes nativos (FileDialog, menu bar, notificaciones) no se capturan: se usan ilustraciones.
- El tema oscuro del sistema puede filtrarse a los controles: forzar el tema claro de la app, como en las capturas.

## Referencias

- `docs/meetings/T017-refinamiento/` (bitacora, rondas 02 a 04, prueba de concepto en el scratchpad de la sesion).
- `docs/manual/usuario.md`, `docs/development.md` (capturas T012), `tools/capturas-manual`.
- `docs/tasks/T012-manual-usuario.md`, `docs/tasks/T016-manual-t014-3-t014-4.md`, `docs/tasks/T015-explorador-cfdi.md`, `docs/tasks/T018-roles-produccion-video.md`.
