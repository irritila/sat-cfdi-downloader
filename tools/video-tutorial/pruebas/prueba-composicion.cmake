# Composicion (T017 D8, D21, D22): dos grabaciones rapidas con la misma
# semilla dan el mismo fondo y la misma transcripcion: PSNR >= 60 dB en cada
# cuadro sobre un recorte de fondo puro y otro de la franja de transcripcion.
# Sin sesion grafica: 77 (SKIP). Variables: HERRAMIENTA (lista), GUION, SALIDA,
# FFMPEG, FFPROBE.

foreach(nombre composicion-a composicion-b)
    execute_process(
        COMMAND ${HERRAMIENTA} --guion ${GUION} --salida ${SALIDA} --nombre ${nombre} --rapido --offline
        RESULT_VARIABLE codigo OUTPUT_VARIABLE salida ERROR_VARIABLE errores)
    message("${salida}${errores}")
    if(codigo EQUAL 77)
        message("SKIP: sin sesion grafica")
        cmake_language(EXIT 77)
    endif()
    if(NOT codigo EQUAL 0)
        message(FATAL_ERROR "la grabacion ${nombre} termino con codigo ${codigo}")
    endif()
endforeach()

execute_process(COMMAND ${FFPROBE} -v error -select_streams v:0 -show_entries stream=width,height
                        -of csv=p=0 ${SALIDA}/composicion-a.mp4
                OUTPUT_VARIABLE dimensiones OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT dimensiones STREQUAL "2560,1440")
    message(FATAL_ERROR "el video mide ${dimensiones}; se esperaba 2560,1440 (D8)")
endif()

# Recortes en pixeles del lienzo: fondo puro (a la derecha de la ventana y
# su sombra) y franja de transcripcion (bajo la ventana).
foreach(zona "fondo:crop=200:560:2340:240" "transcripcion:crop=1600:180:480:1256")
    string(REPLACE ":" ";" partes "${zona}")
    list(GET partes 0 etiqueta)
    string(REPLACE "${etiqueta}:" "" filtro "${zona}")
    execute_process(
        COMMAND ${FFMPEG} -hide_banner -nostdin -i ${SALIDA}/composicion-a.mp4 -i ${SALIDA}/composicion-b.mp4
                -filter_complex "[0:v]${filtro}[a];[1:v]${filtro}[b];[a][b]psnr=stats_file=-" -f null -
        OUTPUT_VARIABLE stats ERROR_VARIABLE _)
    string(REGEX MATCHALL "psnr_avg:[0-9.inf]+" valores "${stats}")
    list(LENGTH valores n)
    set(minimo 1000)
    foreach(v IN LISTS valores)
        string(REPLACE "psnr_avg:" "" v "${v}")
        if(v STREQUAL "inf")
            continue()
        endif()
        if(v LESS minimo)
            set(minimo ${v})
        endif()
    endforeach()
    if(n EQUAL 0 OR minimo LESS 60)
        message(FATAL_ERROR "${etiqueta}: PSNR minimo ${minimo} dB en ${n} cuadros (se exige >= 60)")
    endif()
    message("OK ${etiqueta}: ${n} cuadros, PSNR minimo ${minimo} dB (1000 = identicos)")
endforeach()
