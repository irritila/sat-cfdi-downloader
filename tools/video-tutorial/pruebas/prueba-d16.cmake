# Prueba del script D16 (verificar-d16.cmake) con videos sinteticos pequenos
# hechos con ffmpeg (lavfi): pasa con dos copias identicas y falla (codigo
# != 0) con contenido distinto, capitulos que no coinciden, pistas de
# distinta duracion o pico verdadero del AAC por encima del maximo. Variables: SCRIPT, FFMPEG, FFPROBE, DIR.

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")

file(WRITE "${DIR}/capitulos.txt" ";FFMETADATA1
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

# video(<salida> <fuente lavfi> <segundos de audio>)
function(video salida fuente audio)
    execute_process(COMMAND ${FFMPEG} -v error -y -nostdin -f lavfi -i "${fuente}" -f lavfi
                            -i "sine=frequency=440:sample_rate=48000:duration=${audio}" -i "${DIR}/capitulos.txt"
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

function(manifiesto salida capitulo2 pico)
    file(WRITE "${salida}" "{
  \"esquema\": 1,
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

# verificar(<codigo esperado: 0 | fallo> <manifiesto> <a> [b])
function(verificar esperado man a)
    set(b "")
    if(ARGC GREATER 3)
        set(b "${ARGV3}")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} -DMODO=verificar -DMANIFIESTO=${man} -DA=${a} -DB=${b}
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
message("OK D16: pasa con copias identicas; falla con contenido, capitulos, pistas o pico distintos")
