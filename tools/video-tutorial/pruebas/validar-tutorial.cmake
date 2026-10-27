# T024.4: valida guion-tutorial.json. Los presupuestos se fijan con la voz
# real (corte 2, generacion del lote): con la cache de voz completa se valida
# con --voz (presupuestos incluidos); sin cache, la estimacion por caracteres
# no sirve para los presupuestos y se valida el resto del guion (reglas del
# tutorial, acciones, datos) con todas sus escenas en --escenas.
# Variables: HERRAMIENTA, GUION, CACHE.

if(EXISTS "${CACHE}/actual.json")
    execute_process(COMMAND ${HERRAMIENTA} --guion ${GUION} --validar --voz ${CACHE}
                    RESULT_VARIABLE codigo OUTPUT_VARIABLE salida ERROR_VARIABLE errores)
    message("${salida}${errores}")
    if(NOT codigo EQUAL 0)
        message(FATAL_ERROR "el guion no valida con la voz de ${CACHE}")
    endif()
    return()
endif()
file(READ ${GUION} json)
string(JSON ncap LENGTH "${json}" capitulos)
set(ids "")
math(EXPR ultimo "${ncap} - 1")
foreach(c RANGE ${ultimo})
    string(JSON idc GET "${json}" capitulos ${c} id)
    string(JSON nesc LENGTH "${json}" capitulos ${c} escenas)
    math(EXPR ue "${nesc} - 1")
    foreach(e RANGE ${ue})
        string(JSON ide GET "${json}" capitulos ${c} escenas ${e} id)
        list(APPEND ids "${idc}/${ide}")
    endforeach()
endforeach()
list(JOIN ids "," escenas)
execute_process(COMMAND ${HERRAMIENTA} --guion ${GUION} --validar --escenas ${escenas}
                RESULT_VARIABLE codigo OUTPUT_VARIABLE salida ERROR_VARIABLE errores)
message("${salida}${errores}")
if(NOT codigo EQUAL 0)
    message(FATAL_ERROR "el guion no valida")
endif()
message("sin cache de voz en ${CACHE}: presupuestos no comprobados")
