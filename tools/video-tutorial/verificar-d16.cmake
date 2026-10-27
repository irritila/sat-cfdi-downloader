# Verificacion D16 (T017): compara una grabacion contra el manifiesto
# versionado (verificacion-d16.json) y, si se da una segunda, las dos entre si.
# Sale con codigo != 0 si algo no coincide.
#
#   cmake -DMODO=verificar -DMANIFIESTO=<verificacion-d16.json> -DGUION=<guion.json> -DA=<a.mp4> [-DB=<b.mp4>]
#         -DFFMPEG=<ffmpeg> -DFFPROBE=<ffprobe> [-DTRABAJO=<dir>] -P verificar-d16.cmake
#   cmake -DMODO=generar -DGUION=<guion.json> -DLINEA_TIEMPO=<x.linea-tiempo.json>
#         -DMANIFIESTO=<salida.json> -P verificar-d16.cmake
#
# T024.4 (esquema 2): el manifiesto lleva la huella del guion (SHA-256 del
# archivo) y la de la voz (claves de bloque del cache usado), que la grabacion
# escribe en su linea de tiempo y en la etiqueta `comment` del MP4
# ("satcfdi-d16 guion=<sha> voz=<sha>"). verificar falla si el manifiesto no
# tiene huellas (el del video anterior), si es de otro guion, o si un MP4 se
# grabo con otro guion u otra voz (p. ej. la cache del video anterior).
#
# Contra el manifiesto: video h264 WxH a 30/1, audio aac 48 kHz, duraciones de
# pistas iguales +-toleranciaPistasMs, total en [duracionMin, duracionMax], numero de
# cuadros y capitulos (titulo, inicio y fin en cuadros) de ffprobe
# -show_chapters y pico verdadero del AAC decodificado (picoVerdaderoMaximoAacDb,
# opcional). Entre A y B: mismo numero de cuadros, mismo MD5 del audio
# decodificado (formato del manifiesto) y PSNR >= psnrMinimoDb en cada cuadro
# muestreado (identicos = inf).

cmake_minimum_required(VERSION 3.21)

set(fallos "")
macro(fallar texto)
    message("FALLA: ${texto}")
    list(APPEND fallos "${texto}")
endmacro()

if(MODO STREQUAL "generar")
    # Manifiesto a partir del guion (escenas con ilustracion) y de la linea de
    # tiempo de una grabacion aprobada.
    file(READ "${LINEA_TIEMPO}" lt)
    file(READ "${GUION}" g)
    file(SHA256 "${GUION}" huella_guion)
    string(JSON huella_lt ERROR_VARIABLE sin_huella GET "${lt}" huellaGuion)
    string(JSON huella_voz ERROR_VARIABLE sin_voz GET "${lt}" huellaVoz)
    if(sin_huella OR NOT huella_lt STREQUAL huella_guion)
        message(FATAL_ERROR "la linea de tiempo ${LINEA_TIEMPO} no es de ${GUION} (huella '${huella_lt}', "
                            "guion ${huella_guion}): graba de nuevo con el guion vigente")
    endif()
    if(sin_voz OR huella_voz STREQUAL "ninguna" OR huella_voz STREQUAL "")
        message(FATAL_ERROR "la grabacion de ${LINEA_TIEMPO} no tiene voz (--voz): el manifiesto D16 es del video con voz")
    endif()
    string(JSON total GET "${lt}" totalCuadros)
    string(JSON fps GET "${lt}" fps)
    string(JSON nc LENGTH "${lt}" capitulos)
    math(EXPR uc "${nc} - 1")
    set(capitulos "")
    set(cuadros "")
    foreach(c RANGE ${uc})
        string(JSON titulo GET "${lt}" capitulos ${c} titulo)
        string(JSON inicio GET "${lt}" capitulos ${c} inicio)
        string(JSON fin GET "${lt}" capitulos ${c} fin)
        string(JSON idc GET "${lt}" capitulos ${c} id)
        string(REPLACE "\"" "\\\"" titulo_json "${titulo}")
        # El MP4 hace empezar el primer capitulo en 0 (con portada, la incluye).
        set(inicio_mp4 ${inicio})
        if(c EQUAL 0)
            set(inicio_mp4 0)
        endif()
        list(APPEND capitulos "{\"titulo\": \"${titulo_json}\", \"inicio\": ${inicio_mp4}, \"fin\": ${fin}}")
        # Un cuadro por capitulo: mitad de su primera escena.
        string(JSON e0 GET "${lt}" capitulos ${c} escenas 0 id)
        string(JSON d0 GET "${lt}" capitulos ${c} escenas 0 duracion)
        string(JSON i0 GET "${lt}" capitulos ${c} escenas 0 inicio)
        math(EXPR n "${i0} + ${d0} / 2")
        list(APPEND cuadros "{\"cuadro\": ${n}, \"motivo\": \"capitulo ${idc}: ${e0}\"}")
        # Uno por escena de ilustracion: el ultimo cuadro antes del momento
        # (inicio o fin) de la primera linea que se espera despues de
        # mostrarIlustracion; hasta ahi la ilustracion sigue en pantalla.
        string(JSON ne LENGTH "${lt}" capitulos ${c} escenas)
        math(EXPR ue "${ne} - 1")
        foreach(e RANGE ${ue})
            string(JSON ide GET "${lt}" capitulos ${c} escenas ${e} id)
            # Escena en el guion.
            string(JSON ngc LENGTH "${g}" capitulos)
            math(EXPR ugc "${ngc} - 1")
            set(acciones "")
            foreach(gc RANGE ${ugc})
                string(JSON gid GET "${g}" capitulos ${gc} id)
                if(gid STREQUAL idc)
                    string(JSON nge LENGTH "${g}" capitulos ${gc} escenas)
                    math(EXPR uge "${nge} - 1")
                    foreach(ge RANGE ${uge})
                        string(JSON geid GET "${g}" capitulos ${gc} escenas ${ge} id)
                        if(geid STREQUAL ide)
                            string(JSON acciones ERROR_VARIABLE sin GET "${g}" capitulos ${gc} escenas ${ge} acciones)
                        endif()
                    endforeach()
                endif()
            endforeach()
            if(acciones STREQUAL "")
                continue()
            endif()
            string(JSON na LENGTH "${acciones}")
            math(EXPR ua "${na} - 1")
            set(mostrada "")
            set(linea "")
            set(momento "")
            foreach(a RANGE ${ua})
                string(JSON tipo GET "${acciones}" ${a} tipo)
                if(tipo STREQUAL "mostrarIlustracion" AND mostrada STREQUAL "")
                    string(JSON mostrada GET "${acciones}" ${a} ilustracion)
                elseif(tipo STREQUAL "esperarLinea" AND NOT mostrada STREQUAL "" AND linea STREQUAL "")
                    string(JSON linea GET "${acciones}" ${a} linea)
                    string(JSON momento ERROR_VARIABLE sin GET "${acciones}" ${a} momento)
                endif()
            endforeach()
            if(linea STREQUAL "")
                continue()
            endif()
            string(JSON nl LENGTH "${lt}" capitulos ${c} escenas ${e} lineas)
            math(EXPR ul "${nl} - 1")
            foreach(l RANGE ${ul})
                string(JSON idl GET "${lt}" capitulos ${c} escenas ${e} lineas ${l} id)
                if(idl STREQUAL linea)
                    # En la linea de tiempo el inicio de cada linea es absoluto.
                    string(JSON il GET "${lt}" capitulos ${c} escenas ${e} lineas ${l} inicio)
                    string(JSON dl GET "${lt}" capitulos ${c} escenas ${e} lineas ${l} duracion)
                    if(momento STREQUAL "inicio")
                        math(EXPR n "${il} - 1")
                    else()
                        math(EXPR n "${il} + ${dl} - 1")
                    endif()
                    list(APPEND cuadros "{\"cuadro\": ${n}, \"motivo\": \"ilustracion ${mostrada}: ${idc}/${ide}, antes del ${momento} de ${linea}\"}")
                endif()
            endforeach()
        endforeach()
    endforeach()
    list(JOIN capitulos ",\n    " capitulos_json)
    list(JOIN cuadros ",\n    " cuadros_json)
    file(WRITE "${MANIFIESTO}" "{
  \"esquema\": 2,
  \"descripcion\": \"Manifiesto de verificacion D16 (T017, T024.4; verificar-d16.cmake). Generado de guion-tutorial.json y de la linea de tiempo de la grabacion aprobada.\",
  \"huellaGuion\": \"${huella_guion}\",
  \"huellaVoz\": \"${huella_voz}\",
  \"video\": {\"codec\": \"h264\", \"ancho\": 2560, \"alto\": 1440, \"fps\": \"${fps}/1\"},
  \"audio\": {\"codec\": \"aac\", \"frecuencia\": 48000,
            \"decodificacion\": \"ffmpeg -i <mp4> -map 0:a:0 -c:a pcm_s16le -ar 48000 -ac 2 -f md5 -\"},
  \"toleranciaPistasMs\": 100,
  \"duracionMinS\": 300,
  \"duracionMaxS\": 480,
  \"totalCuadros\": ${total},
  \"psnrMinimoDb\": 60,
  \"picoVerdaderoMaximoAacDb\": -1.8,
  \"capitulos\": [
    ${capitulos_json}
  ],
  \"cuadrosMuestreados\": [
    ${cuadros_json}
  ]
}
")
    message("manifiesto escrito: ${MANIFIESTO}")
    return()
endif()

if(NOT MODO STREQUAL "verificar" OR NOT EXISTS "${MANIFIESTO}" OR NOT EXISTS "${A}" OR NOT EXISTS "${GUION}")
    message(FATAL_ERROR "uso: -DMODO=verificar -DMANIFIESTO=<json> -DGUION=<guion.json> -DA=<mp4> [-DB=<mp4>] -DFFMPEG -DFFPROBE")
endif()
if(DEFINED B AND NOT B STREQUAL "" AND NOT EXISTS "${B}")
    message(FATAL_ERROR "no existe ${B}")
endif()
if(NOT DEFINED TRABAJO OR TRABAJO STREQUAL "")
    get_filename_component(TRABAJO "${A}" DIRECTORY)
endif()
file(READ "${MANIFIESTO}" m)
string(JSON fps_m GET "${m}" video fps)
string(REGEX REPLACE "/.*" "" fps_n "${fps_m}")
string(JSON total_m GET "${m}" totalCuadros)
string(JSON psnr_min GET "${m}" psnrMinimoDb)

# T024.4: huellas. Un manifiesto sin ellas es el del video anterior (T017).
string(JSON huella_m ERROR_VARIABLE sin_huella_m GET "${m}" huellaGuion)
string(JSON huella_voz_m ERROR_VARIABLE sin_voz_m GET "${m}" huellaVoz)
file(SHA256 "${GUION}" huella_guion)
if(sin_huella_m OR sin_voz_m)
    fallar("el manifiesto ${MANIFIESTO} no tiene huellas del guion ni de la voz (es el del video anterior): "
           "regeneralo con -DMODO=generar (docs/development.md)")
elseif(NOT huella_m STREQUAL huella_guion)
    fallar("el manifiesto es de otro guion (huella ${huella_m}; ${GUION} es ${huella_guion}): regeneralo")
endif()

# Contra el manifiesto. Deja <prefijo>_cuadros.
macro(contra_manifiesto video prefijo)
    execute_process(COMMAND ${FFPROBE} -v error -show_entries
                            "stream=codec_type,codec_name,width,height,r_frame_rate,sample_rate,duration,nb_frames:format=duration"
                            -of json "${video}"
                    OUTPUT_VARIABLE p RESULT_VARIABLE r)
    if(NOT r EQUAL 0)
        fallar("${prefijo}: ffprobe fallo")
    endif()
    string(JSON ns LENGTH "${p}" streams)
    math(EXPR us "${ns} - 1")
    set(dv "")
    set(da "")
    set(${prefijo}_cuadros "")
    foreach(s RANGE ${us})
        string(JSON tipo GET "${p}" streams ${s} codec_type)
        string(JSON codec GET "${p}" streams ${s} codec_name)
        if(tipo STREQUAL "video")
            string(JSON w GET "${p}" streams ${s} width)
            string(JSON h GET "${p}" streams ${s} height)
            string(JSON fr GET "${p}" streams ${s} r_frame_rate)
            string(JSON dv GET "${p}" streams ${s} duration)
            string(JSON ${prefijo}_cuadros GET "${p}" streams ${s} nb_frames)
            string(JSON cm GET "${m}" video codec)
            string(JSON wm GET "${m}" video ancho)
            string(JSON hm GET "${m}" video alto)
            if(NOT codec STREQUAL cm OR NOT w EQUAL wm OR NOT h EQUAL hm OR NOT fr STREQUAL fps_m)
                fallar("${prefijo}: video ${codec} ${w}x${h} ${fr}; se esperaba ${cm} ${wm}x${hm} ${fps_m}")
            endif()
            message("${prefijo}: video ${codec} ${w}x${h} ${fr}, ${dv} s, ${${prefijo}_cuadros} cuadros")
        elseif(tipo STREQUAL "audio")
            string(JSON sr GET "${p}" streams ${s} sample_rate)
            string(JSON da GET "${p}" streams ${s} duration)
            string(JSON cam GET "${m}" audio codec)
            string(JSON srm GET "${m}" audio frecuencia)
            if(NOT codec STREQUAL cam OR NOT sr EQUAL srm)
                fallar("${prefijo}: audio ${codec} ${sr} Hz; se esperaba ${cam} ${srm} Hz")
            endif()
            message("${prefijo}: audio ${codec} ${sr} Hz, ${da} s")
        endif()
    endforeach()
    string(JSON dt GET "${p}" format duration)
    if(dv STREQUAL "" OR da STREQUAL "")
        fallar("${prefijo}: faltan pistas (video '${dv}', audio '${da}')")
    else()
        # |dv - da| <= tolerancia, en milisegundos enteros.
        foreach(v dv da dt)
            string(REGEX MATCH "^([0-9]+)\\.?([0-9]*)" _ "${${v}}")
            string(SUBSTRING "${CMAKE_MATCH_2}000" 0 3 ms)
            math(EXPR ${v}_ms "${CMAKE_MATCH_1} * 1000 + 1${ms} - 1000")
        endforeach()
        math(EXPR dif "${dv_ms} - ${da_ms}")
        if(dif LESS 0)
            math(EXPR dif "-${dif}")
        endif()
        string(JSON tol_ms GET "${m}" toleranciaPistasMs)
        string(JSON dmin GET "${m}" duracionMinS)
        string(JSON dmax GET "${m}" duracionMaxS)
        if(dif GREATER tol_ms)
            fallar("${prefijo}: pistas de video y audio difieren ${dif} ms (tolerancia ${tol_ms})")
        endif()
        if(dt_ms LESS ${dmin}000 OR dt_ms GREATER ${dmax}000)
            fallar("${prefijo}: duracion total ${dt} s fuera de [${dmin}, ${dmax}]")
        endif()
        message("${prefijo}: duraciones video/audio difieren ${dif} ms; total ${dt} s")
    endif()
    # D25: pico verdadero del AAC decodificado (si el manifiesto lo fija).
    string(JSON tp_max ERROR_VARIABLE sin_tp GET "${m}" picoVerdaderoMaximoAacDb)
    if(NOT sin_tp)
        execute_process(COMMAND ${FFMPEG} -hide_banner -nostdin -i "${video}" -map 0:a:0
                                -af "loudnorm=I=-16:TP=-2:LRA=11:print_format=json" -f null -
                        ERROR_VARIABLE ln RESULT_VARIABLE r)
        string(REGEX MATCH "\"input_tp\" : \"(-?[0-9.]+|-?inf)\"" _ "${ln}")
        set(tp "${CMAKE_MATCH_1}")
        string(REGEX MATCH "\"input_i\" : \"(-?[0-9.]+|-?inf)\"" _ "${ln}")
        set(li "${CMAKE_MATCH_1}")
        if(NOT r EQUAL 0 OR tp STREQUAL "")
            fallar("${prefijo}: no se pudo medir el audio decodificado")
        elseif(tp GREATER tp_max)
            fallar("${prefijo}: pico verdadero del AAC decodificado ${tp} dBTP > ${tp_max}")
        else()
            message("${prefijo}: audio decodificado ${li} LUFS, pico verdadero ${tp} dBTP (maximo ${tp_max})")
        endif()
    endif()
    # T024.4: el MP4 se grabo con este guion y con la voz del manifiesto.
    execute_process(COMMAND ${FFPROBE} -v error -show_entries format_tags=comment -of default=nw=1:nk=1 "${video}"
                    OUTPUT_VARIABLE comentario OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT comentario MATCHES "^satcfdi-d16 guion=([0-9a-f]+) voz=([0-9a-fnigua]+)$")
        fallar("${prefijo}: el MP4 no trae las huellas D16 (grabado con una herramienta anterior)")
    elseif(NOT CMAKE_MATCH_1 STREQUAL huella_m)
        fallar("${prefijo}: grabado con otro guion (huella ${CMAKE_MATCH_1}; manifiesto ${huella_m})")
    elseif(NOT CMAKE_MATCH_2 STREQUAL huella_voz_m)
        fallar("${prefijo}: grabado con otra voz (huella ${CMAKE_MATCH_2}; manifiesto ${huella_voz_m}); "
               "p. ej. la cache de voz del video anterior")
    else()
        message("${prefijo}: huellas de guion y voz OK")
    endif()
    if(NOT "${${prefijo}_cuadros}" STREQUAL "${total_m}")
        fallar("${prefijo}: ${${prefijo}_cuadros} cuadros; el manifiesto dice ${total_m}")
    endif()
    # Capitulos.
    execute_process(COMMAND ${FFPROBE} -v error -show_chapters -of json "${video}" OUTPUT_VARIABLE pc)
    string(JSON nch LENGTH "${pc}" chapters)
    string(JSON ncm LENGTH "${m}" capitulos)
    if(NOT nch EQUAL ncm)
        fallar("${prefijo}: ${nch} capitulos en el MP4; el manifiesto tiene ${ncm}")
    else()
        math(EXPR uch "${nch} - 1")
        foreach(k RANGE ${uch})
            string(JSON t GET "${pc}" chapters ${k} tags title)
            string(JSON st GET "${pc}" chapters ${k} start_time)
            string(JSON et GET "${pc}" chapters ${k} end_time)
            string(JSON tm GET "${m}" capitulos ${k} titulo)
            string(JSON im GET "${m}" capitulos ${k} inicio)
            string(JSON fm GET "${m}" capitulos ${k} fin)
            # segundos -> cuadros (round(t * fps)) con milisegundos enteros.
            foreach(v st et)
                string(REGEX MATCH "^([0-9]+)\\.?([0-9]*)" _ "${${v}}")
                string(SUBSTRING "${CMAKE_MATCH_2}000" 0 3 ms)
                math(EXPR ${v}_c "((${CMAKE_MATCH_1} * 1000 + 1${ms} - 1000) * ${fps_n} + 500) / 1000")
            endforeach()
            if(NOT t STREQUAL tm OR NOT st_c EQUAL im OR NOT et_c EQUAL fm)
                fallar("${prefijo}: capitulo ${k} '${t}' [${st_c}, ${et_c}); manifiesto '${tm}' [${im}, ${fm})")
            else()
                message("${prefijo}: capitulo '${t}' [${st_c}, ${et_c}) OK")
            endif()
        endforeach()
    endif()
endmacro()

contra_manifiesto("${A}" a)
if(DEFINED B AND NOT B STREQUAL "")
    contra_manifiesto("${B}" b)
    if(NOT a_cuadros STREQUAL b_cuadros)
        fallar("cuadros distintos: ${a_cuadros} / ${b_cuadros}")
    endif()
    # MD5 del audio decodificado (formato del manifiesto).
    foreach(v A B)
        execute_process(COMMAND ${FFMPEG} -v error -nostdin -i "${${v}}" -map 0:a:0 -c:a pcm_s16le -ar 48000 -ac 2
                                -f md5 -
                        OUTPUT_VARIABLE md5_${v} OUTPUT_STRIP_TRAILING_WHITESPACE)
    endforeach()
    if(md5_A STREQUAL "" OR NOT md5_A STREQUAL md5_B)
        fallar("audio decodificado distinto: ${md5_A} / ${md5_B}")
    else()
        message("audio decodificado: ${md5_A} en ambas")
    endif()
    # PSNR en los cuadros muestreados (una sola pasada de decodificacion).
    string(JSON nm LENGTH "${m}" cuadrosMuestreados)
    math(EXPR um "${nm} - 1")
    set(terminos "")
    set(lista "")
    foreach(k RANGE ${um})
        string(JSON n GET "${m}" cuadrosMuestreados ${k} cuadro)
        if(n GREATER_EQUAL total_m)
            fallar("cuadro muestreado ${n} fuera del video")
        endif()
        list(APPEND terminos "eq(n\\,${n})")
        list(APPEND lista "${n}")
    endforeach()
    list(JOIN terminos "+" expresion)
    set(stats "${TRABAJO}/psnr-d16.log")
    file(REMOVE "${stats}")
    execute_process(COMMAND ${FFMPEG} -v error -nostdin -i "${A}" -i "${B}" -filter_complex
                            "[0:v]select='${expresion}'[a];[1:v]select='${expresion}'[b];[a][b]psnr=stats_file=${stats}"
                            -fps_mode passthrough -an -f null -
                    RESULT_VARIABLE r ERROR_VARIABLE err)
    if(NOT r EQUAL 0 OR NOT EXISTS "${stats}")
        fallar("ffmpeg psnr fallo: ${err}")
    else()
        file(STRINGS "${stats}" filas)
        list(LENGTH filas nf)
        if(NOT nf EQUAL nm)
            fallar("psnr comparo ${nf} cuadros; se muestrearon ${nm}")
        endif()
        set(i 0)
        foreach(fila IN LISTS filas)
            string(REGEX MATCH "psnr_avg:([a-z0-9.]+)" _ "${fila}")
            set(v "${CMAKE_MATCH_1}")
            list(GET lista ${i} cuadro)
            string(JSON motivo GET "${m}" cuadrosMuestreados ${i} motivo)
            if(NOT v STREQUAL "inf" AND (v STREQUAL "" OR v LESS psnr_min))
                fallar("cuadro ${cuadro} (${motivo}): PSNR ${v} dB < ${psnr_min}")
            else()
                message("cuadro ${cuadro} (${motivo}): PSNR ${v} dB")
            endif()
            math(EXPR i "${i} + 1")
        endforeach()
    endif()
endif()

list(LENGTH fallos nfallos)
if(nfallos GREATER 0)
    message(FATAL_ERROR "D16: ${nfallos} discrepancias")
endif()
message("D16 OK")
