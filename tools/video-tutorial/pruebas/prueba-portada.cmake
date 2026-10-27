# T024.4 (03-motion 4): portada (120 cuadros) y cierre (150) con la marca.
# Graba pruebas/guion-portada.json (sin red, --offline) y revisa:
# - linea de tiempo: portada 120, cierre 150, capitulo 1 en el cuadro 120;
# - portada 90: sin ventana de la app (zona de la ventana oscura);
# - portada 90 contra cierre 60: el interior de la carpeta compuesta por
#   capas coincide con el lockup (PSNR >= 40 dB en dos recortes planos; los
#   bordes antialias y el fondo animado quedan fuera);
# - ultimo cuadro: #052E1C (+-4) en las cuatro esquinas y el centro.
# Sin sesion grafica: 77 (SKIP). Variables: HERRAMIENTA (lista), GUION, DIR,
# FFMPEG.

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
execute_process(COMMAND ${HERRAMIENTA} --guion ${GUION} --salida ${DIR} --nombre portada --offline
                RESULT_VARIABLE codigo OUTPUT_VARIABLE salida ERROR_VARIABLE errores)
message("${salida}${errores}")
if(codigo EQUAL 77)
    cmake_language(EXIT 77)
endif()
if(NOT codigo EQUAL 0)
    message(FATAL_ERROR "la grabacion termino con codigo ${codigo}")
endif()
set(mp4 "${DIR}/portada.mp4")

file(READ "${DIR}/portada.linea-tiempo.json" lt)
string(JSON portada GET "${lt}" portada)
string(JSON cierre GET "${lt}" cierre)
string(JSON total GET "${lt}" totalCuadros)
string(JSON inicio GET "${lt}" capitulos 0 inicio)
if(NOT portada EQUAL 120 OR NOT cierre EQUAL 150 OR NOT inicio EQUAL 120)
    message(FATAL_ERROR "linea de tiempo: portada ${portada}, cierre ${cierre}, capitulo 1 en ${inicio}")
endif()
math(EXPR cierre60 "${total} - 150 + 60")
math(EXPR ultimo "${total} - 1")

# Color medio (r g b, 0-255) de un recorte del cuadro n.
function(color n x y w h salida)
    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error -nostdin -i ${mp4}
                            -vf "select=eq(n\\,${n}),crop=${w}:${h}:${x}:${y},scale=1:1:flags=area"
                            -frames:v 1 -pix_fmt rgb24 -f rawvideo -y ${DIR}/px.raw
                    RESULT_VARIABLE c)
    file(READ ${DIR}/px.raw hex HEX)
    if(NOT c EQUAL 0 OR NOT hex MATCHES "^[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]$")
        message(FATAL_ERROR "no se pudo leer el cuadro ${n}")
    endif()
    string(SUBSTRING "${hex}" 0 2 r)
    string(SUBSTRING "${hex}" 2 2 g)
    string(SUBSTRING "${hex}" 4 2 b)
    math(EXPR r "0x${r}")
    math(EXPR g "0x${g}")
    math(EXPR b "0x${b}")
    set(${salida} "${r};${g};${b}" PARENT_SCOPE)
endfunction()

# Ultimo cuadro solido #052E1C (5, 46, 28).
foreach(punto "8;8" "2544;8" "8;1424" "2544;1424" "1272;712")
    list(GET punto 0 x)
    list(GET punto 1 y)
    color(${ultimo} ${x} ${y} 8 8 c)
    list(GET c 0 r)
    list(GET c 1 g)
    list(GET c 2 b)
    foreach(par "${r};5" "${g};46" "${b};28")
        list(GET par 0 v)
        list(GET par 1 e)
        math(EXPR d "${v} - ${e}")
        if(d GREATER 4 OR d LESS -4)
            message(FATAL_ERROR "ultimo cuadro en (${x}, ${y}): ${r},${g},${b}; se esperaba #052E1C")
        endif()
    endforeach()
endforeach()

# Portada 90: la zona de la ventana (logicos 180-380 x 300-500) sigue en el
# fondo atenuado, no en la UI clara de la app.
color(90 360 600 400 400 v)
list(GET v 0 r)
list(GET v 1 g)
list(GET v 2 b)
math(EXPR suma "${r} + ${g} + ${b}")
if(suma GREATER 300)
    message(FATAL_ERROR "portada 90: la zona de la ventana es clara (${r},${g},${b}); la app no debe verse")
endif()

# Portada 90 contra cierre 60: interior de las hojas y del frente.
foreach(recorte "210:110:1170:510" "360:130:1100:670")
    execute_process(COMMAND ${FFMPEG} -hide_banner -nostdin -i ${mp4} -i ${mp4} -filter_complex
                            "[0]select=eq(n\\,90),setpts=0,crop=${recorte}[a];[1]select=eq(n\\,${cierre60}),setpts=0,crop=${recorte}[b];[a][b]psnr"
                            -frames:v 1 -f null -
                    ERROR_VARIABLE e)
    string(REGEX MATCH "average:([0-9.inf]+)" _ "${e}")
    set(psnr "${CMAKE_MATCH_1}")
    message("PSNR portada 90 / cierre ${cierre60} en ${recorte}: ${psnr} dB")
    if(psnr STREQUAL "" OR (NOT psnr STREQUAL "inf" AND psnr LESS 40))
        message(FATAL_ERROR "la marca de la portada (90) no coincide con el lockup del cierre (60): ${psnr} dB")
    endif()
endforeach()
message("OK portada y cierre: ${total} cuadros")
