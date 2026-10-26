# T024.2: reproducibilidad de las capturas del manual. Ejecuta
# satcfdi_manual_capturas dos veces en directorios limpios (${DIR}/a y
# ${DIR}/b) y compara el SHA-256 de cada PNG del manifiesto. Falla si falta
# una captura, la herramienta falla o algun hash difiere. Escribe
# ${DIR}/sha256.txt con los hashes de la corrida a.
#
# cmake -DHERRAMIENTA=<satcfdi_manual_capturas> -DMANIFIESTO=<escenarios.json>
#       -DDIR=<directorio en el build> -P verificar-capturas.cmake
foreach(_v HERRAMIENTA MANIFIESTO DIR)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "falta -D${_v}")
    endif()
endforeach()

file(READ "${MANIFIESTO}" _json)
string(JSON _n LENGTH "${_json}" escenarios)
if(_n EQUAL 0)
    message(FATAL_ERROR "el manifiesto no tiene escenarios")
endif()
math(EXPR _ultimo "${_n} - 1")
set(_archivos "")
foreach(_i RANGE ${_ultimo})
    string(JSON _a GET "${_json}" escenarios ${_i} archivo)
    list(APPEND _archivos "${_a}")
endforeach()

foreach(_corrida a b)
    file(REMOVE_RECURSE "${DIR}/${_corrida}")
    execute_process(
        COMMAND "${HERRAMIENTA}" --tema claro --salida "${DIR}/${_corrida}" --textos "${DIR}/${_corrida}/textos"
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _salida
        ERROR_VARIABLE _error)
    message(STATUS "corrida ${_corrida}:\n${_salida}")
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "corrida ${_corrida}: la herramienta termino con ${_rc}\n${_error}")
    endif()
endforeach()

set(_diferentes "")
set(_lineas "")
foreach(_a IN LISTS _archivos)
    foreach(_corrida a b)
        if(NOT EXISTS "${DIR}/${_corrida}/${_a}")
            message(FATAL_ERROR "corrida ${_corrida}: falta ${_a}")
        endif()
        file(SHA256 "${DIR}/${_corrida}/${_a}" _sha_${_corrida})
    endforeach()
    string(APPEND _lineas "${_sha_a}  ${_a}\n")
    if(NOT _sha_a STREQUAL _sha_b)
        list(APPEND _diferentes "${_a} (a ${_sha_a}, b ${_sha_b})")
    endif()
endforeach()
file(WRITE "${DIR}/sha256.txt" "${_lineas}")
message(STATUS "SHA-256 (corrida a):\n${_lineas}")
if(_diferentes)
    list(JOIN _diferentes "\n  " _texto)
    message(FATAL_ERROR "capturas con SHA-256 distinto entre dos ejecuciones:\n  ${_texto}")
endif()
list(LENGTH _archivos _total)
message(STATUS "${_total} capturas identicas en dos ejecuciones")
