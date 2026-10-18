# Prueba de verificar-nombre.cmake sobre un repositorio git temporal.
# Variables: SCRIPT, DIR.

find_program(GIT git REQUIRED)
file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}/repo/historico" "${DIR}/repo/src")
set(r "${DIR}/repo")
file(WRITE "${r}/README.md" "# Archivo Fiscal\n")
file(WRITE "${r}/historico/viejo.md" "Antes se llamaba SAT CFDI Downloader.\n")
file(WRITE "${r}/src/app.cpp" "setApplicationName(\"SAT CFDI Downloader\"); // interno\nauto titulo = \"SAT CFDI Downloader\";\n")
file(WRITE "${r}/.gitignore" "ignorado.txt\n")
file(WRITE "${r}/ignorado.txt" "SAT-CFDI-Downloader\n")
execute_process(COMMAND ${GIT} init -q WORKING_DIRECTORY "${r}" RESULT_VARIABLE x)
execute_process(COMMAND ${GIT} add README.md historico src .gitignore WORKING_DIRECTORY "${r}")

file(WRITE "${DIR}/incompleta.txt" "historico/**\n")
file(WRITE "${DIR}/completa.txt" "# comentario\nhistorico/**\nsrc/*.cpp :: setApplicationName\nsrc/*.cpp :: auto titulo\n")
file(WRITE "${DIR}/con-sobrante.txt" "historico/**\nsrc/** :: SAT CFDI Downloader\nno/existe/** \n")

function(verificar lista esperado patron)
    execute_process(COMMAND ${CMAKE_COMMAND} -DRAIZ=${r} -DPERMITIDOS=${lista} -P ${SCRIPT}
                    RESULT_VARIABLE codigo OUTPUT_VARIABLE o ERROR_VARIABLE e)
    message("${lista}: codigo ${codigo}\n${o}${e}")
    if(esperado STREQUAL "pasa" AND NOT codigo EQUAL 0)
        message(FATAL_ERROR "debia pasar con ${lista}")
    elseif(esperado STREQUAL "falla" AND codigo EQUAL 0)
        message(FATAL_ERROR "debia fallar con ${lista}")
    endif()
    string(FIND "${o}${e}" "${patron}" pos)
    if(pos LESS 0)
        message(FATAL_ERROR "no aparece \"${patron}\" con ${lista}")
    endif()
endfunction()

# Fuera de la lista: la segunda linea de src/app.cpp (el fragmento no la cubre).
verificar(${DIR}/incompleta.txt falla "NO PERMITIDA: src/app.cpp:1:")
verificar(${DIR}/completa.txt pasa "OK nombre: 3 coincidencias, todas permitidas")
# Una entrada que ya no cubre nada solo avisa.
verificar(${DIR}/con-sobrante.txt pasa "aviso: la entrada 'no/existe/**' ya no cubre")
# Un archivo nuevo sin versionar (no ignorado) tambien cuenta.
file(WRITE "${r}/nuevo.txt" "SAT-CFDI-Downloader\n")
verificar(${DIR}/completa.txt falla "NO PERMITIDA: nuevo.txt:1:")
# (ignorado.txt nunca se cuenta: la lista completa da 3 coincidencias, no 4).
message("OK verificar-nombre: lista incompleta, completa, sobrante, sin versionar e ignorado")
