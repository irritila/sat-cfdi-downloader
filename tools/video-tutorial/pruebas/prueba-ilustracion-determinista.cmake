# T024.4 (D16): una ilustracion reducida (selectorArchivos en 960x540, escala
# 0.975) debe dar los mismos pixeles en dos corridas. Antes el texto reducido
# se muestreaba del atlas de glifos y la columna del borde de algunos glifos
# variaba entre corridas; ahora la ilustracion se dibuja en una capa a escala
# 1 (Superposicion.qml). Compara los PNG crudos (no el MP4) de la escena
# perfiles/registrar-efirma del guion de ejemplo en modo rapido.
# Variables: HERRAMIENTA (lista), GUION, SALIDA. Sin sesion grafica: 77.

set(escena "registrar-efirma")
file(REMOVE_RECURSE ${SALIDA})
file(MAKE_DIRECTORY ${SALIDA})

function(grabar nombre rango)
    set(entorno "")
    if(NOT rango STREQUAL "")
        set(entorno ${CMAKE_COMMAND} -E env SATCFDI_VIDEO_DEPURAR_CUADROS=1 SATCFDI_VIDEO_DEPURAR_RANGO=${rango})
    endif()
    execute_process(COMMAND ${entorno} ${HERRAMIENTA} --guion ${GUION} --salida ${SALIDA} --nombre ${nombre}
                            --rapido --offline
                    RESULT_VARIABLE codigo OUTPUT_VARIABLE salida ERROR_VARIABLE errores)
    if(codigo EQUAL 77)
        message("SKIP: sin sesion grafica")
        cmake_language(EXIT 77)
    endif()
    if(NOT codigo EQUAL 0)
        message(FATAL_ERROR "la grabacion ${nombre} termino con codigo ${codigo}\n${salida}${errores}")
    endif()
endfunction()

# 1) Rango de cuadros de la escena (linea de tiempo de una corrida previa).
grabar(plan "")
file(READ ${SALIDA}/plan.linea-tiempo.json json)
string(JSON ncap LENGTH "${json}" capitulos)
math(EXPR ultimoCap "${ncap} - 1")
set(desde "")
foreach(c RANGE ${ultimoCap})
    string(JSON nesc LENGTH "${json}" capitulos ${c} escenas)
    math(EXPR ultimaEsc "${nesc} - 1")
    foreach(e RANGE ${ultimaEsc})
        string(JSON id GET "${json}" capitulos ${c} escenas ${e} id)
        if(id STREQUAL escena)
            string(JSON desde GET "${json}" capitulos ${c} escenas ${e} inicio)
            string(JSON dura GET "${json}" capitulos ${c} escenas ${e} duracion)
        endif()
    endforeach()
endforeach()
if(desde STREQUAL "")
    message(FATAL_ERROR "no esta la escena ${escena} en la linea de tiempo")
endif()
math(EXPR hasta "${desde} + ${dura} - 1")

# 2) Dos corridas con los cuadros crudos de la escena: deben ser identicos.
grabar(a "${desde}-${hasta}")
grabar(b "${desde}-${hasta}")
file(GLOB cuadros RELATIVE ${SALIDA}/a.cuadros ${SALIDA}/a.cuadros/*.png)
list(LENGTH cuadros n)
if(n EQUAL 0)
    message(FATAL_ERROR "no se guardaron cuadros de ${desde} a ${hasta}")
endif()
set(distintos "")
foreach(c ${cuadros})
    file(SHA256 ${SALIDA}/a.cuadros/${c} ha)
    file(SHA256 ${SALIDA}/b.cuadros/${c} hb)
    if(NOT ha STREQUAL hb)
        list(APPEND distintos ${c})
    endif()
endforeach()
if(distintos)
    message(FATAL_ERROR "cuadros distintos entre dos corridas: ${distintos}")
endif()
file(REMOVE_RECURSE ${SALIDA}/a.cuadros ${SALIDA}/b.cuadros)
message("OK ilustracion determinista: ${n} cuadros identicos (${desde}-${hasta}) en dos corridas")
