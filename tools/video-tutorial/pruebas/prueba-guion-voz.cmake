# Validacion de la raiz "voz" del guion (T017 D9; 09-calidad-cierre.md): tipo
# objeto, claves conocidas y tipos. Un error no cae a valores por omision.
# Variables: GRABADOR (satcfdi_video_tutorial), GUION (guion de ejemplo), DIR.

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
file(READ "${GUION}" base)

set(casos
    "\"onyx\"|voz: debe ser un objeto"
    "{\"vos\": \"onyx\"}|voz: clave desconocida \"vos\""
    "{\"voz\": 3}|voz: \"voz\" debe ser un texto no vacio"
    "{\"modelo\": \"  \"}|voz: \"modelo\" debe ser un texto no vacio"
    "{\"instrucciones\": [\"x\"]}|voz: \"instrucciones\" debe ser un texto no vacio"
    "{\"formato\": \"mp3\"}|voz: \"formato\" solo admite \"wav\""
    "{\"velocidad\": \"1\"}|voz: \"velocidad\" debe ser un numero entre 0.25 y 4"
    "{\"velocidad\": 5}|voz: \"velocidad\" debe ser un numero entre 0.25 y 4"
    "{\"modelo\": \"gpt-4o-mini-tts\", \"voz\": \"onyx\", \"instrucciones\": \"x\", \"formato\": \"wav\", \"velocidad\": 1.0}|OK guion")
set(i 0)
foreach(par IN LISTS casos)
    string(FIND "${par}" "|" corte)
    string(SUBSTRING "${par}" 0 ${corte} contenido)
    math(EXPR desde "${corte} + 1")
    string(SUBSTRING "${par}" ${desde} -1 esperado)
    string(REPLACE "\"cuadrosTitulo\": 75," "\"cuadrosTitulo\": 75,\n  \"voz\": ${contenido}," g "${base}")
    if(g STREQUAL base)
        message(FATAL_ERROR "no se pudo agregar \"voz\" al guion de prueba")
    endif()
    file(WRITE "${DIR}/guion-${i}.json" "${g}")
    execute_process(COMMAND ${GRABADOR} --guion ${DIR}/guion-${i}.json --validar
                    RESULT_VARIABLE codigo OUTPUT_VARIABLE salida ERROR_VARIABLE errores)
    message("voz: ${contenido} -> codigo ${codigo}: ${salida}${errores}")
    string(FIND "${salida}${errores}" "${esperado}" pos)
    if(pos LESS 0)
        message(FATAL_ERROR "\"voz\": ${contenido}: no aparece \"${esperado}\"")
    endif()
    if(esperado STREQUAL "OK guion")
        if(NOT codigo EQUAL 0)
            message(FATAL_ERROR "se rechazo una \"voz\" valida")
        endif()
    elseif(codigo EQUAL 0)
        message(FATAL_ERROR "se acepto \"voz\": ${contenido}")
    endif()
    math(EXPR i "${i} + 1")
endforeach()
message("OK voz: ${i} casos (8 invalidos rechazados, 1 valido aceptado)")
