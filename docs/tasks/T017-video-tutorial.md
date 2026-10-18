# T017: Video tutorial de la aplicacion

## Estado

Completada (2026-10-17), con deuda registrada en T021 y T022.

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
| D7 | Un solo MP4 de 5 a 7:30 minutos (tope ampliado por D20) con capitulos (metadatos de capitulo en el MP4 y titulo en pantalla): perfil y e.firma; nueva solicitud; seguimiento y estados; paquetes; explorador y PDF; menu bar y notificaciones. | Aprobada por usuario | Ronda 2. |
| D8 | Formato: 2560x1440 (D21; antes 1920x1080), ventana logica 960x540 a 2x compuesta a escala 1, 30 fps, H.264 `yuv420p` con `+faststart`, audio AAC 48 kHz, loudness integrado de -16 LUFS, tema claro, `es_MX`, UTC. | Recomendacion tecnica | `qt-platform-engineer`. |
| D9 | `OPENAI_API_KEY` se lee de una variable de entorno solo en el paso de generacion de voz. Nunca aparece en el repo, CMake, logs, manifiesto ni salida de error. | Aprobada por usuario | Ronda 2. Evita el Keychain en una herramienta que debe evitarlo. |
| D10 | Ejecutable propio `tools/video-tutorial` con la opcion `SATCFDI_BUILD_VIDEO_TUTORIAL` (OFF por omision). No enlaza `satcfdi_sat`, `satcfdi_secrets_macos` ni `satcfdi_os_macos`, ni usa factories productivas. Selector de archivos, Finder, menu bar y notificaciones se muestran con ilustraciones QML marcadas como demostracion. Puede reutilizar codigo de `tools/capturas-manual` sin cambiar su salida. | Recomendacion tecnica | `qt-architecture-lead` (ronda 02). La app productiva no cambia. |
| D11 | Los roles especialistas que pidio la reunion (guionista/disenador instruccional, ingeniero de audio y voz sintetica, motion design opcional) se crean antes en T018 y participan en el ciclo de desarrollo. | Aprobada por usuario | Ronda 2. |
| D12 | El paso que llama a OpenAI lo ejecuta el usuario, o el coordinador con autorizacion explicita en ese momento: usa la cuenta y el costo del usuario. | Recomendacion tecnica | Accion externa con costo. |
| D13 | Toda linea del guion con `texto` genera exactamente un cue del `.srt` y referencia un intervalo de un bloque de narracion (D19); un bloque es un clip continuo que agrupa las lineas de una escena o de un tramo de ella (el campo `voz` solo cambia la pronunciacion). Inicio y duracion de cada linea y escena se expresan en cuadros enteros a 30 fps. Cada escena declara un presupuesto de cuadros (minimo y maximo); una validacion previa a grabar falla si alguna escena o la duracion total (5 a 7:30 min) queda fuera. | Recomendacion tecnica | `qt-quality-engineer` (ronda 05). Elimina la ambiguedad voz/subtitulos y hace verificable la duracion. |
| D14 | Las escenas de menu bar, selector de archivos, Finder y notificaciones usan ilustraciones con el rotulo visible "Ilustracion"; el guion no afirma que sean capturas reales del sistema. | Recomendacion tecnica | `qt-quality-engineer` (ronda 05), compatible con D7 y D10. |
| D15 | La grabacion acepta `--offline`: cualquier intento de red falla de inmediato (sin cliente HTTP enlazado en el paso de grabacion). La repeticion de verificacion se hace ademas con la red del equipo desconectada. | Recomendacion tecnica | `qt-quality-engineer` (ronda 05). |
| D16 | Un manifiesto de verificacion versionado fija: indices de cuadro muestreados por capitulo, PSNR minimo de 60 dB por cuadro muestreado, decodificacion de audio para el MD5 (`-ar 48000 -ac 2 -f s16le`, o el formato que fije la implementacion y quede en el manifiesto) y la lista esperada de capitulos (titulo, inicio y fin en cuadros) contra la salida de `ffprobe -show_chapters`. Un script lo comprueba y termina con codigo distinto de 0 si algo no coincide. | Recomendacion tecnica | `qt-quality-engineer` (ronda 05). Da un oraculo ejecutable a la reproducibilidad. |
| D17 | La prueba grafica de la herramienta en `ctest` se marca SKIP sin sesion grafica; un SKIP no cuenta como aprobacion y la evidencia grafica se registra aparte. | Recomendacion tecnica | `qt-quality-engineer` (ronda 05). |
| D18 | Narracion fluida: un parrafo hablado por escena con conectores y frases completas, siglas deletreadas solo la primera vez por capitulo e introduccion de 28 a 30 s (que es Archivo Fiscal, para quien, que todo queda en el equipo y el recorrido del video). La app se nombra "Archivo Fiscal" en el guion; el renombrado de la app es otra tarea. | Aprobada por usuario (2026-10-16) | Revision del corte final; `respuestas/04-guion-refinamiento.md`. |
| D19 | Voz por bloques: un clip continuo por escena (o por tramo, si hay una espera operativa larga), sin estado entre llamadas. Marcas de inicio por cue obtenidas localmente detectando las pausas entre frases y validando cobertura, orden y duracion; si no alcanza, se agrega un alineador local con autorizacion del usuario. Caches separados de sintesis, alineacion y montaje; cambiar pausas o alineacion no llama a TTS. Se mantienen `gpt-4o-mini-tts` y `marin` con instrucciones de energia constante; pausas de 250-400 ms entre frases, 500-800 ms entre escenas y 800-1200 ms entre capitulos; normalizacion por pista a -16 LUFS / -2 dBTP. | Aprobada por usuario (2026-10-16) | `respuestas/04-audio-refinamiento.md`. Reduce cambios de timbre y uniones entrecortadas. |
| D20 | Tope de duracion ampliado a 7:30 (13500 cuadros). | Aprobada por usuario (2026-10-16) | La intro ampliada y la narracion fluida llevan el total a ~7:02. |
| D21 | Composicion: lienzo 2560x1440 con la ventana de la app centrada a escala 1, esquinas redondeadas, sombra y barra de titulo simulada sin texto; transcripcion quemada abajo (cue en curso y anterior atenuado) desde la misma linea de tiempo del `.srt`, que se conserva; titulo de capitulo en el margen superior; ilustraciones dentro de la ventana. | Aprobada por usuario (2026-10-16) | `respuestas/04-motion-composicion.md`. |
| D22 | Fondo generativo determinista: fBm con domain warping en `ShaderEffect` precompilado (`.qsb`), tiempo derivado del contador de cuadros, semilla fija, movimiento lento y vineta; paleta Santos Laguna 2008: `#052E1C`, `#0A5F38`, `#15924F`, casi negro `#0E1F18` y acento dorado `#C9A227` (<= 5 %). | Aprobada por usuario (2026-10-16) | `respuestas/04-motion-composicion.md`; paleta propuesta por motion design y aprobada por el usuario. |
| D23 | El refinamiento se hace dentro de T017, antes del corte 4. | Aprobada por usuario (2026-10-16) | El video aun no esta cerrado. |
| D24 | Voz final: `gpt-4o-mini-tts` con voz `onyx` e instrucciones de locutor sobrio sin acento regional (reemplaza a `marin` de D19); montaje con tempo 1.15 y pausa maxima de 180 ms; alineacion por frase con respaldo a la pausa mas cercana y correcciones manuales en `alineacion/correcciones.json`. | Aprobada por usuario (2026-10-16) | Escucha de las variantes de voz y de montaje (`respuestas/05` a `07-audio-*.md`; variante C). |
| D25 | Musica de fondo generada localmente por la herramienta (preset `pad-fiscal-v1`: pad aditivo sin percusion, EQ que libera 1-4 kHz), determinista y sin red, activable con la raiz `musica` del guion. Ducking determinista a partir de los intervalos de voz; la voz no cambia; la grabacion falla si la musica no queda >= 20 dB por debajo de la voz en el 95 % de las ventanas de 400 ms con voz. Valores del tutorial: cama -35 LUFS y duck -9 dB. Pico verdadero de -2 dBTP con tolerancia de +0.2 dB sobre el AAC decodificado. | Aprobada por usuario (2026-10-17); niveles y tolerancia por recomendacion tecnica | `respuestas/08-audio-musica.md`; el usuario eligio musica generada y pidio subirla un poco tras la muestra. |

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

- [x] Dado el cache de voz completo, cuando se ejecuta el target de video con `--offline`, entonces termina con codigo 0 y deja en `build/video/` un MP4 y un `.srt`. La prueba se repite con la red del equipo desconectada con el mismo resultado. (Evidencia parcial aceptada por el usuario: repeticion en el sandbox `sin-red.sb`, sin desconectar la red fisica.)
- [x] `ffprobe` del MP4 muestra video h264 2560x1440 a 30/1 y audio aac a 48 kHz, ambas pistas con la misma duracion ±0.1 s, una duracion total de 5 a 7:30 minutos, y `ffprobe -show_chapters` coincide con la lista de capitulos del manifiesto de verificacion (D16).
- [x] Cada linea del guion con `texto` tiene exactamente un cue en el `.srt` con ese texto y un intervalo en su bloque de voz; el cue empieza a ±3 cuadros de la marca alineada de su frase, y la transcripcion quemada muestra en cada cuadro el cue vigente.
- [x] La validacion previa falla, antes de grabar y con mensaje que nombra la escena, si una escena excede su presupuesto de cuadros o la duracion total sale de 5 a 7:30 min.
- [x] El script del manifiesto de verificacion (D16) pasa sobre dos grabaciones seguidas con el mismo cache: mismo numero de cuadros, mismo MD5 del audio decodificado y PSNR de al menos 60 dB en cada cuadro muestreado.
- [x] Loudness integrado entre -17 y -15 LUFS (`ebur128`).
- [x] Musica (D25): sin la raiz `musica` la pista es identica a la voz sola; con ella, la voz antes de sumar no cambia, dos generaciones dan el mismo hash, el margen voz/musica se cumple y el pico verdadero del AAC decodificado no supera -1.8 dBTP.
- [x] Si falta un clip en el cache, la grabacion falla con un mensaje que nombra la escena y no llama a OpenAI. Si falta `OPENAI_API_KEY` o la API responde con error a mitad del lote, el paso de voz falla con un mensaje claro sin imprimir la clave, el cache queda igual que antes y no hay clips nuevos visibles.
- [x] Con un cache completo, el paso de voz no hace llamadas; si cambia el texto de una linea, solo se sintetiza su bloque; cambiar pausas o alineacion no llama a TTS.
- [x] El target de la herramienta no enlaza `satcfdi_sat`, `satcfdi_secrets_macos` ni `satcfdi_os_macos` (comprobado con `otool -L` o el grafo de CMake). Una corrida no crea ni modifica archivos fuera de su directorio temporal y de `build/video/`; en particular, deja intactos la base, los paquetes y el Keychain del usuario.
- [x] El explorador y el PDF del video salen del lector ZIP, el parser y el renderer reales sobre el ZIP sintetico. La escena del PDF empieza despues de `documentoListo`. Con un PDF forzado a fallar o a no responder en el modo rapido de prueba, la grabacion termina con error por `documentoError` o por timeout, respectivamente.
- [x] Las escenas de menu bar, selector, Finder y notificaciones muestran el rotulo "Ilustracion" (revision de un cuadro por escena).
- [x] Composicion (D21, D22): en un cuadro por capitulo la ventana esta centrada a escala 1 sobre el fondo generativo, y ni la transcripcion ni el titulo tapan la ventana; dos grabaciones con la misma semilla dan el mismo fondo (PSNR de D16).
- [x] Solo datos sinteticos: un escaneo del guion, del `.srt` y de los fixtures contra una lista de RFC, nombres y rutas permitidos pasa. La revision visual de un cuadro por escena no muestra datos reales.
- [x] `git status` tras generar el video no muestra MP4, cuadros, clips ni manifiesto. La clave no aparece en ningun archivo generado (grep).
- [x] `satcfdi_app`, `satcfdi_manual_capturas` y sus salidas no cambian (las capturas del manual se regeneran identicas o solo con diferencias justificadas). `ctest` pasa completo (la prueba grafica en SKIP sin sesion grafica no cuenta como aprobacion; D17) y la herramienta compila con `-Werror`. (Dos corridas completas al 100 %; intermitencia de ~5 % en pruebas graficas `--rapido` registrada en T022. Capturas 04, 05 y 13 desactualizadas desde antes de T017: T021.)
- [x] Los capitulos y textos coinciden con la app y con `docs/manual/usuario.md` vigentes (revision del guionista y del coordinador). (Coinciden con la app; el manual esta desactualizado respecto a la app: T021.)
- [x] El usuario aprueba una muestra de voz antes de sintetizar el guion completo y aprueba el corte final.
- [x] `docs/development.md` documenta los dos comandos, la variable de entorno, el cache y los requisitos (sesion grafica, Retina).

## Verificacion

1. Build `-Werror` con `SATCFDI_BUILD_VIDEO_TUTORIAL=ON` y `ctest` completo.
2. Paso de voz ejecutado por el usuario (D12), primero con una muestra y luego con el guion completo; segunda corrida sin llamadas (log del paso).
3. Dos grabaciones con `--offline` y la red desconectada; script del manifiesto de verificacion (capitulos, cuadros, MD5 de audio, PSNR) y `ebur128`.
4. `otool -L` sobre la herramienta, escaneo de datos sinteticos, grep de la clave y `git status`.
5. Revision del guion (guionista), de la voz y la mezcla (audio), de los textos frente a la app (coordinador y Codex) y del corte final (usuario).

## Resultado

- Herramienta `tools/video-tutorial` (opcion `SATCFDI_BUILD_VIDEO_TUTORIAL`): grabador sin red que maneja la app real con datos sinteticos, paso de voz por bloques con cache de sintesis, montaje y alineacion, composicion 2560x1440 con fondo generativo y transcripcion quemada, musica generada con ducking y verificacion D16 (`video_tutorial_verificar`).
- Video final aprobado por el usuario: 5:53, voz `onyx` (D24), musica `pad-fiscal-v1` (D25), -16.03 LUFS / -1.89 dBTP; D16 OK sobre dos grabaciones.
- `ctest` 49 pruebas al 100 % en dos corridas seguidas (1 SKIP esperado). Revision de Codex en `docs/meetings/T017-desarrollo/respuestas/09-calidad-cierre.md`; evidencia en `docs/meetings/T017-desarrollo/evidencia.md`.
- Deuda: intermitencia de pruebas graficas en modo rapido (T022); manual y capturas desactualizados (T021); red fisica desconectada como evidencia parcial; renombrado (T019) obligara a regrabar.

## Riesgos

- Cambios en la UI o en los `objectName` desalinean el guion: se detectan porque la grabacion falla si no encuentra un control.
- Cambios de modelo o voz de OpenAI alteran el audio: el cache y el manifiesto fijan la version usada; regenerar es una decision explicita.
- Componentes nativos (FileDialog, menu bar, notificaciones) no se capturan: se usan ilustraciones.
- El tema oscuro del sistema puede filtrarse a los controles: forzar el tema claro de la app, como en las capturas.

## Referencias

- `docs/meetings/T017-refinamiento/` (bitacora, rondas 02 a 04, prueba de concepto en el scratchpad de la sesion).
- `docs/manual/usuario.md`, `docs/development.md` (capturas T012), `tools/capturas-manual`.
- `docs/tasks/T012-manual-usuario.md`, `docs/tasks/T016-manual-t014-3-t014-4.md`, `docs/tasks/T015-explorador-cfdi.md`, `docs/tasks/T018-roles-produccion-video.md`.
