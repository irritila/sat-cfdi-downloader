# T024.2: escaneo de las salidas de satcfdi_manual_capturas (${DIR}: PNG y
# textos/<archivo>.png.txt con los textos visibles de cada captura). Falla si:
# - aparece un RFC distinto de los genericos XAXX010101000 / XEXX010101000;
# - aparece una ruta /Users/<x> con <x> distinto de "usuario";
# - aparece el usuario, el nombre completo, el equipo o el HOME de quien
#   ejecuta (datos reales de la sesion);
# - un PNG lleva metadatos de texto (tEXt, zTXt, iTXt).
# Los textos incluyen los accesibles (Accessible.name/description), que
# VoiceOver lee aunque no se dibujen.
#
# Riesgo residual (documentado, T024.2): un nombre o razon social real que no
# sea de la sesion (p. ej. un cliente escrito a mano en un fake) NO se detecta.
# No hay regla barata sin falsos positivos: los nombres sinteticos son texto
# libre ("Papeleria Ejemplo", "Refacciones Ficticias") y una lista permitida
# exigiria enumerar todo el texto de la UI. La mitigacion es de origen: los
# datos salen solo de tools/fakes-materiales y ServiciosFijos.h, revisados
# como datos sinteticos, y los RFC (que si se detectan) son los genericos.
# Tampoco se detecta texto dibujado que no venga de una propiedad (p. ej. el
# contenido rasterizado del PDF); el PDF se genera del XML sintetico.
#
# cmake -DDIR=<directorio de una corrida> -P escanear-capturas.cmake
# cmake "-DTEXTOS=<archivo;...>" -P escanear-capturas.cmake
#   (T024.2 corte 2: solo archivos de texto, p. ej. los guiones del video y
#   lo que la herramienta del video vuelca sin grabar)
if(DEFINED TEXTOS)
    set(_pngs "")
    set(_textos ${TEXTOS})
    foreach(_f IN LISTS _textos)
        if(NOT EXISTS "${_f}")
            message(FATAL_ERROR "no existe ${_f}")
        endif()
    endforeach()
    list(LENGTH _textos _ntxt)
    set(_npng 0)
elseif(DEFINED DIR)
    file(GLOB _pngs "${DIR}/*.png")
    file(GLOB _textos "${DIR}/textos/*.txt")
    list(LENGTH _pngs _npng)
    list(LENGTH _textos _ntxt)
    if(_npng EQUAL 0 OR NOT _npng EQUAL _ntxt)
        message(FATAL_ERROR "se esperaban PNG y un archivo de textos por PNG en ${DIR} (${_npng} PNG, ${_ntxt} textos)")
    endif()
else()
    message(FATAL_ERROR "falta -DDIR o -DTEXTOS")
endif()

# Identidad real de la sesion (se ignoran valores vacios o muy cortos).
set(_reales "")
if(DEFINED ENV{USER})
    list(APPEND _reales "$ENV{USER}")
endif()
if(DEFINED ENV{HOME})
    list(APPEND _reales "$ENV{HOME}")
endif()
execute_process(COMMAND id -F OUTPUT_VARIABLE _nombre OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
list(APPEND _reales "${_nombre}")
cmake_host_system_information(RESULT _equipo QUERY HOSTNAME)
list(APPEND _reales "${_equipo}")
set(_buscar "")
foreach(_r IN LISTS _reales)
    string(LENGTH "${_r}" _largo)
    if(_largo GREATER 2 AND NOT _r STREQUAL "usuario")
        list(APPEND _buscar "${_r}")
    endif()
endforeach()

set(_rfc "[A-Z&][A-Z&][A-Z&][A-Z&]?[0-9][0-9][0-9][0-9][0-9][0-9][A-Z0-9][A-Z0-9][A-Z0-9]")
set(_hallazgos "")
foreach(_f IN LISTS _textos)
    get_filename_component(_nombreArchivo "${_f}" NAME)
    file(READ "${_f}" _contenido)
    string(REGEX MATCHALL "${_rfc}" _rfcs "${_contenido}")
    foreach(_x IN LISTS _rfcs)
        if(NOT _x STREQUAL "XAXX010101000" AND NOT _x STREQUAL "XEXX010101000")
            list(APPEND _hallazgos "${_nombreArchivo}: RFC no generico '${_x}'")
        endif()
    endforeach()
    string(REGEX MATCHALL "/Users/[^/ \n\"']*" _rutas "${_contenido}")
    foreach(_x IN LISTS _rutas)
        if(NOT _x STREQUAL "/Users/usuario")
            list(APPEND _hallazgos "${_nombreArchivo}: ruta '${_x}'")
        endif()
    endforeach()
    foreach(_x IN LISTS _buscar)
        string(FIND "${_contenido}" "${_x}" _pos)
        if(NOT _pos EQUAL -1)
            list(APPEND _hallazgos "${_nombreArchivo}: dato de la sesion real")
        endif()
    endforeach()
endforeach()
foreach(_f IN LISTS _pngs)
    get_filename_component(_nombreArchivo "${_f}" NAME)
    file(STRINGS "${_f}" _cadenas)
    foreach(_x IN LISTS _buscar)
        string(FIND "${_cadenas}" "${_x}" _pos)
        if(NOT _pos EQUAL -1)
            list(APPEND _hallazgos "${_nombreArchivo}: dato de la sesion real en los bytes del PNG")
        endif()
    endforeach()
    file(READ "${_f}" _hex HEX)
    # tEXt 74455874, zTXt 7a545874, iTXt 69545874 (tipo de chunk PNG).
    foreach(_chunk 74455874 7a545874 69545874)
        string(FIND "${_hex}" "${_chunk}" _pos)
        if(NOT _pos EQUAL -1)
            list(APPEND _hallazgos "${_nombreArchivo}: metadatos de texto en el PNG")
        endif()
    endforeach()
endforeach()
if(_hallazgos)
    list(JOIN _hallazgos "\n  " _texto)
    message(FATAL_ERROR "escaneo de capturas:\n  ${_texto}")
endif()
message(STATUS "escaneo: ${_npng} PNG y ${_ntxt} archivos de texto sin RFC, nombres ni rutas reales")
