# T024.3: prueba de verificar-manual.cmake con manuales, imagenes y
# manifiestos temporales: un caso que pasa y un caso negativo por regla.
# Variables: SCRIPT, DIR.
foreach(_v SCRIPT DIR)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "falta -D${_v}")
    endif()
endforeach()
file(REMOVE_RECURSE "${DIR}")

# Caso base: dos PNG en img/, ambos referenciados y en el manifiesto.
function(preparar caso manual pngs manifiesto)
    set(d "${DIR}/${caso}")
    file(MAKE_DIRECTORY "${d}/img")
    foreach(png IN LISTS pngs)
        file(WRITE "${d}/img/${png}" "")
    endforeach()
    file(WRITE "${d}/usuario.md" "${manual}")
    set(json "{\"escenarios\": [")
    set(sep "")
    foreach(png IN LISTS manifiesto)
        string(APPEND json "${sep}{\"archivo\": \"${png}\"}")
        set(sep ", ")
    endforeach()
    string(APPEND json "]}")
    file(WRITE "${d}/escenarios.json" "${json}")
endfunction()

function(verificar caso esperado patron)
    set(d "${DIR}/${caso}")
    execute_process(COMMAND ${CMAKE_COMMAND} -DMANUAL=${d}/usuario.md -DIMAGENES=${d}/img
                            -DMANIFIESTO=${d}/escenarios.json -P ${SCRIPT}
                    RESULT_VARIABLE codigo OUTPUT_VARIABLE o ERROR_VARIABLE e)
    message("${caso}: codigo ${codigo}\n${o}${e}")
    if(esperado STREQUAL "pasa" AND NOT codigo EQUAL 0)
        message(FATAL_ERROR "${caso}: debia pasar")
    elseif(esperado STREQUAL "falla" AND codigo EQUAL 0)
        message(FATAL_ERROR "${caso}: debia fallar")
    endif()
    string(FIND "${o}${e}" "${patron}" pos)
    if(pos LESS 0)
        message(FATAL_ERROR "${caso}: no aparece \"${patron}\"")
    endif()
endfunction()

set(apendice "## Apéndice: Diagnóstico

Cada descarga crea una solicitud por dirección; el SAT entrega paquetes.

### Paquetes

Un PAQUETE vencido; las Solicitudes se ven en la lista.

![Lista de Diagnóstico](img/b.png)
")
set(intro "# Manual

Intro sin vocabulario técnico: descargas, archivos del SAT, paquetería y
empaquetado no cuentan.

```
# Diagnóstico (no es un encabezado: está en un bloque de código)
```

![Facturas de septiembre; con punto y coma](img/a.png)

")
set(cierre "## Glosario

Descarga: lo que antes se llamaba de otra forma.
")
set(pngs "a.png;b.png")

# Pasa: el vocabulario solo dentro del apendice (con subsecciones) y el
# apendice termina en el siguiente encabezado de su nivel.
preparar(valido "${intro}${apendice}${cierre}" "${pngs}" "${pngs}")
verificar(valido pasa "OK verificar-manual: 2 imagenes referenciadas")

# vocabulario: fuera del apendice, en cualquier caja y con acento.
preparar(voc-intro "${intro}Pulsa para crear Solicitudes.\n\n${apendice}${cierre}" "${pngs}" "${pngs}")
verificar(voc-intro falla "FALLA vocabulario: linea 12 (seccion \"Manual\"): \"solicitudes\"")
preparar(voc-despues "${intro}${apendice}${cierre}El PAQUETÉ llegó.\n" "${pngs}" "${pngs}")
verificar(voc-despues falla "(seccion \"Glosario\"): \"paquete\"")
preparar(voc-acento "${intro}Una solicitúd.\n${apendice}${cierre}" "${pngs}" "${pngs}")
verificar(voc-acento falla "\"solicitud\" fuera del apendice")
preparar(voc-acento-a "${intro}Un páquete y una sólicitud.\n${apendice}${cierre}" "${pngs}" "${pngs}")
verificar(voc-acento-a falla "\"paquete\" fuera del apendice")
preparar(voc-otro-diagnostico "${intro}## Diagnóstico de e.firma\n\nUna solicitud.\n\n${apendice}${cierre}" "${pngs}" "${pngs}")
verificar(voc-otro-diagnostico falla "(seccion \"Diagnóstico de e.firma\"): \"solicitud\"")
preparar(png-fuera "${intro}![Otra](../otra.png)\n${apendice}${cierre}" "${pngs}" "${pngs}")
verificar(png-fuera falla "FALLA imagen: PNG fuera de img/: ../otra.png")
preparar(voc-encabezado "${intro}## Paquetes del SAT\n\n${apendice}${cierre}" "${pngs}" "${pngs}")
verificar(voc-encabezado falla "(seccion \"Paquetes del SAT\"): \"paquetes\"")
preparar(sin-apendice "${intro}${cierre}" "a.png" "a.png")
verificar(sin-apendice falla "exactamente un apendice \"## Apéndice: Diagnóstico\" y hay 0")
preparar(dos-apendices "${intro}${apendice}${cierre}## Apendice: Diagnostico\n" "${pngs}" "${pngs}")
verificar(dos-apendices falla "y hay 2")

# imagen: referencia a un PNG que no existe, y sin texto alternativo.
preparar(sin-imagen "${intro}![Falta](img/c.png)\n${apendice}${cierre}" "${pngs}" "${pngs}")
verificar(sin-imagen falla "FALLA imagen: no existe la imagen referenciada: img/c.png")
preparar(sin-alt "${intro}![ ](img/a.png)\n${apendice}${cierre}" "${pngs}" "${pngs}")
verificar(sin-alt falla "FALLA imagen: sin texto alternativo: img/a.png")

# huerfana: PNG en img/ sin referencia en el manual.
preparar(huerfana "${intro}${apendice}${cierre}" "a.png;b.png;z.png" "a.png;b.png;z.png")
verificar(huerfana falla "FALLA huerfana: z.png no esta referenciado en el manual")

# manifiesto: PNG referenciado que no esta en el manifiesto, y PNG del
# manifiesto que no esta en img/.
preparar(fuera-manifiesto "${intro}${apendice}${cierre}" "${pngs}" "a.png")
verificar(fuera-manifiesto falla "FALLA manifiesto: b.png no esta en el manifiesto de capturas")
preparar(sin-regenerar "${intro}${apendice}${cierre}" "${pngs}" "a.png;b.png;d.png")
verificar(sin-regenerar falla "FALLA manifiesto: d.png esta en el manifiesto pero no en")

message("OK prueba-verificar-manual: 1 caso valido y 14 negativos (vocabulario, imagen, huerfana, manifiesto)")
