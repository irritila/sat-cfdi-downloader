# Grabacion con voz del motor de prueba (T017 D8, D13): pista AAC-LC estereo
# 48 kHz, misma duracion que el video (+-0.1 s), loudness integrado entre -17
# y -15 LUFS, pico verdadero <= -1 dBTP y cada cue del .srt alineado con el
# inicio de su clip (+-1 cuadro). Sin sesion grafica: 77 (SKIP).
# Variables: VOZ, HERRAMIENTA (lista), GUION, DIR, FFMPEG, FFPROBE.

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
set(cache "${DIR}/cache")

execute_process(COMMAND ${VOZ} --guion ${GUION} --cache ${cache} --motor prueba
                RESULT_VARIABLE codigo OUTPUT_VARIABLE salida ERROR_VARIABLE errores)
if(NOT codigo EQUAL 0)
    message(FATAL_ERROR "paso de voz (prueba) fallo:\n${salida}${errores}")
endif()

execute_process(
    COMMAND ${HERRAMIENTA} --guion ${GUION} --salida ${DIR} --nombre voz --offline --voz ${cache} --motor-voz prueba
    RESULT_VARIABLE codigo OUTPUT_VARIABLE salida ERROR_VARIABLE errores)
message("${salida}${errores}")
if(codigo EQUAL 77)
    message("SKIP: sin sesion grafica")
    cmake_language(EXIT 77)
endif()
if(NOT codigo EQUAL 0)
    message(FATAL_ERROR "la grabacion con voz termino con codigo ${codigo}")
endif()
set(mp4 "${DIR}/voz.mp4")

# Pistas y duraciones.
execute_process(COMMAND ${FFPROBE} -v error -show_entries stream=codec_type,codec_name,profile,sample_rate,channels,duration
                        -of compact=p=0 ${mp4}
                OUTPUT_VARIABLE pistas)
message("${pistas}")
foreach(esperado "codec_name=aac\\|profile=LC\\|codec_type=audio\\|sample_rate=48000\\|channels=2"
                 "codec_name=h264\\|profile=[^|]*\\|codec_type=video")
    if(NOT pistas MATCHES "${esperado}")
        message(FATAL_ERROR "pista inesperada (falta ${esperado})")
    endif()
endforeach()
string(REGEX MATCH "codec_type=video[^\n]*duration=([0-9.]+)" _ "${pistas}")
set(dv "${CMAKE_MATCH_1}")
string(REGEX MATCH "codec_type=audio[^\n]*duration=([0-9.]+)" _ "${pistas}")
set(da "${CMAKE_MATCH_1}")
# math() no acepta decimales: se compara en milisegundos.
function(a_ms valor salida)
    string(REGEX MATCH "^([0-9]+)\\.?([0-9]*)" _ "${valor}")
    string(SUBSTRING "${CMAKE_MATCH_2}000" 0 3 frac)
    math(EXPR ms "${CMAKE_MATCH_1} * 1000 + ${frac}")
    set(${salida} ${ms} PARENT_SCOPE)
endfunction()
a_ms("${dv}" dv_ms)
a_ms("${da}" da_ms)
math(EXPR dif "${dv_ms} - ${da_ms}")
if(dif GREATER 100 OR dif LESS -100)
    message(FATAL_ERROR "video ${dv} s y audio ${da} s difieren mas de 0.1 s")
endif()

# Loudness (EBU R128) del AAC decodificado.
execute_process(COMMAND ${FFMPEG} -hide_banner -nostdin -i ${mp4} -map 0:a -af ebur128=peak=true -f null -
                ERROR_VARIABLE r128)
string(REGEX MATCH "Integrated loudness:[ \n]+I:[ ]+(-?[0-9.]+) LUFS" _ "${r128}")
set(lufs "${CMAKE_MATCH_1}")
string(REGEX MATCH "True peak:[ \n]+Peak:[ ]+(-?[0-9.inf]+) dBFS" _ "${r128}")
set(pico "${CMAKE_MATCH_1}")
message("loudness integrado: ${lufs} LUFS; pico verdadero: ${pico} dBTP")
if(lufs STREQUAL "" OR lufs LESS -17 OR lufs GREATER -15)
    message(FATAL_ERROR "loudness integrado ${lufs} LUFS fuera de [-17, -15]")
endif()
if(pico STREQUAL "" OR pico GREATER -1)
    message(FATAL_ERROR "pico verdadero ${pico} dBTP por encima de -1")
endif()

# Alineacion: cada cue empieza justo antes de su voz (donde termina un
# silencio).
execute_process(COMMAND ${FFMPEG} -hide_banner -nostdin -i ${mp4} -map 0:a -af silencedetect=n=-45dB:d=0.1
                        -f null -
                ERROR_VARIABLE silencios)
string(REGEX MATCHALL "silence_end: [0-9.]+" finales "${silencios}")
file(STRINGS "${DIR}/voz.srt" lineas_srt REGEX "^[0-9:,]+ --> ")
set(revisados 0)
foreach(l IN LISTS lineas_srt)
    string(REGEX MATCH "^([0-9]+):([0-9]+):([0-9]+),([0-9]+)" _ "${l}")
    math(EXPR cue_ms "((${CMAKE_MATCH_1} * 60 + ${CMAKE_MATCH_2}) * 60 + ${CMAKE_MATCH_3}) * 1000 + ${CMAKE_MATCH_4}")
    if(cue_ms LESS 100)
        continue() # sin silencio previo que detectar
    endif()
    set(mejor 999999)
    foreach(f IN LISTS finales)
        string(REGEX MATCH "([0-9]+)\\.([0-9]+)" _ "${f}")
        string(SUBSTRING "${CMAKE_MATCH_2}000" 0 3 frac)
        math(EXPR fin_ms "${CMAKE_MATCH_1} * 1000 + ${frac}")
        # La voz empieza 50-65 ms despues del cue (adelanto de la marca o
        # cabeza del bloque); +-1 cuadro de cuantizacion.
        math(EXPR d "${fin_ms} - ${cue_ms} - 57")
        if(d LESS 0)
            math(EXPR d "-${d}")
        endif()
        if(d LESS mejor)
            set(mejor ${d})
        endif()
    endforeach()
    if(mejor GREATER 50)
        message(FATAL_ERROR "el cue \"${l}\" no coincide con el inicio de su clip (desfase ${mejor} ms)")
    endif()
    math(EXPR revisados "${revisados} + 1")
endforeach()
if(revisados EQUAL 0)
    message(FATAL_ERROR "no se reviso ningun cue")
endif()
message("OK con voz: ${revisados} cues alineados; ${lufs} LUFS; pico ${pico} dBTP; video ${dv} s, audio ${da} s")

# Muestra montada (D12): solo dos escenas, con voz y subtitulos.
execute_process(
    COMMAND ${HERRAMIENTA} --guion ${GUION} --salida ${DIR} --nombre muestra --offline --voz ${cache}
            --motor-voz prueba --escenas menu-bar,explorador/facturas
    RESULT_VARIABLE codigo OUTPUT_VARIABLE salida ERROR_VARIABLE errores)
if(NOT codigo EQUAL 0)
    message(FATAL_ERROR "la muestra montada fallo:\n${salida}${errores}")
endif()
file(READ "${DIR}/muestra.linea-tiempo.json" lt)
string(JSON n LENGTH "${lt}" capitulos)
string(JSON total GET "${lt}" totalCuadros)
file(STRINGS "${DIR}/muestra.srt" cues REGEX " --> ")
list(LENGTH cues ncues)
execute_process(COMMAND ${FFPROBE} -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames
                        -of csv=p=0 ${DIR}/muestra.mp4
                OUTPUT_VARIABLE cuadros OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT n EQUAL 1 OR NOT cuadros EQUAL total OR NOT ncues EQUAL 4)
    message(FATAL_ERROR "muestra: ${n} capitulos, ${cuadros}/${total} cuadros, ${ncues} cues (se esperaban 1, iguales y 4)")
endif()
message("OK muestra montada: 2 escenas, ${total} cuadros, ${ncues} cues")

