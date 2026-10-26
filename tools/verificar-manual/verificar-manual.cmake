# T024.3: verificacion del manual de usuario (sin render, sin Qt).
#
# cmake -DMANUAL=<usuario.md> -DIMAGENES=<dir img> -DMANIFIESTO=<escenarios.json>
#       -P verificar-manual.cmake
#
# Reglas (cada falla se imprime con "FALLA <regla>: ..." y el script termina
# con error si hay alguna):
#  vocabulario  "solicitud(es)" y "paquete(s)" (con y sin acento, cualquier
#               caja, palabra completa) solo dentro del apendice Diagnostico:
#               la seccion Markdown cuyo encabezado contiene "Diagnóstico"
#               (o "Diagnostico"), con sus subsecciones, hasta el siguiente
#               encabezado de su nivel o superior. Debe haber exactamente un
#               apendice. Los encabezados dentro de bloques ``` no cuentan.
#  imagen       cada ![alt](ruta) existe (ruta relativa al manual) y tiene
#               texto alternativo.
#  huerfana     cada PNG de IMAGENES esta referenciado en el manual.
#  manifiesto   cada PNG de IMAGENES esta en el manifiesto de capturas, y cada
#               PNG del manifiesto existe en IMAGENES.
foreach(_v MANUAL IMAGENES MANIFIESTO)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "falta -D${_v}")
    endif()
endforeach()
# Rutas absolutas (file(GLOB) con rutas relativas no encuentra nada en -P).
foreach(_v MANUAL IMAGENES MANIFIESTO)
    get_filename_component(${_v} "${${_v}}" ABSOLUTE)
endforeach()
foreach(_f "${MANUAL}" "${MANIFIESTO}")
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "no existe ${_f}")
    endif()
endforeach()

set(_fallas 0)
macro(_falla regla texto)
    message("FALLA ${regla}: ${texto}")
    math(EXPR _fallas "${_fallas} + 1")
endmacro()

file(READ "${MANUAL}" _texto)
get_filename_component(_dirManual "${MANUAL}" DIRECTORY)

# --- Reglas imagen y huerfana -----------------------------------------------
# ';' en el texto alternativo partiria la lista de enlaces.
string(REPLACE ";" "<pc>" _textoEnlaces "${_texto}")
string(REGEX MATCHALL "!\\[[^]\n]*\\]\\([^)\n]*\\)" _enlaces "${_textoEnlaces}")
set(_referenciadas "")
foreach(_enlace IN LISTS _enlaces)
    string(REGEX REPLACE "^!\\[([^]]*)\\]\\(([^)]*)\\)$" "\\1" _alt "${_enlace}")
    string(REGEX REPLACE "^!\\[([^]]*)\\]\\(([^)]*)\\)$" "\\2" _ruta "${_enlace}")
    # ![alt](ruta "titulo"): solo la ruta.
    string(REGEX REPLACE "[ \t].*$" "" _ruta "${_ruta}")
    string(STRIP "${_alt}" _alt)
    if(_alt STREQUAL "")
        _falla(imagen "sin texto alternativo: ${_ruta}")
    endif()
    if(_ruta MATCHES "\\.png$" AND NOT _ruta MATCHES "^img/[^/]+\\.png$")
        _falla(imagen "PNG fuera de img/: ${_ruta}")
    endif()
    if(NOT EXISTS "${_dirManual}/${_ruta}")
        _falla(imagen "no existe la imagen referenciada: ${_ruta}")
    endif()
    get_filename_component(_absoluta "${_dirManual}/${_ruta}" ABSOLUTE)
    list(APPEND _referenciadas "${_absoluta}")
endforeach()

file(GLOB _pngs RELATIVE "${IMAGENES}" "${IMAGENES}/*.png")
list(SORT _pngs)
foreach(_png IN LISTS _pngs)
    get_filename_component(_absoluta "${IMAGENES}/${_png}" ABSOLUTE)
    if(NOT _absoluta IN_LIST _referenciadas)
        _falla(huerfana "${_png} no esta referenciado en el manual")
    endif()
endforeach()

# --- Regla manifiesto ---------------------------------------------------------
file(READ "${MANIFIESTO}" _json)
string(JSON _n LENGTH "${_json}" escenarios)
set(_declarados "")
if(_n GREATER 0)
    math(EXPR _ultimo "${_n} - 1")
    foreach(_i RANGE ${_ultimo})
        string(JSON _archivo GET "${_json}" escenarios ${_i} archivo)
        list(APPEND _declarados "${_archivo}")
    endforeach()
endif()
foreach(_png IN LISTS _pngs)
    if(NOT _png IN_LIST _declarados)
        _falla(manifiesto "${_png} no esta en el manifiesto de capturas")
    endif()
endforeach()
foreach(_archivo IN LISTS _declarados)
    if(NOT EXISTS "${IMAGENES}/${_archivo}")
        _falla(manifiesto "${_archivo} esta en el manifiesto pero no en ${IMAGENES} (regenera con manual_capturas)")
    endif()
endforeach()

# --- Regla vocabulario --------------------------------------------------------
# Lineas como lista de CMake: se protegen ';' y los corchetes (que alteran la
# division de listas); el texto solo se usa para buscar palabras.
string(REPLACE ";" "<pc>" _lineas "${_texto}")
string(REPLACE "[" "<ca>" _lineas "${_lineas}")
string(REPLACE "]" "<cc>" _lineas "${_lineas}")
string(REPLACE "\\" "<bs>" _lineas "${_lineas}")
string(REPLACE "\n" ";" _lineas "${_lineas}")

set(_numero 0)
set(_enCodigo FALSE)
set(_seccion "(antes del primer encabezado)")
set(_nivelApendice 0) # 0 = fuera del apendice
set(_apendices 0)
# Palabra completa: antes y despues, ni letra ni digito. Se comparan en
# minusculas y sin los acentos de estas palabras (TOLOWER solo cambia ASCII, y
# las expresiones de CMake comparan bytes).
set(_palabra "(solicitud(es)?|paquetes?)")
foreach(_linea IN LISTS _lineas)
    math(EXPR _numero "${_numero} + 1")
    if(_linea MATCHES "^[ ]*(```|~~~)")
        if(_enCodigo)
            set(_enCodigo FALSE)
        else()
            set(_enCodigo TRUE)
        endif()
    elseif(NOT _enCodigo AND _linea MATCHES "^(#+)[ \t]+(.*)$")
        string(LENGTH "${CMAKE_MATCH_1}" _nivel)
        set(_titulo "${CMAKE_MATCH_2}")
        set(_seccion "${_titulo}")
        if(_nivelApendice GREATER 0 AND _nivel LESS_EQUAL _nivelApendice)
            set(_nivelApendice 0)
        endif()
        # Solo el apendice exacto "## Apendice: Diagnostico" (con o sin
        # acentos); otro encabezado con "Diagnostico" no abre excepcion.
        string(STRIP "${_titulo}" _tituloLimpio)
        if(_nivelApendice EQUAL 0 AND _nivel EQUAL 2 AND _tituloLimpio MATCHES "^Ap(é|e)ndice: Diagn(ó|o)stico$")
            set(_nivelApendice ${_nivel})
            math(EXPR _apendices "${_apendices} + 1")
            continue()
        endif()
    endif()
    if(_nivelApendice GREATER 0)
        continue()
    endif()
    set(_min "${_linea}")
    foreach(_par "Á>a" "á>a" "É>e" "é>e" "Í>i" "í>i" "Ó>o" "ó>o" "Ú>u" "ú>u" "Ü>u" "ü>u")
        string(REPLACE ">" ";" _par "${_par}")
        list(GET _par 0 _de)
        list(GET _par 1 _a)
        string(REPLACE "${_de}" "${_a}" _min "${_min}")
    endforeach()
    string(TOLOWER "${_min}" _min)
    if(" ${_min} " MATCHES "[^a-z0-9]${_palabra}[^a-z0-9]")
        set(_hallada "${CMAKE_MATCH_1}")
        string(REPLACE "<pc>" ";" _mostrar "${_linea}")
        string(REPLACE "<ca>" "[" _mostrar "${_mostrar}")
        string(REPLACE "<cc>" "]" _mostrar "${_mostrar}")
        string(REPLACE "<bs>" "\\" _mostrar "${_mostrar}")
        _falla(vocabulario "linea ${_numero} (seccion \"${_seccion}\"): \"${_hallada}\" fuera del apendice Diagnóstico: ${_mostrar}")
    endif()
endforeach()
if(NOT _apendices EQUAL 1)
    _falla(vocabulario "se esperaba exactamente un apendice \"## Apéndice: Diagnóstico\" y hay ${_apendices}")
endif()

list(LENGTH _enlaces _nEnlaces)
list(LENGTH _pngs _nPngs)
if(_fallas GREATER 0)
    message(FATAL_ERROR "verificar-manual: ${_fallas} falla(s) en ${MANUAL}")
endif()
message("OK verificar-manual: ${_nEnlaces} imagenes referenciadas, ${_nPngs} PNG en el manifiesto, vocabulario solo en el apendice Diagnóstico")
