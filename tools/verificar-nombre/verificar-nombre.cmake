# T019: el nombre anterior de la app no debe aparecer fuera de la lista de
# coincidencias permitidas (permitidos.txt).
#
#   cmake -DRAIZ=<repo> [-DPERMITIDOS=<lista>] -P tools/verificar-nombre/verificar-nombre.cmake
#
# Busca con `git grep` (archivos versionados y no versionados que no esten
# ignorados) los textos "SAT CFDI Downloader" y "SAT-CFDI-Downloader". Cada
# coincidencia debe estar cubierta por una entrada de la lista:
#
#   <glob>                    toda coincidencia en esos archivos
#   <glob> :: <fragmento>     solo las lineas que contienen <fragmento> (literal)
#
# En el glob, `*` no cruza `/` y `**` si. Lineas vacias y las que empiezan con
# `#` se ignoran. Sale con codigo != 0 si alguna coincidencia no esta
# permitida; avisa (sin fallar) de entradas que ya no cubren nada.

cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED RAIZ OR NOT IS_DIRECTORY "${RAIZ}")
    message(FATAL_ERROR "uso: cmake -DRAIZ=<repo> [-DPERMITIDOS=<lista>] -P verificar-nombre.cmake")
endif()
if(NOT DEFINED PERMITIDOS OR PERMITIDOS STREQUAL "")
    set(PERMITIDOS "${CMAKE_CURRENT_LIST_DIR}/permitidos.txt")
endif()
if(NOT EXISTS "${PERMITIDOS}")
    message(FATAL_ERROR "no existe la lista de coincidencias permitidas: ${PERMITIDOS}")
endif()
find_program(GIT git REQUIRED)

# glob -> expresion regular anclada.
function(glob_a_regex glob var)
    set(r "${glob}")
    foreach(c "\\" "." "+" "?" "^" "$" "(" ")" "[" "]" "{" "}" "|")
        string(REPLACE "${c}" "\\${c}" r "${r}")
    endforeach()
    string(REPLACE "**" "@DOBLE@" r "${r}")
    string(REPLACE "*" "[^/]*" r "${r}")
    string(REPLACE "@DOBLE@" ".*" r "${r}")
    set(${var} "^${r}$" PARENT_SCOPE)
endfunction()

# Lista: regex de ruta y fragmento por entrada.
file(STRINGS "${PERMITIDOS}" lineas ENCODING UTF-8)
set(n 0)
foreach(linea IN LISTS lineas)
    string(STRIP "${linea}" linea)
    if(linea STREQUAL "" OR linea MATCHES "^#")
        continue()
    endif()
    string(FIND "${linea}" " :: " corte)
    if(corte GREATER -1)
        string(SUBSTRING "${linea}" 0 ${corte} glob)
        math(EXPR desde "${corte} + 4")
        string(SUBSTRING "${linea}" ${desde} -1 fragmento)
    else()
        set(glob "${linea}")
        set(fragmento "")
    endif()
    string(STRIP "${glob}" glob)
    glob_a_regex("${glob}" regex)
    set(entrada_${n}_regex "${regex}")
    set(entrada_${n}_fragmento "${fragmento}")
    set(entrada_${n}_texto "${linea}")
    set(entrada_${n}_usos 0)
    math(EXPR n "${n} + 1")
endforeach()

execute_process(COMMAND ${GIT} -C "${RAIZ}" grep -n -I --untracked -F
                        -e "SAT CFDI Downloader" -e "SAT-CFDI-Downloader"
                OUTPUT_VARIABLE salida RESULT_VARIABLE r ERROR_VARIABLE err)
# git grep: 0 = hubo coincidencias, 1 = ninguna, otro = error.
if(NOT r EQUAL 0 AND NOT r EQUAL 1)
    message(FATAL_ERROR "git grep fallo (${r}): ${err}")
endif()

string(REPLACE ";" "\;" salida "${salida}")
string(REPLACE "\n" ";" filas "${salida}")
set(no_permitidas 0)
set(total 0)
foreach(fila IN LISTS filas)
    if(fila STREQUAL "")
        continue()
    endif()
    math(EXPR total "${total} + 1")
    string(FIND "${fila}" ":" c1)
    string(SUBSTRING "${fila}" 0 ${c1} ruta)
    math(EXPR d "${c1} + 1")
    string(SUBSTRING "${fila}" ${d} -1 resto)
    string(FIND "${resto}" ":" c2)
    math(EXPR d "${c2} + 1")
    string(SUBSTRING "${resto}" ${d} -1 contenido)
    set(cubierta FALSE)
    if(n GREATER 0)
        math(EXPR ultimo "${n} - 1")
        foreach(i RANGE ${ultimo})
            if(ruta MATCHES "${entrada_${i}_regex}")
                set(ok TRUE)
                if(NOT entrada_${i}_fragmento STREQUAL "")
                    string(FIND "${contenido}" "${entrada_${i}_fragmento}" pos)
                    if(pos EQUAL -1)
                        set(ok FALSE)
                    endif()
                endif()
                if(ok)
                    set(cubierta TRUE)
                    math(EXPR entrada_${i}_usos "${entrada_${i}_usos} + 1")
                    break()
                endif()
            endif()
        endforeach()
    endif()
    if(NOT cubierta)
        math(EXPR no_permitidas "${no_permitidas} + 1")
        message("NO PERMITIDA: ${fila}")
    endif()
endforeach()

if(n GREATER 0)
    math(EXPR ultimo "${n} - 1")
    foreach(i RANGE ${ultimo})
        if(entrada_${i}_usos EQUAL 0)
            message("aviso: la entrada '${entrada_${i}_texto}' ya no cubre ninguna coincidencia")
        endif()
    endforeach()
endif()

if(no_permitidas GREATER 0)
    message(FATAL_ERROR "nombre anterior: ${no_permitidas} de ${total} coincidencias fuera de la lista permitida (${PERMITIDOS})")
endif()
message("OK nombre: ${total} coincidencias, todas permitidas")
