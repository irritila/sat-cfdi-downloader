# Prueba del script D16 (verificar-d16.cmake) con videos sinteticos pequenos
# hechos con ffmpeg (lavfi): pasa con dos copias identicas y falla (codigo
# != 0) con contenido distinto, capitulos que no coinciden, pistas de
# distinta duracion o pico verdadero del AAC por encima del maximo. T024.4:
# falla tambien con el manifiesto del video anterior (sin huellas), con un
# manifiesto de otro guion y con un MP4 grabado con otro guion u otra voz.
# Variables: SCRIPT, FFMPEG, FFPROBE, DIR.

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")

# Guion sintetico (solo importa su huella) y huellas de voz.
file(WRITE "${DIR}/guion.json" "{\"titulo\": \"guion de prueba D16\"}\n")
file(WRITE "${DIR}/otro-guion.json" "{\"titulo\": \"otro guion\"}\n")
file(SHA256 "${DIR}/guion.json" huella)
file(SHA256 "${DIR}/otro-guion.json" huella_otra)
string(SHA256 voz "voz de prueba")
string(SHA256 voz_anterior "voz del video anterior")

# capitulos(<archivo> <comentario>): FFMETADATA con la etiqueta comment.
function(capitulos archivo comentario)
    file(WRITE "${archivo}" ";FFMETADATA1
${comentario}
[CHAPTER]
TIMEBASE=1/1000
START=0
END=1000
title=1 · Uno
[CHAPTER]
TIMEBASE=1/1000
START=1000
END=3000
title=2 · Dos
")
endfunction()
capitulos("${DIR}/capitulos.txt" "comment=satcfdi-d16 guion\\=${huella} voz\\=${voz}")
capitulos("${DIR}/capitulos-otra-voz.txt" "comment=satcfdi-d16 guion\\=${huella} voz\\=${voz_anterior}")
capitulos("${DIR}/capitulos-sin-huella.txt" "")

# video(<salida> <fuente lavfi> <segundos de audio> [metadatos])
function(video salida fuente audio)
    set(meta "${DIR}/capitulos.txt")
    if(ARGC GREATER 3)
        set(meta "${ARGV3}")
    endif()
    execute_process(COMMAND ${FFMPEG} -v error -y -nostdin -f lavfi -i "${fuente}" -f lavfi
                            -i "sine=frequency=440:sample_rate=48000:duration=${audio}" -i "${meta}"
                            -map 0:v -map 1:a -map_metadata 2 -map_chapters 2 -t 3 -c:v libx264 -pix_fmt yuv420p
                            -c:a aac -b:a 192k "${salida}"
                    RESULT_VARIABLE r)
    if(NOT r EQUAL 0)
        message(FATAL_ERROR "ffmpeg no pudo crear ${salida}")
    endif()
endfunction()

video(${DIR}/a.mp4 "testsrc2=size=320x180:rate=30:duration=3" 3)
file(COPY_FILE ${DIR}/a.mp4 ${DIR}/b.mp4)
video(${DIR}/distinto.mp4 "mandelbrot=size=320x180:rate=30" 3)
video(${DIR}/corto.mp4 "testsrc2=size=320x180:rate=30:duration=3" 2.5)
video(${DIR}/otra-voz.mp4 "testsrc2=size=320x180:rate=30:duration=3" 3 "${DIR}/capitulos-otra-voz.txt")
video(${DIR}/sin-huella.mp4 "testsrc2=size=320x180:rate=30:duration=3" 3 "${DIR}/capitulos-sin-huella.txt")

function(manifiesto salida capitulo2 pico)
    set(huellas "  \"huellaGuion\": \"${huella}\",\n  \"huellaVoz\": \"${voz}\",\n")
    if(ARGC GREATER 3)
        set(huellas "${ARGV3}")
    endif()
    file(WRITE "${salida}" "{
  \"esquema\": 2,
${huellas}
  \"video\": {\"codec\": \"h264\", \"ancho\": 320, \"alto\": 180, \"fps\": \"30/1\"},
  \"audio\": {\"codec\": \"aac\", \"frecuencia\": 48000},
  \"toleranciaPistasMs\": 100,
  \"duracionMinS\": 2,
  \"duracionMaxS\": 4,
  \"totalCuadros\": 90,
  \"psnrMinimoDb\": 60,
  \"picoVerdaderoMaximoAacDb\": ${pico},
  \"capitulos\": [
    {\"titulo\": \"1 · Uno\", \"inicio\": 0, \"fin\": 30},
    {\"titulo\": \"${capitulo2}\", \"inicio\": 30, \"fin\": 90}
  ],
  \"cuadrosMuestreados\": [
    {\"cuadro\": 10, \"motivo\": \"capitulo 1\"},
    {\"cuadro\": 60, \"motivo\": \"capitulo 2\"}
  ]
}
")
endfunction()
# El seno de lavfi (amplitud 1/8) queda cerca de -18 dBTP.
manifiesto(${DIR}/manifiesto.json "2 · Dos" -1.8)
manifiesto(${DIR}/otro-capitulo.json "2 · Otro" -1.8)
manifiesto(${DIR}/pico-estricto.json "2 · Dos" -30)
# El del video anterior (esquema 1, sin huellas) y uno de otro guion.
manifiesto(${DIR}/anterior.json "2 · Dos" -1.8 "")
manifiesto(${DIR}/otro-guion.json "2 · Dos" -1.8 "  \"huellaGuion\": \"${huella_otra}\",\n  \"huellaVoz\": \"${voz}\",\n")

# verificar(<codigo esperado: 0 | fallo> <manifiesto> <a> [b])
function(verificar esperado man a)
    set(b "")
    if(ARGC GREATER 3)
        set(b "${ARGV3}")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} -DMODO=verificar -DMANIFIESTO=${man} -DGUION=${DIR}/guion.json
                            -DA=${a} -DB=${b}
                            -DFFMPEG=${FFMPEG} -DFFPROBE=${FFPROBE} -DTRABAJO=${DIR} -P ${SCRIPT}
                    RESULT_VARIABLE r OUTPUT_VARIABLE o ERROR_VARIABLE e)
    message("verificar ${a} ${b} contra ${man}: codigo ${r}\n${o}${e}")
    if(esperado STREQUAL "0" AND NOT r EQUAL 0)
        message(FATAL_ERROR "debia pasar")
    elseif(esperado STREQUAL "fallo" AND r EQUAL 0)
        message(FATAL_ERROR "debia fallar")
    endif()
endfunction()

verificar(0 ${DIR}/manifiesto.json ${DIR}/a.mp4)
verificar(0 ${DIR}/manifiesto.json ${DIR}/a.mp4 ${DIR}/b.mp4)
verificar(fallo ${DIR}/manifiesto.json ${DIR}/a.mp4 ${DIR}/distinto.mp4)
verificar(fallo ${DIR}/otro-capitulo.json ${DIR}/a.mp4)
verificar(fallo ${DIR}/manifiesto.json ${DIR}/corto.mp4)
verificar(fallo ${DIR}/pico-estricto.json ${DIR}/a.mp4)
verificar(fallo ${DIR}/anterior.json ${DIR}/a.mp4)
verificar(fallo ${DIR}/otro-guion.json ${DIR}/a.mp4)
verificar(fallo ${DIR}/manifiesto.json ${DIR}/otra-voz.mp4)
verificar(fallo ${DIR}/manifiesto.json ${DIR}/a.mp4 ${DIR}/otra-voz.mp4)
verificar(fallo ${DIR}/manifiesto.json ${DIR}/sin-huella.mp4)
message("OK D16: pasa con copias identicas; falla con contenido, capitulos, pistas o pico distintos, con el "
        "manifiesto anterior o de otro guion y con un MP4 de otro guion u otra voz")
