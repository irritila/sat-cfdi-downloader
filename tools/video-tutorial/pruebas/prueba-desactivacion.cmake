# T022 D7: otra app toma el foco de macOS entre el press y el release de un
# clic. Se simula con SATCFDI_VIDEO_DIAGNOSTICO_DESACTIVAR=N (la ventana
# pierde el foco de Qt tras el press del N-esimo clic con efecto, igual que
# con un cambio de app real).
# - corregida: FiltroEntradaReal descarta WindowDeactivate; el clic conserva
#   su efecto y la grabacion termina bien.
# - detectada: con el filtro desactivado
#   (SATCFDI_VIDEO_DIAGNOSTICO_PERMITIR_DESACTIVAR=1) el press pierde el
#   agarre; la grabacion falla antes del release con traza y PNG, sin
#   reintento.
# Sin sesion grafica: 77 (SKIP). Variables: HERRAMIENTA (lista), GUION, SALIDA,
# CASO (corregida|detectada), CLIC.

set(nombre "desactivacion-${CASO}")
file(REMOVE "${SALIDA}/${nombre}.fallo.png")
set(entorno "SATCFDI_VIDEO_DIAGNOSTICO_DESACTIVAR=${CLIC}")
if(CASO STREQUAL "detectada")
    list(APPEND entorno "SATCFDI_VIDEO_DIAGNOSTICO_PERMITIR_DESACTIVAR=1")
endif()
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env ${entorno}
            ${HERRAMIENTA} --guion ${GUION} --salida ${SALIDA} --nombre ${nombre} --rapido --offline
    RESULT_VARIABLE codigo OUTPUT_VARIABLE salida ERROR_VARIABLE errores)
message("${salida}${errores}")
if(codigo EQUAL 77)
    message("SKIP: sin sesion grafica")
    cmake_language(EXIT 77)
endif()

if(CASO STREQUAL "corregida")
    if(NOT codigo EQUAL 0)
        message(FATAL_ERROR "con la ventana desactivada tras el press, la grabacion termino con codigo ${codigo}")
    endif()
    return()
endif()

if(NOT codigo EQUAL 1)
    message(FATAL_ERROR "sin el filtro se esperaba el codigo 1 y termino con ${codigo}")
endif()
foreach(esperado
        "perdio el agarre del raton antes del release \\(sin reintento\\)"
        "traza: \\[escena '[^']+'\\] antes-release"
        "traza:   mouseGrabberItem=\\(ninguno\\)"
        "ventana en [^;]*${nombre}\\.fallo\\.png")
    if(NOT errores MATCHES "${esperado}")
        message(FATAL_ERROR "falta en la salida: ${esperado}")
    endif()
endforeach()
if(NOT EXISTS "${SALIDA}/${nombre}.fallo.png")
    message(FATAL_ERROR "no se escribio ${SALIDA}/${nombre}.fallo.png")
endif()
message("OK: el agarre perdido se detecta antes del release, con traza y PNG")
