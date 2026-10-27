# T024.4: reglas del guion del tutorial y de las acciones nuevas, sin grabar.
# Variables: HERRAMIENTA (satcfdi_video_tutorial) y DIR (pruebas/).
function(validar archivo esperado)
    execute_process(COMMAND ${HERRAMIENTA} --guion ${DIR}/${archivo} --validar
                    RESULT_VARIABLE codigo OUTPUT_VARIABLE salida ERROR_VARIABLE salida)
    if(esperado STREQUAL "OK")
        if(NOT codigo EQUAL 0)
            message(FATAL_ERROR "${archivo} deberia ser valido:\n${salida}")
        endif()
    elseif(codigo EQUAL 0 OR NOT salida MATCHES "${esperado}")
        message(FATAL_ERROR "${archivo} deberia fallar con '${esperado}' y dio (${codigo}):\n${salida}")
    endif()
endfunction()

# 7 capitulos y 5:00-8:00 (9000-14400 cuadros).
validar(tutorial-reglas-ok.json OK)
validar(tutorial-reglas-seis.json "exactamente 7 capitulos \\(tiene 6\\)")
validar(tutorial-reglas-corto.json "duracion total 6405 cuadros .*fuera de \\[9000, 14400\\]")
validar(tutorial-reglas-largo.json "duracion total 16905 cuadros .*fuera de \\[9000, 14400\\]")
# T024 D3: vocabulario retirado en texto y en voz, sin importar mayusculas.
validar(tutorial-reglas-texto.json "escena 'c3/e': la linea 'l1' usa \"solicitudes\" en texto")
validar(tutorial-reglas-voz.json "escena 'c5/e': la linea 'l1' usa \"paquete\" en voz")
# tecla con modificadores: orden Cmd, Shift, Opcion, Ctrl.
validar(guion-tecla-orden.json "fuera de orden en \"Shift\\+Cmd\\+E\"")
# avanzarMes solo sobre meses de datos.meses.
validar(guion-mes-no-declarado.json "2026-09")
message("OK reglas del tutorial y acciones T024.4")
