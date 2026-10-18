# Pruebas de la musica de fondo (R4, 08-audio-musica.md), sin sesion grafica:
# --solo-audio escribe la pista de voz (antes de sumar) y la final. Voz del
# motor de prueba (sin red ni clave).
# Variables: CASO, VOZ (satcfdi_video_voz), GRABADOR (satcfdi_video_tutorial),
# GUION (guion de ejemplo), DIR.

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
set(cache "${DIR}/cache")

# Bloque "musica" con los valores de la especificacion salvo `extra`
# (pares "clave": valor que reemplazan o agregan campos).
function(guion_con_musica salida contenido)
    file(READ "${GUION}" g)
    string(REPLACE "\"cuadrosTitulo\": 75," "\"cuadrosTitulo\": 75,\n  \"musica\": ${contenido}," g "${g}")
    if(NOT g MATCHES "\"musica\"")
        message(FATAL_ERROR "no se pudo agregar \"musica\" al guion de prueba")
    endif()
    file(WRITE "${salida}" "${g}")
endfunction()

function(grabador prefijo)
    execute_process(COMMAND ${GRABADOR} ${ARGN}
                    RESULT_VARIABLE codigo OUTPUT_VARIABLE salida ERROR_VARIABLE errores)
    message("$ satcfdi_video_tutorial ${ARGN}\n${salida}${errores}")
    set(${prefijo}_codigo "${codigo}" PARENT_SCOPE)
    set(${prefijo}_salida "${salida}${errores}" PARENT_SCOPE)
endfunction()

function(exigir texto patron)
    if(NOT texto MATCHES "${patron}")
        message(FATAL_ERROR "no aparece \"${patron}\"")
    endif()
endfunction()

# Pista de audio sin grabar: <prefijo>_voz / <prefijo>_pista (sha256).
function(audio prefijo guion dir)
    grabador(r --guion ${guion} --voz ${cache} --motor-voz prueba --solo-audio ${dir} ${ARGN})
    if(NOT r_codigo EQUAL 0)
        message(FATAL_ERROR "--solo-audio fallo con ${guion}")
    endif()
    string(REGEX MATCH "voz.s16 sha256 ([0-9a-f]+); pista.s16 sha256 ([0-9a-f]+)" _ "${r_salida}")
    set(${prefijo}_voz "${CMAKE_MATCH_1}" PARENT_SCOPE)
    set(${prefijo}_pista "${CMAKE_MATCH_2}" PARENT_SCOPE)
    set(${prefijo}_salida "${r_salida}" PARENT_SCOPE)
endfunction()

# Musica de la prueba: la voz del motor de prueba es un tono con poca energia
# en 1-4 kHz (no es una voz real); -43 LUFS deja el margen exigido.
set(musica_prueba "{\"activa\": true, \"preset\": \"pad-fiscal-v1\", \"nivelLufs\": -43, \"duckDb\": -6, \"ataqueMs\": 150, \"retencionMs\": 250, \"liberacionMs\": 1000, \"entradaMs\": 2000, \"salidaMs\": 3000}")

if(CASO STREQUAL "configuracion")
    # Tipos, campos conocidos y rangos; fundidos compatibles con la duracion.
    set(invalidos
        "{\"activa\": true, \"volumen\": 1}|clave desconocida \"volumen\""
        "{\"activa\": \"si\"}|\"activa\" debe ser true o false"
        "{\"activa\": true, \"preset\": \"otro\"}|\"preset\" solo admite"
        "{\"activa\": true, \"nivelLufs\": -50}|\"nivelLufs\" debe ser un numero entre -45 y -30"
        "{\"activa\": true, \"nivelLufs\": \"-34\"}|\"nivelLufs\" debe ser un numero"
        "{\"activa\": true, \"duckDb\": -3}|\"duckDb\" debe ser un numero entre -12 y -4"
        "{\"activa\": true, \"ataqueMs\": 20}|\"ataqueMs\" debe ser un entero entre 50 y 300"
        "{\"activa\": true, \"retencionMs\": 99.5}|\"retencionMs\" debe ser un entero entre 100 y 500"
        "{\"activa\": true, \"liberacionMs\": 2500}|\"liberacionMs\" debe ser un entero entre 500 y 2000"
        "{\"activa\": true, \"entradaMs\": 6000}|\"entradaMs\" debe ser un entero entre 500 y 5000"
        "{\"activa\": true, \"salidaMs\": 100}|\"salidaMs\" debe ser un entero entre 500 y 5000"
        "[true]|musica: debe ser un objeto")
    set(i 0)
    foreach(par IN LISTS invalidos)
        string(FIND "${par}" "|" corte)
        string(SUBSTRING "${par}" 0 ${corte} contenido)
        math(EXPR desde "${corte} + 1")
        string(SUBSTRING "${par}" ${desde} -1 esperado)
        guion_con_musica(${DIR}/invalido-${i}.json "${contenido}")
        grabador(v --guion ${DIR}/invalido-${i}.json --validar)
        if(v_codigo EQUAL 0)
            message(FATAL_ERROR "se acepto \"musica\": ${contenido}")
        endif()
        string(FIND "${v_salida}" "${esperado}" pos)
        if(pos LESS 0)
            message(FATAL_ERROR "\"musica\": ${contenido}: no aparece \"${esperado}\"")
        endif()
        math(EXPR i "${i} + 1")
    endforeach()
    # Valida: imprime la configuracion. Fundidos 2 + 3 s no caben en una
    # escena de 4.8 s.
    guion_con_musica(${DIR}/valido.json "${musica_prueba}")
    grabador(v --guion ${DIR}/valido.json --validar)
    if(NOT v_codigo EQUAL 0)
        message(FATAL_ERROR "se rechazo la musica valida")
    endif()
    exigir("${v_salida}" "musica: pad-fiscal-v1, cama -43.0 LUFS, duck -6.0 dB")
    grabador(v --guion ${DIR}/valido.json --validar --escenas perfiles/perfil)
    if(v_codigo EQUAL 0)
        message(FATAL_ERROR "se aceptaron fundidos de 5 s en una escena de 4.8 s")
    endif()
    exigir("${v_salida}" "musica: los fundidos \\(2000 \\+ 3000 ms\\) no caben en 4.80 s")
    message("OK configuracion: ${i} configuraciones invalidas rechazadas; fundidos contra la duracion")

elseif(CASO STREQUAL "mezcla")
    execute_process(COMMAND ${CMAKE_COMMAND} -E env --unset=OPENAI_API_KEY ${VOZ} --guion ${GUION} --cache ${cache}
                            --motor prueba
                    RESULT_VARIABLE codigo OUTPUT_QUIET ERROR_QUIET)
    if(NOT codigo EQUAL 0)
        message(FATAL_ERROR "el paso de voz de prueba fallo")
    endif()
    # 1) Sin "musica" y con "activa": false la pista es exactamente la voz.
    audio(sin ${GUION} ${DIR}/sin)
    if(NOT sin_voz STREQUAL sin_pista OR sin_voz STREQUAL "")
        message(FATAL_ERROR "sin musica la pista difiere de la voz")
    endif()
    guion_con_musica(${DIR}/inactiva.json "{\"activa\": false, \"nivelLufs\": -40}")
    audio(inactiva ${DIR}/inactiva.json ${DIR}/inactiva)
    if(NOT inactiva_pista STREQUAL sin_pista)
        message(FATAL_ERROR "con \"activa\": false la pista cambio")
    endif()
    # 2) Con musica: voz identica antes de sumar; dos generaciones (caches
    #    distintos) dan el mismo PCM; la tercera sale del cache.
    guion_con_musica(${DIR}/musica.json "${musica_prueba}")
    audio(a ${DIR}/musica.json ${DIR}/a --cache-musica ${DIR}/musica-1)
    audio(b ${DIR}/musica.json ${DIR}/b --cache-musica ${DIR}/musica-2)
    audio(c ${DIR}/musica.json ${DIR}/c --cache-musica ${DIR}/musica-1)
    if(NOT a_voz STREQUAL sin_pista)
        message(FATAL_ERROR "la voz cambio al activar la musica")
    endif()
    if(a_pista STREQUAL sin_pista)
        message(FATAL_ERROR "la musica no llego a la pista")
    endif()
    if(NOT a_pista STREQUAL b_pista OR NOT a_pista STREQUAL c_pista)
        message(FATAL_ERROR "la musica no es determinista (${a_pista} / ${b_pista} / ${c_pista})")
    endif()
    exigir("${a_salida}" "musica: generada")
    exigir("${b_salida}" "musica: generada")
    exigir("${c_salida}" "musica: del cache")
    # 3) Duracion exacta: totalCuadros * 1600 muestras por canal.
    string(REGEX MATCH "audio: ([0-9]+) muestras" _ "${a_salida}")
    math(EXPR bytes "${CMAKE_MATCH_1} * 4")
    foreach(f ${DIR}/a/voz.s16 ${DIR}/a/pista.s16)
        file(SIZE "${f}" tam)
        if(NOT tam EQUAL bytes)
            message(FATAL_ERROR "${f}: ${tam} bytes; se esperaban ${bytes}")
        endif()
    endforeach()
    # 4) Niveles y margen (la herramienta falla si no se cumplen; aqui se
    #    comprueba ademas lo impreso).
    string(REGEX MATCH "mezcla (-?[0-9.]+) LUFS" _ "${a_salida}")
    set(lufs "${CMAKE_MATCH_1}")
    if(lufs LESS -16.2 OR lufs GREATER -15.8)
        message(FATAL_ERROR "mezcla ${lufs} LUFS; se esperaba -16 +-0.2")
    endif()
    string(REGEX MATCH "margen >= 20 dB en ([0-9.]+) % de ([0-9]+) ventanas" _ "${a_salida}")
    if(CMAKE_MATCH_1 LESS 95 OR CMAKE_MATCH_2 LESS 50)
        message(FATAL_ERROR "margen voz/musica: ${CMAKE_MATCH_1} % de ${CMAKE_MATCH_2} ventanas")
    endif()
    # Cierre: la musica no se recupera tras la ultima frase.
    exigir("${a_salida}" "cierre: musica a -6.0 dB hasta el final")
    # 5) Musica demasiado alta: falla con el margen y lo que falta.
    guion_con_musica(${DIR}/alta.json "{\"activa\": true, \"nivelLufs\": -30, \"duckDb\": -4}")
    grabador(alta --guion ${DIR}/alta.json --voz ${cache} --motor-voz prueba --solo-audio ${DIR}/alta
             --cache-musica ${DIR}/musica-1)
    if(alta_codigo EQUAL 0)
        message(FATAL_ERROR "se acepto una musica sin margen bajo la voz")
    endif()
    exigir("${alta_salida}" "margen voz/musica insuficiente")
    message("OK mezcla: voz identica, musica determinista (${a_pista}), ${lufs} LUFS, duracion exacta")

else()
    message(FATAL_ERROR "CASO desconocido: ${CASO}")
endif()
