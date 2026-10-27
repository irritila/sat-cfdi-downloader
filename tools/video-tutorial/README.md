# tools/video-tutorial (T017)

Graba el video tutorial cuadro a cuadro con la UI real (`SatCfdiDownloader`)
sobre servicios fijos gobernados por un guion JSON versionado. Sin SQLite,
worker, SAT, Keychain ni red. Corte 1: video sin voz (MP4 + `.srt` + capitulos).

> Estado (T024.4, corte 2): `guion-tutorial.json` es el tutorial nuevo (7
> capitulos, portada y cierre, voz `cedar`). Graba completo sin voz y con
> `--rapido`; falta la voz real (la sintetiza el usuario: muestra y lote).

## Uso

```bash
cmake -S . -B build -G Ninja -DSATCFDI_BUILD_VIDEO_TUTORIAL=ON
cmake --build build --target video_tutorial      # guion-tutorial.json -> build/video/tutorial.{mp4,srt,linea-tiempo.json}
ctest --test-dir build -L video                  # validacion, sandbox, sincronia, modo rapido, barrera PDF

# Directo (la grabacion siempre dentro del sandbox sin red, D15):
build/tools/video-tutorial/satcfdi_video_tutorial --guion tools/video-tutorial/guion-ejemplo.json --validar
sandbox-exec -f tools/video-tutorial/sin-red.sb build/tools/video-tutorial/satcfdi_video_tutorial \
    --guion <guion.json> --salida build/video [--nombre tutorial] [--rapido] --offline \
    [--forzar-pdf fallo|sin-respuesta]
```

- Requisitos: ffmpeg/ffprobe (`brew install ffmpeg`, se buscan al configurar),
  sesion grafica de macOS con pantalla Retina (sin Retina se reescala con aviso).
  No tocar el raton ni cambiar de app durante la grabacion (la entrada real se
  descarta, pero el foco de macOS puede cambiar el aspecto de la ventana).
- Codigos: 0 ok; 1 error (con la escena); 2 uso; 77 sin sesion grafica
  (`SKIP_RETURN_CODE` en ctest: SKIP, que no cuenta como aprobacion, D17).
  `SATCFDI_VIDEO_SIN_SESION=1` simula la falta de sesion.
- `--salida` debe quedar dentro del directorio de build (`CMAKE_BINARY_DIR`,
  fijado al compilar); fuera de el la herramienta se niega.
- `--rapido`: duraciones al 10 % (minimo 1 cuadro), sin validar presupuestos,
  x264 `ultrafast`. Solo para pruebas.
- `--forzar-pdf`: solo pruebas de la barrera (D6): `fallo` provoca
  `documentoError`; `sin-respuesta` deja el PDF sin terminar (timeout).
- `SATCFDI_VIDEO_DEPURAR_CUADROS=1`: diagnostico; guarda cada cuadro en PNG en
  `<salida>/<nombre>.cuadros/` (nunca fuera de la salida validada); con
  `SATCFDI_VIDEO_DEPURAR_RANGO=desde-hasta` solo esos cuadros.
- `SATCFDI_VIDEO_DIAGNOSTICO_CLIC=1` (T022): diagnostico de clics, apagado
  por omision. Escribe `<salida>/<nombre>.diag.log` con, en cada `mover` y
  alrededor del press y del release de cada `clic`: cuadro, cursor, objetivo
  (rect en escena, `visible`/`enabled`/`opacity`, `pressed`/`down`/`hovered`),
  cadena de items bajo el cursor ordenada por `z`, `mouseGrabberItem`,
  `activeFocusItem`, ventana activa, estado minimo (pagina, modo de perfiles,
  `hayFormulario`) y los eventos que reciben la ventana y el objetivo, con
  sello de tiempo. Sin RFC ni contrasenas. Solo observa (no bombea ni
  renderiza): no cambia los cuadros.
- `SATCFDI_VIDEO_DIAGNOSTICO_DESACTIVAR=N` y
  `SATCFDI_VIDEO_DIAGNOSTICO_PERMITIR_DESACTIVAR=1`: solo diagnostico y
  pruebas (T022). Tras el press del N-esimo clic con efecto, la ventana
  pierde el foco de Qt como cuando otra app toma el foco de macOS; la segunda
  deja pasar ese `WindowDeactivate` (ver "Clic perdido por cambio de app").
- `--sonda-red`: intenta una conexion TCP (saliente y local) y dice
  `RED DISPONIBLE` o `RED BLOQUEADA: ...`; sirve para probar el sandbox.
- Targets: `video_tutorial` (con voz), `video_tutorial_sin_voz`,
  `video_tutorial_voz` y `video_tutorial_voz_estimar` (ver "Voz").

## Voz por bloques (D5, D9, D12, D13, D19)

Dos ejecutables: `satcfdi_video_voz` (paso de voz, el UNICO con cliente HTTP)
y `satcfdi_video_tutorial` (grabacion, sin red, dentro del sandbox). La
grabacion solo lee el cache de voz; si falta un bloque, falla nombrando
escena y bloque y nunca llama a OpenAI.

### Contrato del guion (fijado por el coordinador)

- Bloque de narracion = un clip continuo. Por omision, cada escena es un
  bloque. Una linea con `"nuevoBloque": true` abre un bloque nuevo dentro de
  su escena (para esperas operativas largas).
- Texto que se sintetiza por bloque: el texto hablado efectivo de sus lineas
  (`voz` si existe, si no `texto`), unido con un espacio. Cada linea termina
  en puntuacion de fin de frase (`.` `?` `!` `:`), y la validacion lo exige.
- Cada linea conserva `id`, `texto` (= cue) y `voz` opcional. `esperarLinea`
  se resuelve contra la marca alineada del cue. `separacionLineas` no aplica
  dentro de un bloque.
- Si una accion necesita mas tiempo que la voz, la pausa se inserta solo en
  una frontera entre lineas, desplazando lo posterior; nunca se corta la voz.
  Las pausas entre bloques, escenas y capitulos siguen D19 (250-400, 500-800
  y 800-1200 ms, contando el silencio existente).

Como lo aplica la herramienta:

- Ids de bloque: `capitulo/escena` (el primero) y `capitulo/escena/<linea>`
  (los que abre `nuevoBloque`). Ids de linea para correcciones:
  `capitulo/escena/linea`.
- Plan (cuadros a 30 fps, audio en muestras a 48 kHz): cada bloque empieza en
  un cuadro entero; sus lineas empiezan en `inicio del bloque + marca`. El cue
  de una linea dura hasta el inicio del cue siguiente o el fin de su voz.
- Pausas minimas (raiz `"pausas"`, ms): `bloque` 300 (250-400), `escena` 600
  (500-800) y `capitulo` 1000 (800-1200). Se miden de fin de voz a inicio de
  voz, contando los 150 ms + 65 ms de silencio que deja el recorte. Si las
  acciones (o el `colchon`, o el `inicio` de una linea) dan mas tiempo, la
  pausa es mayor; nunca menor.
- `inicio` de una linea: cuadro minimo de su cue; si la voz llegaria antes, se
  inserta la pausa en la frontera previa (o se retrasa el bloque).
- `esperarLinea` con `momento: inicio`: si las acciones llegan tarde, la linea
  (y lo que sigue) se desplaza. Con `momento: fin`: si llegan tarde, se
  desplaza la linea siguiente. Cambiar pausas o acciones no llama a TTS.
- Escena: dura `max(fin de acciones, fin de voz) + colchon`; presupuestos por
  escena y total (D7/D20: 5 a 7:30 min, 9000 a 13500 cuadros).
- `--validar --detalle [--voz <cache>]` imprime escena, bloque, cue e inicio
  exacto del audio de cada linea.

### Flujo para el usuario (D12: solo tu ejecutas las llamadas con costo)

```bash
cd <raiz-del-repositorio>
cmake -S . -B build -G Ninja -DSATCFDI_BUILD_VIDEO_TUTORIAL=ON
cmake --build build --target satcfdi_video_voz satcfdi_video_tutorial

# 1. Estimar: bloques por sintetizar, caracteres y costo. Sin llamadas ni clave.
cmake --build build --target video_tutorial_voz_estimar

# 2. Muestra: solo los bloques de esas escenas (pide escribir "si").
export OPENAI_API_KEY=...        # solo en tu terminal; nunca en archivos
build/tools/video-tutorial/satcfdi_video_voz \
    --guion tools/video-tutorial/guion-tutorial.json --cache build/video/tts-cache \
    --muestra bienvenida,perfil-efirma/crear-perfil
unset OPENAI_API_KEY

# 3. Escuchar la muestra montada, en contexto (con subtitulos, sin red):
sandbox-exec -f tools/video-tutorial/sin-red.sb build/tools/video-tutorial/satcfdi_video_tutorial \
    --guion tools/video-tutorial/guion-tutorial.json --salida build/video --nombre muestra \
    --offline --voz build/video/tts-cache --escenas bienvenida,perfil-efirma/crear-perfil
open build/video/muestra.mp4      # build/video/muestra.srt a un lado
#    (o los WAV originales: el paso 2 imprime la ruta de cada bloque)

# 4. Si una marca de subtitulo cae mal: corregirla sin resintetizar.
#    build/video/tts-cache/alineacion/correcciones.json:
#    {"perfil-efirma/crear-perfil/l2": 6.40}   (segundos desde el inicio del bloque)
build/tools/video-tutorial/satcfdi_video_voz \
    --guion tools/video-tutorial/guion-tutorial.json --cache build/video/tts-cache   # sin llamadas

# 5. Aprobada la muestra: el lote (solo los bloques que faltan).
export OPENAI_API_KEY=...
cmake --build build --target video_tutorial_voz      # pide confirmacion
unset OPENAI_API_KEY

# 6. Validar presupuestos con la voz real y grabar, sin red.
build/tools/video-tutorial/satcfdi_video_tutorial --guion tools/video-tutorial/guion-tutorial.json \
    --validar --detalle --voz build/video/tts-cache
cmake --build build --target video_tutorial
```

Si la muestra no se aprueba, cambia `voz` (raiz o linea) en el guion y repite
el paso 2: solo se sintetiza el bloque que cambio. Con el cache completo, el
paso de voz no hace llamadas ni necesita la clave.

### `satcfdi_video_voz`

```
satcfdi_video_voz --guion <guion.json> [--cache <dir>] [--motor openai|prueba]
                  [--muestra <escena|capitulo/escena|capitulo/escena/linea,...>]
                  [--estimar] [--confirmar] [--pausa-minima-ms 120]
```

- `--muestra`: escena (`bienvenida`), `capitulo/escena` (todos sus bloques) o
  una linea (`escena/linea` o `capitulo/escena/linea`: el bloque que la
  contiene).
- Configuracion: objeto `voz` de la raiz del guion: `modelo`, `voz`,
  `instrucciones`, `formato` (`wav`) y `velocidad` (1.0). Lo que falte toma la
  propuesta de especialista-audio-voz (`gpt-4o-mini-tts`, `marin` y las
  instrucciones de `04-audio-refinamiento.md`: "Español de México. Narrador
  explicativo, cercano y sobrio. …"). Con motor `openai`, valores vacios o
  "pendiente..." se rechazan. Modelo, voz y tarifa estan sin verificar:
  confirmalos en la documentacion oficial antes de la muestra.
- Llamada: `POST https://api.openai.com/v1/audio/speech` con `model`, `voice`,
  `input` (texto del bloque), `instructions`, `response_format` y `speed`. Una
  peticion a la vez.
- `OPENAI_API_KEY` solo se lee del entorno, solo si hay bloques por sintetizar
  y solo se usa en la cabecera Authorization. Nunca se escribe en archivos,
  logs, manifiesto ni mensajes de error.
- Confirmacion: antes de llamar muestra los bloques, caracteres y el costo
  estimado (referencia no verificada USD 0.015/min) y pide escribir "si"; sin
  terminal interactiva exige `--confirmar`.
- Errores seguros: id del bloque, codigo HTTP y categoria; nunca cabeceras ni
  cuerpo. Reintentos: maximo 2, solo para 429 (no cuota) y 5xx, respetando
  `Retry-After` (o 2 s y 4 s). Sin reintento para autenticacion, cuota, 4xx ni
  errores de red o tiempo agotado (una peticion ambigua pudo cobrarse).
- Alineacion fallida: el error nombra escena, bloque y linea; el audio
  sintetizado SI se publica (no se vuelve a pagar) y el bloque queda sin
  alinear hasta corregir sus marcas; la grabacion falla mientras tanto.
- `--motor prueba`: audio sintetico local y determinista (15 caracteres/s,
  350 ms entre frases), sin red ni clave; para ctest. Sus claves llevan
  `"motor": "prueba"` y nunca coinciden con las de OpenAI.
  `SATCFDI_VOZ_PRUEBA_FALLAR_EN=N` simula un fallo en la N-esima llamada;
  `SATCFDI_VOZ_PRUEBA_SIN_PAUSAS=1` une las frases sin pausa (la alineacion
  debe fallar).
- `--cache` debe estar dentro del directorio de build.

### Tempo de montaje (raiz `"montaje": {"tempo": 1.1}`)

- Acelera (o frena) la narracion localmente con ffmpeg `atempo`, que conserva
  el tono. Rango 0.8-1.25; 1.0 por omision. No es el `speed` de la API: no
  cambia la clave de sintesis y nunca llama a TTS.
- Se aplica al audio de cada bloque antes del recorte (asi los 65/150 ms
  conservados y las pausas que mide la alineacion ya son las del audio
  montado), y antes de medir el loudness y de alinear.
- Entra en la clave de montaje y en la de alineacion (y por ello en su
  firma); con 1.0 las claves no cambian y los caches previos siguen validos.
  Cambiar el tempo rehace montaje y alineacion de cada bloque, sin TTS.
- Correcciones manuales: se aplican TAL CUAL, en segundos del audio montado
  con el tempo vigente (lo que imprime "alineado ..." y lo que se escucha).
  No se escalan: si cambias el tempo, revisalas (una correccion fuera de
  lugar falla la validacion o se ve desfasada).

### Pausa maxima (raiz `"montaje": {"pausaMaxima": 200}`, R1d)

- Opcional, en ms (100-1000). Sin el campo no cambia nada (ni claves ni audio).
- Despues de `atempo` y del recorte, cada silencio interno del bloque (bajo
  -45 dB, el mismo umbral de la alineacion) mas largo que `pausaMaxima` se
  acorta a `pausaMaxima`: se conserva la mitad a cada lado y se quita el
  centro, con fundidos de 5 ms. Los extremos del bloque (65/150 ms) no se tocan.
- El loudness y la duracion se miden sobre el audio comprimido.
- Pausas iguales tras comprimir ya no distinguen frases: la alineacion detecta
  las pausas sobre el audio sin comprimir (`montaje/<clave>.sin-comprimir.s16`,
  solo si algo se acorto) y pasa cada marca al audio montado con el mapa de
  cortes (`mapa`: pares inicio/muestras quitadas) guardado en el montaje.
  Las "pausas candidatas" del manifiesto quedan en segundos sin comprimir.
- Entra en la clave de montaje y en la de alineacion; cambiarla rehace ambas,
  sin TTS. Las correcciones manuales son segundos del audio montado (ya
  comprimido): revisalas si cambias `pausaMaxima`.
- `satcfdi_video_voz` imprime la pausa maxima y, por bloque, cuantas pausas
  acorto ("N acortadas"); `--validar --detalle` tambien.

### Cache: tres capas (por omision `build/video/tts-cache/`, ignorado por git)

- Sintesis: clave = SHA-256 del JSON canonico con el texto del bloque, modelo,
  voz, instrucciones, formato y velocidad. Guarda el WAV original
  (`sintesis/<clave>.wav`). Bloques con el mismo texto comparten clip.
- Montaje: clave = SHA-256 del audio + version del recorte + tempo (si no es
  1.0) + pausaMaxima (si existe). Guarda el bloque montado (`montaje/<clave>.s16`, PCM estereo 48 kHz),
  su tempo y su loudness.
- Alineacion: clave = sintesis + texto de cada linea + tempo (si no es 1.0) +
  pausaMaxima (si existe). Guarda marcas (muestras
  y segundos), pausas candidatas, metodo y version, correcciones usadas y una
  firma; se recalcula solo si cambia la firma (audio, lineas, metodo o
  correcciones).
- Correcciones manuales: `alineacion/correcciones.json` (fuera de las
  generaciones; lo editas tu): `{"capitulo/escena/linea": segundos}` desde el
  inicio del bloque recortado. Reemplazan la marca calculada y se validan.
- Pausas, ganancias y posiciones (montaje final) se calculan al grabar: no
  tocan el cache ni TTS.
- `actual.json` (puntero unico) -> `generaciones/<id>/` con `manifiesto.json`
  (esquema 2) y los archivos. Cada corrida toma un bloqueo exclusivo
  (`.bloqueo`), arma la generacion nueva en `.preparando-<id>/` (lo existente
  por enlace duro), la mueve a `generaciones/` y reemplaza `actual.json` de
  forma atomica. Ante un fallo de sintesis borra el staging y la generacion
  anterior queda intacta. Se conservan la actual y la anterior. Un cache por
  linea (esquema 1) se ignora: no se migra.

### Procesado, alineacion y mezcla

- Montaje por bloque: recorte de extremos (-50 dBFS; conserva 65 ms antes y
  150 ms despues; pausas internas intactas), fundidos de 5 ms, estereo 48 kHz.
- Alineacion por indice de frase (local, sin dependencias nuevas; version 3):
  - Frases de cada linea (texto hablado efectivo): corta en `.` `?` `!` y
    en puntos suspensivos (`...` o `…`, un solo corte) cuando sigue espacio o
    el fin; en `:` solo si sigue una mayuscula o es el final. `¿...?` y
    `¡...!` son una frase que termina en `?` o `!`. Un punto sin espacio
    despues (`e.firma`, `1.5`) no corta.
  - Pausas del bloque: `silencedetect` (-45 dB, duracion minima
    `--pausa-minima-ms`, 120 por omision).
  - Exacto ("frases"): si hay al menos frases-1 pausas y las frases-1 mas
    largas se distinguen de la siguiente (>= 20 % y >= 40 ms mas largas), esas
    son las fronteras entre frases y el cue i empieza tras la pausa que
    cierra la ultima frase del cue i-1.
  - Respaldo "respaldo-frases": si no se distinguen, cada frontera entre
    frases va a la pausa mas cercana a su posicion estimada por caracteres
    (en orden). Respaldo "respaldo-proporcional": si hay menos pausas que
    frases-1, solo las fronteras entre lineas, por posicion estimada. Los
    respaldos se avisan en el paso de voz y en `--validar --detalle`.
  - La marca de una linea queda 50 ms antes de su voz. Se valida orden,
    cobertura y cue minimo de 1 s; si falla, error con escena y bloque.
  - Por bloque se reporta cuantas pausas encontro frente a cuantas esperaba
    (frases-1). Cambiar la version del metodo o `--pausa-minima-ms` realinea
    sin TTS.
- Mezcla (al grabar): cada tramo de linea en su posicion (los cortes caen en
  pausas; fundidos de 5 ms), ajuste moderado por bloque (+-3 dB hacia la
  mediana de los bloques) y normalizacion de la pista completa en dos pasadas
  de loudnorm (I=-16 LUFS, TP=-3 dBTP desde T024.4, LRA=11, lineal). AAC-LC estereo 48 kHz
  192 kb/s.
- `--escenas <ids>` en el grabador: ids `capitulo/escena`, o solo `escena`
  si es unica (un id desconocido o ambiguo falla y lista los validos); imprime
  `escenas: ... -> muestra de N cuadros`. Solo esas escenas salen en el MP4, el
  `.srt`, los capitulos y la voz (consecutivas); las demas se ejecutan sin
  grabarse para dejar la app en el mismo estado; las que no tienen voz en el
  cache usan la duracion estimada (no salen en el video). Sin presupuestos.
  Sin `--escenas`, con `--voz`, un bloque sin audio en el cache hace fallar
  la planificacion nombrando escena y bloque (D17).
- Verificacion (`satcfdi_video_con_voz`): pista AAC 48 kHz estereo, misma
  duracion que el video (+-0.1 s), -17 a -15 LUFS, pico <= -2.3 dBTP, cada cue
  justo antes de su voz (+-1 cuadro) y una muestra montada de 2 escenas.

## Musica de fondo (R4, 08-audio-musica.md)

Pad generado localmente bajo la voz. Sin bibliotecas nuevas, sin red, sin
aleatoriedad ni reloj. Raiz del guion (ausente, o `"activa": false`, = sin
musica: la pista sale byte a byte igual que antes):

```json
"musica": {"activa": true, "preset": "pad-fiscal-v1", "nivelLufs": -34, "duckDb": -6,
           "ataqueMs": 150, "retencionMs": 250, "liberacionMs": 1000,
           "entradaMs": 2000, "salidaMs": 3000}
```

- Validacion: solo esos campos; `activa` booleano; `preset` solo
  `pad-fiscal-v1`; `nivelLufs` -45..-30 y `duckDb` -12..-4 (numeros);
  `ataqueMs` 50..300, `retencionMs` 100..500, `liberacionMs` 500..2000,
  `entradaMs`/`salidaMs` 500..5000 (enteros). Los fundidos deben caber en la
  pista que se graba (con `--escenas`, la de esas escenas). Campos omitidos =
  los valores del ejemplo.
- Preset `pad-fiscal-v1` (`Musica.cpp`): sintesis aditiva mono (ambos canales
  iguales), Dmaj9 -> Bm7 -> Gmaj7 -> Asus2 (D3-F#3-A3-E4, B2-D3-F#3-A3,
  G2-B2-D3-F#3, A2-E3-B3-E4), 72 BPM, 8 pulsos por acorde (320.000 muestras;
  ciclo de 26,667 s). Por nota, seno y armonicos 2.o/3.o (1 / 0,18 / 0,04),
  fases fijas. Acordes con medio coseno de 1,5 s solapado; la fase de cada
  oscilador depende solo del tiempo absoluto, asi una nota comun a dos
  acordes sigue continua en el fundido (no hay reinicios de fase con
  envolvente distinta de cero, tampoco en la vuelta del ultimo al primero).
  Fundidos globales `entradaMs`/`salidaMs` solo sobre la musica. Duracion
  exacta: `totalCuadros x 1600` muestras.
- Con `"cierre": true` (T024.4, 01-audio-voz §2) la salida no usa `salidaMs`:
  queda anclada al cierre. Medio coseno desde el cuadro local 45 del cierre
  hasta silencio exacto en el 135; del 135 al 149 la musica es cero y la pista
  conserva su duracion completa. El ancla entra en la clave de cache de la
  mezcla. La portada no lleva voz; la entrada de 2 s (`entradaMs`) cae dentro
  de ella.
- EQ solo de la musica (biquads en C++, continuos en toda la pista):
  pasaaltos 120 Hz 12 dB/oct, campana 250 Hz -4 dB Q 0,8, pasabajos 900 Hz
  24 dB/oct, campana 2 kHz -9 dB Q 0,7.
- Calibracion: la cama ecualizada (antes del ducking) se lleva a `nivelLufs`
  con una ganancia constante (medida con ffmpeg loudnorm, como estereo).
- Ducking determinista con los tramos de voz del montaje (una linea = un
  tramo; se unen los separados por menos de 1,5 s): `duckDb` adicional,
  ataque de `ataqueMs` completado 50 ms antes de la voz, retencion y
  liberacion; tras la ultima frase la atenuacion se mantiene hasta el final.
- Suma en flotante con ganancia unitaria sobre la pista de voz ya normalizada
  (la voz no cambia; no hay loudnorm sobre la suma). Si la mezcla se sale de
  -16 +-0,2 LUFS se baja solo la musica (pasos de 0,5 dB, hasta -12 dB); si
  ni asi, error.
- Verificacion en cada mezcla (la grabacion falla si no se cumple): ventanas
  de 400 ms con voz activa (RMS de la voz >= -35 dBFS): musica >= 20 dB por
  debajo en RMS en el 95 % y >= 25 dB en 1-4 kHz en el 95 % (de las ventanas
  con energia de voz en esa banda); sin muestras saturadas. El error dice el
  percentil 5 y cuantos dB hay que bajar. Se imprime una linea `musica: ...`
  con cama, ganancia, mezcla (LUFS y dBTP del PCM), margenes, cierre y el
  sha256 del PCM de la musica.
- Cache propio, independiente del TTS (`--cache-musica <dir>`; por omision
  `<cache de voz>/../musica-cache`, p. ej. `build/video/musica-cache/`): clave
  = SHA-256 de version del generador, preset y configuracion, muestras,
  intervalos de ducking y `ffmpeg -version`. Guarda la musica final (f32 mono)
  y un JSON con su sha256 y la calibracion. No promete identidad binaria entre
  plataformas distintas (D16).
- `--solo-audio <dir>` (con `--voz`): escribe `voz.s16` (antes de sumar) y
  `pista.s16` (lo que recibe ffmpeg) sin sesion grafica ni grabacion, e
  imprime sus sha256. Lo usan las pruebas `satcfdi_video_musica_*`.
- Pico verdadero (D25): -2 dBTP con +0,2 dB de tolerancia sobre el AAC
  decodificado. La voz se normaliza a -3 dBTP (`kPicoObjetivoVozDb`): con -2
  la muestra de T024.4 dio -1.76 dBTP en el AAC; con -3, -2.84. Al terminar cada grabacion con audio, la herramienta decodifica
  el MP4, imprime `audio AAC decodificado: X LUFS, Y dBTP (maximo -1.8)` y, si
  pasa de -1.8 dBTP, falla sin publicar nada. `verificar-d16.cmake` lo
  comprueba tambien (`picoVerdaderoMaximoAacDb` del manifiesto).
- El cache se publica de forma comprobada: PCM y manifiesto completos en
  temporales (escritura, flush y cierre verificados) y luego `rename(2)`
  atomico, primero el PCM y al final el manifiesto (un manifiesto que no
  coincide con el sha256 del PCM hace regenerar la entrada).
- Las metricas no certifican inteligibilidad ni caracter musical: hace falta
  escucha.

## Salidas

Solo en `--salida` (por omision `build/video/`, ignorado por git), publicadas
con renombre atomico: se preparan en `<salida>/.publicando-XXXXXX/` y cada una
se renombra sobre el destino sin borrar antes la anterior:
`<nombre>.mp4` (H.264 `yuv420p`, 2560x1440, 30 fps, `+faststart`, capitulos),
`<nombre>.srt` (un cue por linea del guion) y `<nombre>.linea-tiempo.json`
(cuadro de inicio y duracion de capitulos, escenas y lineas; base para la voz
y el manifiesto de verificacion). Todo lo intermedio (ZIP sinteticos, PDF,
iconos, capitulos FFMETADATA, MP4 parcial, `ffmpeg.log`) vive en un
`QTemporaryDir` que se borra al terminar. La cache de QML se desactiva
(`QML_DISABLE_DISK_CACHE=1`). El MP4 lleva ademas la pista de capitulos de
QuickTime (`bin_data`) que ffmpeg agrega al escribir capitulos.

## Guiones

- `guion-tutorial.json`: el tutorial (especialista-guion-tutoriales); lo
  graba `video_tutorial` (cache `SATCFDI_VIDEO_GUION`) y lo valida, sin grabar, la
  prueba `satcfdi_video_guion_tutorial`.
- `guion-ejemplo.json`: ejemplo corto que usan las demas pruebas.

## Composicion (D8, D21, D22; 04-motion-composicion.md)

El video es un lienzo de 2560x1440 (1280x720 logicos, `qml/Lienzo.qml`, una
segunda ventana de la herramienta que no toma el foco):

- Ventana de la app centrada a escala 1: la captura de la ventana real
  (960x540 logicos = 1920x1080 px) se pinta pixel a pixel en x 320-2240 px,
  bajo una barra de titulo simulada de 28 logicos (semaforo, sin texto), con
  esquinas de 12 logicos, sombra (negro 35 %, desenfoque ~48 px, +20 px) y
  borde de 1 px `#1a000000`. El cursor, la onda y las ilustraciones van
  dentro de la captura: siguen alineados con los controles (objectName).
- Fondo generativo (D22): `ShaderEffect` con `qml/fondo.frag` precompilado a
  `.qsb` (`qt_add_shaders`): fBm de ruido simplex (5 octavas, lacunaridad 2,
  ganancia 0.45, octavas altas atenuadas) con domain warping de dos niveles,
  gradiente de la paleta, acento dorado en una franja estrecha y vineta de
  -20 %. Tiempo = cuadro / 30 * velocidad (deriva ~50 s), giro de 2 grados
  por minuto, semilla fija; la fase sube 0.35 por capitulo, interpolada en 60
  cuadros.
- Titulo de capitulo en el margen superior (x de la ventana): entra en 9
  cuadros, se queda `cuadrosTitulo` al 100 % y baja al 55 %, donde
  permanece; al cambiar de capitulo, fundido cruzado de 9 cuadros.
- Transcripcion quemada abajo, desde la misma linea de tiempo del `.srt` (que
  se conserva): ranura actual (40 px) y anterior (32 px, 45 %), pildoras
  `#cc1c1c1e`, ancho maximo 1600 px. Al empezar un cue la actual sube en 8
  cuadros y la nueva entra en 6. Un cue sigue visible hasta 2 s despues de
  su fin, sin pasar del fin de su escena.
- Todo se calcula con el numero de cuadro de la salida: nada depende del
  reloj. Cada cuadro se verifica: el texto quemado es el cue vigente
  ("transcripcion verificada: N cuadros con cue"; si no, la grabacion falla).
- Determinismo: el texto se rasteriza con CoreText (`QML_DISABLE_DISTANCEFIELD`)
  y x264 cuantiza sin mb-tree ni AQ, para que una diferencia minima en una
  zona no cambie el resto del cuadro. Dos grabaciones: fondo y transcripcion
  identicos; cuadro completo >= 78 dB (prueba `satcfdi_video_composicion`).
- Raiz `"composicion"` (opcional; sin ella, estos valores):

```json
"composicion": {
  "lienzo": [2560, 1440],
  "fondo": {"semilla": 2008, "velocidad": 0.02,
            "paleta": ["#052E1C", "#0A5F38", "#15924F", "#0E1F18", "#C9A227"]},
  "transcripcion": {"lineas": 2}
}
```

  `paleta`: base, verde Santos, luz, sombra y acento. `lineas`: 1 (solo el cue
  en curso) o 2.
- Foco: si otra aplicacion toma el foco del sistema durante la grabacion
  (macOS ya no deja que una app lo recupere sola), la herramienta devuelve el
  foco de Qt a la ventana de la app con `QWindowSystemInterface` (Qt GUI
  privado) en cada cuadro: teclado, campo activo y apariencia activa no
  dependen del sistema (probado activando Finder a mitad de una grabacion).

## Portada y cierre (T024.4, 03-motion §4)

Raiz `"portada": true` y `"cierre": true` (booleanos; por omision false; con
`--escenas` no se graban). Portada de 120 cuadros antes del primer capitulo y
cierre de 150 despues del ultimo, sin voz ni transcripcion; la linea de
tiempo los declara (`portada`, `cierre`) y el primer capitulo empieza en el
cuadro 120. Todo depende solo del cuadro de la salida (`Lienzo.qml`):

- Fondo con `atenuacion` (uniform de `fondo.frag`): 0.6 en la portada, sube a
  1.0 en 96-119; en el cierre baja de 1.0 a 0.6 en 0-23.
- Portada: capas de `resources/branding/` (carpeta y frente escalan de 0.96 a
  1 en 12-23; hoja posterior 18 y anterior 24 bajan 40/56 logicos; pestana en
  38-43; wordmark sube 8 logicos en 42-53) con halo `MultiEffect`; la marca se
  va en 96-107 y la ventana entra en 102-119.
- Cierre: ventana y textos salen en 0-14; `lockup-video.svg` entra en 10-21
  (escala 0.98 -> 1) con el mismo halo; velo `#052E1C` de 112 a 136: el
  ultimo cuadro es solido.
- Pruebas: `satcfdi_video_portada_cierre` (linea de tiempo; ultimo cuadro
  #052E1C; sin ventana en la portada 90; PSNR >= 40 dB entre la marca de la
  portada 90 y la del cierre en total-90, en el interior: el h264 separa los
  bordes) y `satcfdi_video_musica_portada_cierre` (voz < -60 dBFS en portada y
  cierre; musica audible en la portada y en el cierre 0-44; < -60 dBFS desde
  el 135).

## Overlays dentro de la ventana (motion design, revision-motion.md)

- El titulo de capitulo ya no va dentro de la ventana: lo pone el lienzo.
- Onda del clic: 15 cuadros, anillo 40 pt `#0062d6` (escala 0.5 a 1.6,
  opacidad 0.9 a 0) y disco de 24 pt al 35 % (entra en 2, sale en 8) (M8).
- Cursor y onda se ocultan en ilustraciones con fundido de 6 cuadros (M9).
- Capa de ilustracion: fundido de 8 cuadros al entrar y salir, y cruzado
  (4 + 4) al cambiar de ilustracion (M10).
- Determinismo de los overlays: fundidos y onda se calculan por numero de
  cuadro (propiedad `cuadro` de la superposicion), no con animaciones de Qt
  Quick, que al arrancar entre cuadros toman una fraccion de tiempo real.
- Latido: una animacion infinita invisible mantiene en marcha el reloj de
  animaciones de la app. Sin ella, la primera animacion tras un periodo sin
  animaciones saltaba a su final (asi se cortaba en seco el titulo, M2).
- Tras cada accion instantanea (mutaciones, ilustraciones, barreras) la
  herramienta deja terminar en tiempo real todo el trabajo asincrono que
  dispara (continuaciones de QFuture, pool de hilos, layouts) antes del
  siguiente cuadro.

## Clic perdido por cambio de app (T022)

Causa: el usuario cambia de app mientras corre una grabacion. Si la ventana
pierde el foco del sistema entre el press y el release de un `clic`, Qt Quick
suelta el agarre del raton: el release no llega al control y el clic no
tiene efecto (era la intermitencia de `--rapido`). Una via es el
`WindowDeactivate` no espontaneo, reproducida por inyeccion
(`SATCFDI_VIDEO_DIAGNOSTICO_DESACTIVAR`). Con cambios de app reales el
agarre tambien se pierde por otra via, que no se investigo.

- `FiltroEntradaReal` descarta `WindowDeactivate`. Reduce el riesgo, pero
  no lo elimina.
- Antes del release, la herramienta exige que el raton siga agarrado por el
  objetivo del `mover` previo, un descendiente o un ancestro (p. ej. la fila
  que agarra el clic hecho sobre su texto). Con un grabber ajeno falla; sin
  grabber solo vale si el objetivo sigue `pressed`/`down` (`Agarre.h`). Si
  un clic pierde el agarre, la grabacion falla con
  "el clic sobre '<objetivo>' perdio el agarre del raton antes del release",
  imprime la traza en stderr y deja `<salida>/<nombre>.fallo.png`, sin
  reintentar.

Recomendacion: no cambiar de app mientras corren las pruebas graficas ni la
grabacion.

Pruebas: `satcfdi_video_agarre` (contrato de `Agarre.h` con items QML
sinteticos), `satcfdi_video_desactivacion_corregida` y
`satcfdi_video_desactivacion_detectada` (`pruebas/prueba-desactivacion.cmake`).

## Entorno fijo (D4, D8)

`QSG_RENDER_LOOP=basic`, `QT_QPA_PLATFORM=cocoa`, `TZ=UTC`, locale `es_MX`,
`Theme.oscuro=false`, sin parpadeo del cursor de texto, reloj de la app y
"hoy" de la app (`setReloj`) iguales a `datos.ahora`, ventana 960x540 logica
en la pantalla de mayor `devicePixelRatio`, reloj de animaciones propio
(`QAnimationDriver`: cada cuadro avanza 1/30 s). Las barreras congelan ese
reloj. La entrada real del usuario (raton, teclado, perdida de foco) se
descarta; solo cuentan los eventos sinteticos del guion.

Riesgo residual de reloj (historico; `NuevaSolicitudViewModel` se retiro en
T024.1): `NuevaSolicitudViewModel` creaba su propia
`ConsultaPreparacionPerfiles` con el reloj del sistema (sin forma de
inyectarlo desde fuera de `src/`). Ahi una e.firma cuyo `vigenteHasta` ya paso
en la fecha REAL del equipo cuenta como vencida y el perfil no aparece en el
selector. Con `efirma: "porVencer"` (ahora + 21 dias) eso ocurre a partir de
`datos.ahora + 21 dias` en tiempo real. El resto de la UI usa el reloj del
guion. No hay otras lecturas de la fecha real que lleguen a pantalla
(revisado: `currentDate`, `currentDateTime`, `relojSistema`, `new Date` en QML).

Residual no determinista (no se corrige sin tocar `src/`): los indicadores
indeterminados de Qt Quick Controls (BusyIndicator de "Validando…", barra de
"Descargando", indicador de "Leyendo paquetes") se animan con su propio
temporizador de tiempo real. Los cuadros donde aparecen difieren entre
corridas; el manifiesto D16 no debe muestrearlos. ffmpeg usa un cuadro clave
por segundo (`-g 30`) para que una diferencia no se propague mas de 30 cuadros.

Reproducibilidad medida (guion de ejemplo de 2000 cuadros, dos corridas en el
sandbox): mismos cuadros y `.srt` identico; PSNR minimo por cuadro 63.4 dB. No es identico byte a byte:
el texto rasterizado por la GPU varia en 1 nivel en algunos pixeles.

Ilustraciones reducidas (T024.4, D16): una ilustracion mas grande que la
ventana (`selectorArchivos`, `finder`) se reduce con `scale`. Sin mas, su
texto se muestreaba con filtro lineal desde el atlas de glifos, cuya
disposicion depende de lo que se dibujo antes, y la columna del borde de
algunos glifos cambiaba entre corridas (17 pixeles por cuadro en
`perfil/registrar-efirma`). Ahora `Superposicion.qml` dibuja esas
ilustraciones en una capa (`layer.enabled` cuando la escala es < 1) y reduce
la textura. La prueba `satcfdi_video_ilustracion_determinista` graba dos
veces la escena con `selectorArchivos` del guion de ejemplo y exige PNG
identicos.

### Verificacion D16 (`verificar-d16.cmake`, `verificacion-d16.json`)

- `verificacion-d16.json` (versionado): video h264 2560x1440 30/1, audio aac
  48 kHz, tolerancia entre pistas (100 ms), duracion total (300-480 s),
  esquema 2 (T024.4) con `huellaGuion` (SHA-256 del guion) y `huellaVoz`
  (SHA-256 de las claves de bloque del cache de voz usadas),
  `totalCuadros`, `psnrMinimoDb` (60), formato de decodificacion del audio para
  el MD5 (`-c:a pcm_s16le -ar 48000 -ac 2 -f md5`), capitulos (titulo, inicio y
  fin en cuadros) y cuadros muestreados: la mitad de la primera escena de cada
  capitulo y, por escena de ilustracion, el ultimo cuadro antes del momento de
  la primera linea esperada tras `mostrarIlustracion` (la ilustracion sigue
  visible). Ningun cuadro muestreado cae en los indicadores indeterminados.
- `cmake -DMODO=verificar -DMANIFIESTO=... -DA=<mp4> [-DB=<mp4>] -DFFMPEG=...
  -DFFPROBE=... -P verificar-d16.cmake`: A (y B) contra el manifiesto
  (ffprobe y `-show_chapters`); con B, ademas mismo numero de cuadros, mismo
  MD5 del audio decodificado y PSNR >= 60 dB en cada cuadro muestreado (una
  sola pasada de ffmpeg; identicos = inf). Codigo != 0 si algo no coincide.
  Target: `video_tutorial_verificar` (A = `build/video/tutorial.mp4`, B =
  `build/video/verificacion/tutorial.mp4`).
- `-DMODO=generar -DGUION=... -DLINEA_TIEMPO=<x.linea-tiempo.json>
  -DMANIFIESTO=...`: regenera el manifiesto desde la grabacion aprobada (solo
  si cambia el guion; el diff se revisa). Exige que la linea de tiempo sea de
  ese guion y de una grabacion con voz. Target:
  `cmake --build build --target video_tutorial_manifiesto` (desde
  `build/video/tutorial.linea-tiempo.json`).
- D16 (T024.4): cada MP4 lleva en su etiqueta `comment`
  `satcfdi-d16 guion=<sha256> voz=<sha256|ninguna>`. `verificar` (que ahora
  pide `-DGUION`) falla si el manifiesto no tiene huellas (es el del video
  anterior: regeneralo), si es de otro guion, o si un video se grabo con otro
  guion u otra voz (cache de voz anterior).
- Prueba `satcfdi_video_verificacion_d16`: videos sinteticos pequenos; pasa con
  copias identicas y falla con contenido, capitulos o pistas distintos.

## Sin red (D15)

La grabacion (target `video_tutorial` y pruebas graficas) corre dentro de
`sandbox-exec -f sin-red.sb`, un perfil versionado `(allow default)
(deny network*)` que niega todo socket (IP y Unix) al proceso y a sus hijos
(ffmpeg); cocoa y Qt Quick funcionan (usan puertos Mach). La prueba
`satcfdi_video_red_sandbox` demuestra que dentro del sandbox una conexion
falla con `Operation not permitted`, y `satcfdi_video_red_control` que la
misma sonda conecta fuera de el. Defensas adicionales: el ejecutable no enlaza `satcfdi_sat`, `satcfdi_secrets_macos`,
`satcfdi_os_macos`, `satcfdi_app_core` (arrastra `satcfdi_sat`), `satcfdi_crypto`,
`satcfdi_infrastructure` ni Qt Sql, y ningun target del proyecto en su grafo
enlaza Qt Network. CMake lo comprueba al configurar. `QtQml`/`QtQuick` de Qt si
dependen de `QtNetwork` (por eso aparece en `otool -L`); para eso, un
interceptor de URL de QML deja pasar solo `file:`, `qrc:`, `data:` e `image:`.
Cualquier otro esquema se bloquea; con `--offline` la grabacion ademas falla
de inmediato. `sandbox-exec` esta marcado como obsoleto por Apple pero funciona
en macOS 26 (verificado). El paso de voz (corte 3) sera un ejecutable o script
aparte, fuera del sandbox.

## Guion (esquema 1)

```jsonc
{
  "esquema": 1,
  "tipo": "tutorial" | "ejemplo",   // tutorial: 9000 a 14400 cuadros (5:00 a 8:00), exactamente 7 capitulos
  "ventana": {"ancho": 960, "alto": 540}, // T024.4: ventana logica (800-1200 x 450-640); por omision 960x540
  "portada": true, "cierre": true, // T024.4: 120 y 150 cuadros de marca sin voz (booleanos; por omision false)
  "titulo": "…",                   // titulo del MP4
  "presupuestoTotal": {"min": N, "max": N}, // solo con tipo "ejemplo"
  "cuadrosTitulo": 75,             // titulo del capitulo en pantalla (0-300)
  "rotuloHerramienta": false,      // D14: rotulo propio ademas del de la ilustracion
  "voz": {…},                      // modelo, voz, instrucciones, formato ("wav"), velocidad (0.25-4);
                                   // objeto con solo esas claves y esos tipos, o error de validacion
  "pausas": {"bloque": 300, "escena": 600, "capitulo": 1000}, // ms minimos (D19)
  "montaje": {"tempo": 1.0},       // velocidad local de la voz (0.8-1.25), sin resintetizar;
                                   // "pausaMaxima": ms (100-1000) acorta silencios internos
  "musica": {"activa": true, ...},  // musica de fondo local (ver "Musica de fondo"); ausente = sin musica
  "datos": {
    "ahora": "2026-10-01T10:00:00Z",          // reloj fijo de la app
    "perfiles": [{"clave", "rfc", "nombre", "efirma": "lista|porVencer|sinEFirma",
                  "vigenteHasta"?: "ISO", "visible"?: true}],
    "solicitudes": [{"clave", "perfil", "tipo": "emitidos|recibidos",
                     "desde": "yyyy-MM-dd", "hasta": "yyyy-MM-dd", "estado", "visible"?: true,
                     "cfdis"?: [{"tipo": "I|E", "serie"?, "folio", "contraparte",
                                 "fecha": "yyyy-MM-ddTHH:mm:ss", "subtotal": "1000.00",
                                 "descripcion"}]}],
    // T024.4: descarga guiada simulada (Facturas, franja e Historial por lotes)
    "meses": [{"perfil", "mes": "yyyy-MM", "estado", "direccion"?, "causa"?, "descargadoEn"?: "ISO",
               "emitidas"?: N | [cfdi…], "recibidas"?: N | [cfdi…]}],
    // T024.4: estado inicial del respaldo (ausente = el del fake comun)
    "respaldo": {"carpeta"?: true, "contrasena"?: true, "diario"?: true, "ultimoExito"?: "ISO"}
  },
  "capitulos": [{"id", "titulo", "escenas": [{
      "id", "presupuesto": {"min": N, "max": N},  // cuadros
      "colchon"?: 15, "separacionLineas"?: 6,
      "lineas": [{"id", "texto", "voz"?, "inicio"?, "nuevoBloque"?}],
      "acciones": [ … ]
  }]}]
}
```

- RFC solo `XAXX010101000` o `XEXX010101000` (D2). Con `cfdis` se genera antes
  de grabar un ZIP sintetico (XML CFDI 4.0 con IVA 16 %) que se indexa con el
  lector ZIP, el parser y `calcularIndicePaquete` reales; el PDF sale de
  `RenderizadorPdfCfdi`.
- Perfil con `"visible": false`: aparece cuando la UI lo crea (Nuevo perfil)
  con ese RFC; crear cualquier otro RFC hace fallar la grabacion (D2).
- Estados de solicitud: `creada`, `enviando`, `enviada` (Aceptada por SAT),
  `enProceso`, `error` (Error SAT), `terminada` (paquete Disponible),
  `descargando`, `descargada` (paquete Descargado; habilita Ver facturas).
- Lineas (D13, D19): toda linea tiene `texto` (un cue del `.srt`), termina en
  `.` `?` `!` o `:` y pertenece a un bloque de narracion; `voz` solo cambia la
  pronunciacion. Tiempos: ver "Voz por bloques". Sin `--voz`, el audio de cada
  bloque se estima por longitud (15 caracteres/s) con marcas proporcionales.
- `datos.meses` (T024.4): cada mes declarado genera, por direccion con
  facturas, un ZIP sintetico real (mismo XML que `cfdis`) que se indexa con el
  codigo real; la lista y el PDF de Facturas salen de ahi. `emitidas` /
  `recibidas` son un conteo (facturas deterministas dentro del mes) o una
  lista explicita (`fecha` dentro del mes). Estados: `sinDescargar`,
  `preparando`, `disponibles`, `disponiblesParcialmente` (la `direccion`
  completa, por omision emitidas; la otra sin descargar) y
  `requiereAtencion` (la `direccion`, por omision recibidas, con `causa` =
  `claveEstable(CausaAtencion)`, p. ej. `ErrorSat`; la otra Disponibles).
  Vale tambien para perfiles `"visible": false` (Facturas los muestra en
  cuanto la UI los crea). Historial: un lote por mes descargado (fecha
  `descargadoEn`, o el dia 1 del mes siguiente) con resultado coherente con su
  estado, mas una entrada "Anterior" por solicitud visible. Ejecutar la accion
  de la franja ("Descargar facturas") pasa a Preparando las direcciones sin
  facturas y crea su lote, como el worker; el guion lo termina con
  `avanzarMes`.
- Escena: dura `max(fin de acciones, fin de voz) + colchon`. La validacion
  previa falla si una escena queda fuera de su presupuesto o el total fuera
  del suyo, con un mensaje que nombra `escena '<capitulo>/<escena>'`.
- Tutorial (T024.4): exactamente 7 capitulos y 5:00-8:00; falla si el titulo
  de un capitulo o el `texto`/`voz` de una linea dicen "solicitud(es)" o
  "paquete(s)" (T024 D3; sin importar mayusculas ni acentos), nombrando
  escena, linea y campo.

### Acciones (secuenciales; cuadros a 30 fps)

| tipo | campos | cuadros |
| --- | --- | --- |
| `esperar` | `cuadros` | `cuadros` |
| `mover` | `objetivo` (objectName visible o ruta), `cuadros`?, `desplazamiento` [dx, dy], `ancla` (auto, centro, derecha, abajo), `hover`=false | `cuadros`; sin `cuadros`, el plan reserva 40 y el cursor se mueve `clamp(round(10 + d/22), 14, 40)` cuadros segun la distancia y reposa el resto (M6) |
| `clic` | `pausa`=8, `cuadros`=10, `sinEfecto`=false | `pausa` + `cuadros`: cursor quieto `pausa` cuadros; luego onda + press y release 3 cuadros despues (M7); con `sinEfecto` solo la onda (p. ej. Elegir…, que abriria un dialogo nativo) |
| `escribir` | `texto`, `cuadrosPorCaracter`=3 | `len * cuadrosPorCaracter` |
| `escribirContrasena` | `longitud`, `cuadrosPorCaracter`=3 | igual; valor sintetico fijo, nunca en el guion ni en logs |
| `tecla` | `tecla`: `[Cmd+][Shift+][Opcion+][Ctrl+]<tecla>` (en ese orden; tecla: Enter, Escape, Tab, Abajo, Arriba, Izquierda, Derecha, Espacio, Retroceso, Coma, Punto, A-Z o 0-9); `mostrar`? (keycap; por omision solo con modificadores); `cuadros` | 36 con keycap (entra 0-5, evento en el 6, sale 28-35; tambien con `--rapido` el evento va en el 6), 6 sin keycap (evento en el 0). Cmd = `Qt::ControlModifier` (en macOS Qt intercambia Ctrl y Cmd); los atajos van por el mapa de atajos de Qt (`QWindowSystemInterface::handleShortcutEvent`) |
| `desplazar` | `objetivo` (un Flickable o un control que lo contiene, p. ej. `<prefijo>Desplazable` de los dialogos de Respaldo/Exportar), `hasta` (contentY) o `delta`, `cuadros`? | `cuadros` (24 con `hasta` o con abs(`delta`) > 200, si no 18); contentY por cuadro con InOutQuad, acotado al contenido |
| `avanzarMes` | `perfil`, `mes` (de `datos.meses`), `estado`, `direccion`? (emitidas, recibidas, ambas), `causa`? | 0; cambia la franja, la lista y el Historial como el worker (p. ej. `preparando` y luego `disponibles` por direccion) |
| `elegirCarpeta` | `destino`: exportacion o respaldo; `ruta` bajo `/Users/usuario/` | 0; lo que el FolderDialog entregaria (D10: el dialogo real nunca se abre) |
| `progresoRespaldo` | `fase` (captura, escritura, publicacion, depuracion), `bytes`, `total`? | 0; requiere un respaldo en curso ("Respaldar ahora") |
| `resultadoRespaldo` | `resultado`? (exito, conAvisos, error), `bytes`? (12400000), `fecha`? (ISO; por omision `datos.ahora`) | 0; termina el respaldo en curso |
| `esperarRespaldo` | `resultado`? (exito, conAvisos, error), `timeoutMs`? (5000) | 0 (barrera: la pagina muestra el resultado con esa variante) |
| `mostrarBanner` | `notificaciones`: como en la ilustracion `notificacion` | 0; banner 400x70 sobre la app viva (motion 2b): entra en 10, el segundo en el cuadro 14 y 82 mas abajo, rotulo "Ilustración" |
| `ocultarBanner` | | 0; sale en 10 cuadros |
| `esperarLinea` | `linea`, `momento`: inicio o fin | hasta ese cuadro |
| `avanzarSolicitud` | `solicitud` (clave o `nuevaN`), `estado`, `verificadaEn`? (ISO: resultado de Verificar ahora) | 0 |
| `cambiarEFirma` | `perfil`, `efirma`, `vigenteHasta`? | 0 |
| `mostrarSolicitudes` | `claves`: reemplaza la lista visible | 0 |
| `seleccionarArchivoEFirma` | `archivo`: certificado o llave; `ruta` bajo `/Users/usuario/` (nunca se lee) | 0 |
| `completarEFirma` | `perfil`, `vigenteHasta`? | 0; termina "Validando…" con exito |
| `mostrarIlustracion` | `ilustracion`: menuBar, iconoMenuBar, notificacion, finder, selectorArchivos, selectorCarpeta (T024.4: `qml/IlustracionSelectorCarpeta.qml`, 720x440, `carpeta`, `nombreCarpeta`?, `carpetas`, `seleccionada`); `parametros`. `notificacion` acepta tambien `{tipo: facturasDescargadas, perfil, mes, direccion: emitidas\|recibidas}` (texto de `ServicioNotificaciones`) | 0 |
| `ocultarIlustracion` | | 0 |
| `esperarDocumento` | `timeoutMs` (1-30000) | 0 (barrera) |
| `esperarConteo` | `objetivo`, `conteo`, `timeoutMs` | 0 (barrera) |
| `esperarExportacion` | `resultado`? (exito, advertencia, neutro, error, esperandoNombre; exito por omision), `timeoutMs` (1-30000) | 0 (barrera) |

- Cursor (revision de motion design): el destino de `mover` se recalcula en
  cada cuadro (M3), con smoothstep. `ancla` auto: a la derecha del texto de un
  Text/Label (x + contentWidth + 6) si el `mover` no termina en `clic`;
  centro en botones, campos, filas y si sigue un clic (M11, M12).
- Hover (M4): el cursor simulado NO genera hover. El raton sintetico solo
  entra a la ventana del press al release de un `clic` y despues sale; asi no
  hay tooltips con temporizador de tiempo real ni estados de hover variables.
  `"hover": true` en un `mover` envia MouseMove en cada cuadro (bajo riesgo
  del autor: los tooltips aparecen tras 600 ms reales).
- `objetivo`: objectName de un control visible (el primero en el arbol
  visual) o una ruta `padre/hijo` (p. ej. `campoTipoDescarga/segmento_Recibidos`,
  `filaHistorial_0/botonDetalles`). Si no esta en la primera busqueda (la UI
  aun se asienta tras un clic o un future), la herramienta bombea eventos y
  renderiza con el reloj congelado hasta que exista y sea visible, con un
  limite de 2 s reales (igual con `--rapido`); no consume cuadros de la linea
  de tiempo, asi que el video no cambia. Vencido el limite, falla nombrando la
  escena y el objectName.
- `nuevaN`: la N-esima solicitud creada desde la UI durante la grabacion
  (vale en `avanzarSolicitud` y en `mostrarSolicitudes`).
- Facturas e Historial (T022 D6; desde T024.2 con los fakes comunes de
  `tools/fakes-materiales`, los mismos de `tools/capturas-manual`): Facturas
  es la pagina inicial, como en la app. Muestra los perfiles visibles del
  guion con su e.firma y el catalogo mensual de `datos.meses` (vacio sin el). El Historial
  tiene una entrada por solicitud visible del perfil, la mas reciente
  primero, y su "Ver detalles tecnicos" (`filaHistorial_N/botonDetalles`)
  abre el detalle (Verificar ahora, Ver facturas). Desde Perfiles,
  `botonRegresar` vuelve a Facturas.
- Exportacion y Respaldo (T024.2): la sesion conecta el
  `ExportacionFacturasService` real con puertos en memoria
  (`materiales::ExportacionSintetica`, sin disco ni PDF reales) y los
  controles sinteticos de respaldo, restauracion y datos previos. T024.4: el
  respaldo queda en curso tras "Respaldar ahora" hasta `resultadoRespaldo`
  (con `progresoRespaldo` entre medias); el nombre visible de la carpeta es el
  ultimo componente de la ruta de `elegirCarpeta`.
- Dialogos nativos y sistema (D10, T024.4): la grabacion falla nombrando la
  escena si se abre un FileDialog/FolderDialog (o cualquier dialogo de
  QtQuick.Dialogs), si la UI pide Finder o Vista Previa, abre una URL con el
  sistema (`QDesktopServices`/`Qt.openUrlExternally`) o escribe en el
  portapapeles. Cada `clic` con `sinEfecto` queda en la salida
  (`clic sinEfecto: escena …`) y en `sinEfecto` de la linea de tiempo.
- `mover` falla si el centro del control queda fuera de la vista de su
  Flickable ancestro (p. ej. el contenido desplazable de los dialogos en
  960x540): el guion debe `desplazar` ese Flickable antes.
- Guia de primer uso de Facturas (`guiaPrimerUsoFacturas`): se deriva en vivo
  de los datos (perfiles visibles, alguna e.firma Lista) y se ve sin
  perfiles. Pasos `guiaPrimerUsoFacturas/pasoPerfil|pasoEFirma|pasoFacturas`
  y sus botones `.../boton_<paso>`.
- Clave de recuperacion (T024.4): la herramienta conecta "Copiar" e
  "Imprimir…" para que se vean como en la app; si se pulsan con efecto, la
  grabacion falla (D10: portapapeles e impresion son nativos).
- Barreras: congelan el reloj (no consumen cuadros) y esperan en tiempo real.
  `esperarDocumento`: `documentoListo` del visor (`visorPdfCfdi`) y la primera
  pagina rasterizada; `documentoError` o el timeout fallan nombrando la escena.
  `esperarConteo`: la propiedad `count` del control (p. ej. `listaFacturas`).
  `esperarExportacion`: la exportacion abierta sale de Planeando/Exportando y
  termina en el `resultado` declarado (la variante del resultado, o
  `esperandoNombre`); falla nombrando la causa si no hay exportacion abierta,
  vence el timeout, vuelve al dialogo por un error del plan o del destino, se
  cierra o el resultado es otro (p. ej. una cancelacion es `neutro`). El
  servicio corre en su propio hilo, por eso es barrera.
  El timeout es de tiempo real (ms), no de cuadros.
- Busqueda del explorador: la consulta usa un debounce `QTimer` de 250 ms en
  tiempo real (`ExploradorCfdiViewModel`). Despues de `escribir` en
  `campoBusquedaFacturas` va siempre `esperarConteo` con el numero de filas
  esperado (y otro tras limpiar), asi el cuadro siguiente ya muestra el
  resultado. Riesgo residual: si entre dos teclas pasan mas de 250 ms reales,
  un filtro intermedio puede verse unos cuadros; el contador "N de M" se
  actualiza al instante.
- e.firma (E1.2-E1.4): Nuevo perfil y Guardar usan el servicio de perfiles de
  la herramienta; Registrar deja la importacion pendiente ("Validando…") hasta
  `completarEFirma`. Los Elegir… se pulsan con `sinEfecto` y el archivo llega
  con `seleccionarArchivoEFirma`; la contrasena con `escribirContrasena`.
- `menuBar` e `iconoMenuBar` usan los textos y el dibujo reales
  (`menubar::entradas`, `dibujarIconoMenuBar`); `finder` y `selectorArchivos`
  toman sus propiedades de `parametros`.
- `notificacion`: `parametros.notificaciones` = lista de
  `{"tipo": "descargaCompleta", "solicitud": clave}` o
  `{"tipo": "efirmaPorVencer", "perfil": clave}`. Titulos y cuerpos salen de
  `ServicioNotificaciones` real; los botones de accion, de una COPIA de la
  regla de `notificacionLocalDe` (`src/app_core/IntegracionWorker.cpp`) en
  `NotificacionesIlustradas.cpp`, porque la herramienta no enlaza app_core.
  La prueba `satcfdi_video_sync_notificaciones` compara la huella SHA-256 de
  esa funcion; si cambia, revisa la copia y actualiza
  `SATCFDI_VIDEO_HUELLA_NOTIFICACIONES`.
- D14: las cinco ilustraciones de capturas traen su rotulo "Ilustración"
  (lo vigila `satcfdi_video_rotulo_ilustraciones`); `rotuloHerramienta: true`
  agrega ademas el de la herramienta (decision de motion design pendiente).
