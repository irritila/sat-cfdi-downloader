# Prueba grafica en modo rapido (T017 D17): graba el guion de ejemplo con
# pocos cuadros y comprueba el MP4 con ffprobe. Sin sesion grafica termina con
# 77 (SKIP_RETURN_CODE: la prueba queda en SKIP, que no cuenta como
# aprobacion). Variables: HERRAMIENTA (lista: sandbox sin red + ejecutable),
# GUION, SALIDA, FFPROBE.

execute_process(
    COMMAND ${HERRAMIENTA} --guion "${GUION}" --salida "${SALIDA}" --nombre rapido --rapido --offline
    RESULT_VARIABLE codigo
    OUTPUT_VARIABLE salida
    ERROR_VARIABLE errores
)
message("${salida}${errores}")
if(codigo EQUAL 77)
    message("SKIP: sin sesion grafica")
    cmake_language(EXIT 77)
endif()
if(NOT codigo EQUAL 0)
    message(FATAL_ERROR "la grabacion rapida termino con codigo ${codigo}")
endif()
# D21: el texto quemado se verifica cuadro a cuadro contra el cue vigente.
if(NOT salida MATCHES "transcripcion verificada: [1-9][0-9]* cuadros con cue")
    message(FATAL_ERROR "no se verifico la transcripcion quemada")
endif()

set(mp4 "${SALIDA}/rapido.mp4")
foreach(archivo "${mp4}" "${SALIDA}/rapido.srt" "${SALIDA}/rapido.linea-tiempo.json")
    if(NOT EXISTS "${archivo}")
        message(FATAL_ERROR "falta ${archivo}")
    endif()
endforeach()

execute_process(
    COMMAND "${FFPROBE}" -v error -select_streams v:0
            -show_entries stream=codec_name,width,height,r_frame_rate,pix_fmt,nb_frames
            -of default=noprint_wrappers=1 "${mp4}"
    RESULT_VARIABLE codigo
    OUTPUT_VARIABLE video
)
if(NOT codigo EQUAL 0)
    message(FATAL_ERROR "ffprobe fallo sobre ${mp4}")
endif()
foreach(esperado "codec_name=h264" "width=2560" "height=1440" "r_frame_rate=30/1" "pix_fmt=yuv420p")
    string(FIND "${video}" "${esperado}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "el MP4 no cumple ${esperado}:\n${video}")
    endif()
endforeach()

# Mismo numero de cuadros que la linea de tiempo.
file(READ "${SALIDA}/rapido.linea-tiempo.json" linea)
string(JSON total GET "${linea}" totalCuadros)
string(JSON capitulosEsperados LENGTH "${linea}" capitulos)
string(REGEX MATCH "nb_frames=([0-9]+)" _ "${video}")
if(NOT CMAKE_MATCH_1 EQUAL total)
    message(FATAL_ERROR "cuadros del MP4 (${CMAKE_MATCH_1}) distintos de la linea de tiempo (${total})")
endif()

execute_process(
    COMMAND "${FFPROBE}" -v error -show_chapters -of csv=p=0 "${mp4}"
    OUTPUT_VARIABLE capitulos
)
string(REGEX MATCHALL "[^\n]+" filas "${capitulos}")
list(LENGTH filas n)
if(NOT n EQUAL capitulosEsperados)
    message(FATAL_ERROR "capitulos en el MP4: ${n}; esperados: ${capitulosEsperados}\n${capitulos}")
endif()

# Un cue por linea con texto.
file(READ "${SALIDA}/rapido.srt" srt)
string(REGEX MATCHALL "[0-9][0-9]:[0-9][0-9]:[0-9][0-9],[0-9][0-9][0-9] --> " cues "${srt}")
list(LENGTH cues ncues)
if(ncues EQUAL 0)
    message(FATAL_ERROR "el .srt no tiene cues")
endif()
message("OK rapido: ${total} cuadros, ${n} capitulos, ${ncues} cues")
