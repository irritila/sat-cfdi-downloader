# T024.2: las aserciones del manifiesto detectan un estado visual equivocado
# pero estable (lo que el SHA-256 de dos corridas no detecta). Copia el
# manifiesto pidiendo en 18-franja-preparando el titular de un mes Disponibles
# y exige que la captura falle nombrando la asercion.
#
# cmake -DHERRAMIENTA=<satcfdi_manual_capturas> -DMANIFIESTO=<escenarios.json>
#       -DDIR=<directorio de trabajo> -P asercion-negativa.cmake
foreach(_v HERRAMIENTA MANIFIESTO DIR)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "falta -D${_v}")
    endif()
endforeach()
file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
file(READ "${MANIFIESTO}" _json)
set(_correcto "\"Preparando las facturas de septiembre 2026\"")
set(_equivocado "\"Facturas de septiembre 2026 completas\"")
string(FIND "${_json}" "${_correcto}" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR "el manifiesto ya no declara ${_correcto}; actualiza esta prueba")
endif()
string(REPLACE "${_correcto}" "${_equivocado}" _json "${_json}")
file(WRITE "${DIR}/escenarios.json" "${_json}")
execute_process(
    COMMAND "${HERRAMIENTA}" --salida "${DIR}" --solo 18-franja-preparando.png --manifiesto "${DIR}/escenarios.json"
    RESULT_VARIABLE _rc ERROR_VARIABLE _err OUTPUT_VARIABLE _out)
if(_rc EQUAL 0)
    message(FATAL_ERROR "la captura paso con una asercion equivocada:\n${_out}")
endif()
if(NOT _err MATCHES "18-franja-preparando.png: asercion fallida: falta el texto \"Facturas de septiembre 2026 completas\"")
    message(FATAL_ERROR "la captura fallo sin nombrar la asercion (rc ${_rc}):\n${_err}")
endif()
if(EXISTS "${DIR}/18-franja-preparando.png")
    message(FATAL_ERROR "se escribio el PNG pese a la asercion fallida")
endif()
message(STATUS "asercion negativa: la captura falla y no se escribe")
