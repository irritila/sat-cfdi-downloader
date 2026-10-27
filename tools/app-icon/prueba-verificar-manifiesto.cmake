# T020: casos del verificador del manifiesto del icono, sobre copias en DIR.
#
#   cmake -DSCRIPT=<verificar-manifiesto.cmake> -DORIGEN=<repo> -DDIR=<tmp> -P prueba-verificar-manifiesto.cmake
#
# Un caso valido (copia intacta) y uno negativo por regla: fuente cambiada,
# salida cambiada, archivo nuevo en el .icon, capa del .icon distinta de
# branding, manifiesto sin xcode, sin comando o sin minimo-macos, y minimo
# distinto del build.
foreach(_v SCRIPT ORIGEN DIR)
    if(NOT ${_v})
        message(FATAL_ERROR "prueba-verificar-manifiesto: falta -D${_v}")
    endif()
endforeach()

function(_preparar caso salida)
    set(_r "${DIR}/${caso}")
    file(REMOVE_RECURSE "${_r}")
    file(MAKE_DIRECTORY "${_r}/resources/macos" "${_r}/resources/branding")
    file(COPY "${ORIGEN}/resources/macos/AppIcon.icon" "${ORIGEN}/resources/macos/AppIcon-compilado"
              "${ORIGEN}/resources/macos/AppIcon.manifiesto"
         DESTINATION "${_r}/resources/macos")
    file(COPY "${ORIGEN}/resources/branding/icono" DESTINATION "${_r}/resources/branding")
    set(${salida} "${_r}" PARENT_SCOPE)
endfunction()

function(_esperar caso raiz debe_pasar)
    execute_process(COMMAND "${CMAKE_COMMAND}" -DRAIZ=${raiz} -P "${SCRIPT}"
                    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(debe_pasar AND NOT _rc EQUAL 0)
        message(FATAL_ERROR "caso ${caso}: debia pasar\n${_out}${_err}")
    elseif(NOT debe_pasar AND _rc EQUAL 0)
        message(FATAL_ERROR "caso ${caso}: debia fallar")
    endif()
    message(STATUS "caso ${caso}: ok")
endfunction()

_preparar(valido r)
_esperar(valido "${r}" TRUE)

_preparar(fuente r)
file(APPEND "${r}/resources/macos/AppIcon.icon/icon.json" " ")
_esperar(fuente-cambiada "${r}" FALSE)

_preparar(capa-branding r)
file(APPEND "${r}/resources/branding/icono/icono-frente.svg" "\n")
_esperar(capa-branding-cambiada "${r}" FALSE)

_preparar(salida r)
file(APPEND "${r}/resources/macos/AppIcon-compilado/AppIcon.icns" "x")
_esperar(salida-cambiada "${r}" FALSE)

_preparar(nuevo r)
file(WRITE "${r}/resources/macos/AppIcon.icon/Assets/nueva.svg" "<svg/>")
_esperar(archivo-nuevo-en-icon "${r}" FALSE)

_preparar(capa-copia r)
file(APPEND "${r}/resources/macos/AppIcon.icon/Assets/icono-pestana.svg" "\n")
_esperar(capa-copia-cambiada "${r}" FALSE)

function(_quitar_linea raiz prefijo)
    file(STRINGS "${raiz}/resources/macos/AppIcon.manifiesto" _l)
    list(FILTER _l EXCLUDE REGEX "^${prefijo}\t")
    list(JOIN _l "\n" _t)
    file(WRITE "${raiz}/resources/macos/AppIcon.manifiesto" "${_t}\n")
endfunction()

_preparar(sin-xcode r)
_quitar_linea("${r}" xcode)
_esperar(manifiesto-sin-xcode "${r}" FALSE)

_preparar(sin-comando r)
_quitar_linea("${r}" comando)
_esperar(manifiesto-sin-comando "${r}" FALSE)

_preparar(sin-minimo r)
_quitar_linea("${r}" minimo-macos)
_esperar(manifiesto-sin-minimo "${r}" FALSE)

# Minimo distinto del build (-DMINIMO): el icono se compilo para otro macOS.
_preparar(otro-minimo r)
execute_process(COMMAND "${CMAKE_COMMAND}" -DRAIZ=${r} -DMINIMO=1.0 -P "${SCRIPT}"
                RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
if(_rc EQUAL 0)
    message(FATAL_ERROR "caso minimo-distinto-del-build: debia fallar")
endif()
message(STATUS "caso minimo-distinto-del-build: ok")

file(REMOVE_RECURSE "${DIR}")
