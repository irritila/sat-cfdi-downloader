# tools/video-tutorial (T017): graba el video tutorial cuadro a cuadro a partir
# del guion versionado (README.md). Solo con -DSATCFDI_BUILD_VIDEO_TUTORIAL=ON;
# no es parte de la app. Requiere ffmpeg (Homebrew) y, para grabar, una sesion
# grafica de macOS.
#
# D10: NO enlaza satcfdi_sat, satcfdi_secrets_macos, satcfdi_os_macos ni
# satcfdi_app_core (este ultimo arrastra satcfdi_sat), ni Qt6::Network de forma
# directa. Se comprueba al configurar. Como tools/capturas-manual, compila
# MacOSMapeos.cpp e IconoMenuBar.cpp (textos y dibujo del menu bar, sin AppKit)
# para las ilustraciones.

find_program(SATCFDI_FFMPEG ffmpeg HINTS /opt/homebrew/bin /usr/local/bin)
find_program(SATCFDI_FFPROBE ffprobe HINTS /opt/homebrew/bin /usr/local/bin)
if(NOT SATCFDI_FFMPEG OR NOT SATCFDI_FFPROBE)
    message(FATAL_ERROR "SATCFDI_BUILD_VIDEO_TUTORIAL requiere ffmpeg y ffprobe (brew install ffmpeg)")
endif()
# D15: la grabacion corre dentro de un sandbox de proceso sin red (sin-red.sb).
find_program(SATCFDI_SANDBOX_EXEC sandbox-exec HINTS /usr/bin)
if(NOT SATCFDI_SANDBOX_EXEC)
    message(FATAL_ERROR "SATCFDI_BUILD_VIDEO_TUTORIAL requiere sandbox-exec (macOS)")
endif()
set(SATCFDI_VIDEO_SIN_RED ${SATCFDI_SANDBOX_EXEC} -f ${CMAKE_CURRENT_SOURCE_DIR}/sin-red.sb)

# Guion, salidas y cache de voz (solo Qt Core): lo comparten el grabador y el
# paso de voz.
qt_add_library(satcfdi_video_guion STATIC
    Guion.h
    Guion.cpp
    ResultadosExportacion.h
    Voz.h
    Voz.cpp
    Musica.h
    Musica.cpp
)
target_link_libraries(satcfdi_video_guion PUBLIC Qt6::Core)
satcfdi_enable_warnings(satcfdi_video_guion)

# Qt6::GuiPrivate: QWindowSystemInterface (foco de Qt de la ventana de la app).
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)

qt_add_executable(satcfdi_video_tutorial
    main.cpp
    CuadroApp.h
    CuadroApp.cpp
    Salidas.h
    Salidas.cpp
    DatosDemo.h
    DatosDemo.cpp
    Grabador.h
    Grabador.cpp
    Agarre.h
    DiagnosticoClic.h
    DiagnosticoClic.cpp
    EntornoQml.h
    EntornoQml.cpp
    Barreras.h
    Barreras.cpp
    BarreraExportacion.h
    BarreraExportacion.cpp
    ResultadosExportacion.h
    SalidaFfmpeg.h
    SalidaFfmpeg.cpp
    Ilustraciones.h
    Ilustraciones.cpp
    NotificacionesIlustradas.h
    NotificacionesIlustradas.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/os/macos/MacOSMapeos.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/os/macos/IconoMenuBar.cpp
)
target_include_directories(satcfdi_video_tutorial PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    # ServiciosFijos.h (fakes inmutables del manual), sin modificarlo.
    ${PROJECT_SOURCE_DIR}/tools/capturas-manual
)
target_compile_definitions(satcfdi_video_tutorial PRIVATE
    SATCFDI_VIDEO_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    SATCFDI_CAPTURAS_QML_DIR="${PROJECT_SOURCE_DIR}/tools/capturas-manual/qml"
    SATCFDI_FFMPEG="${SATCFDI_FFMPEG}"
    # --salida solo se acepta dentro de este directorio.
    SATCFDI_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
target_link_libraries(satcfdi_video_tutorial PRIVATE
    satcfdi_video_guion
    # T024.2 (T024 D9): Facturas, Historial, Exportacion y Respaldo sinteticos.
    satcfdi_fakes_materiales
    satcfdi_presentation
    satcfdi_presentationplugin
    satcfdi_application
    satcfdi_domain
    satcfdi_ports
    # D6: lector ZIP, parser y renderer PDF reales sobre ZIP sinteticos.
    satcfdi_cfdi_zip
    satcfdi_cfdi_xml
    satcfdi_cfdi_pdf
    # Escritura del ZIP sintetico (el lector productivo solo lee).
    libzip::zip
    Qt6::Widgets
    Qt6::Quick
    Qt6::Qml
    # QWindowSystemInterface: el foco de Qt de la ventana de la app no depende
    # de que la app sea la activa del sistema (Grabador.cpp, asegurarActiva).
    Qt6::GuiPrivate
    # D17: CGSessionCopyCurrentDictionary (sesion grafica).
    "-framework CoreGraphics"
    "-framework CoreFoundation"
)
satcfdi_enable_warnings(satcfdi_video_tutorial)

# T020 D6: IconoMenuBar.cpp renderiza los SVG del menu bar desde
# :/menubar/...; sin ellos la ilustracion menuBar falla (regresion de T020,
# corregida en T024.4). Mismos archivos que satcfdi_os_macos.
set(_video_menu_svg_dir ${PROJECT_SOURCE_DIR}/resources/branding/menu)
set(_video_menu_svg)
foreach(_lado 18 36)
    foreach(_estado "" "-trabajando" "-pausa" "-atencion")
        list(APPEND _video_menu_svg ${_video_menu_svg_dir}/menu-${_lado}${_estado}.svg)
    endforeach()
endforeach()
qt_add_resources(satcfdi_video_tutorial "satcfdi_video_menubar"
    PREFIX "/menubar"
    BASE ${_video_menu_svg_dir}
    FILES ${_video_menu_svg}
)

# D22: fondo generativo como ShaderEffect con el shader precompilado a .qsb
# (qsb de Qt ShaderTools, sin red): qrc:/satcfdi-video/qml/fondo.frag.qsb.
find_package(Qt6 REQUIRED COMPONENTS ShaderTools)
qt_add_shaders(satcfdi_video_tutorial "satcfdi_video_shaders"
    PREFIX "/satcfdi-video"
    FILES qml/fondo.frag
)

# D10/D15: grafo de enlace sin SAT, Keychain, adaptador macOS, app_core ni
# SQLite. Qt6::Network: ningun target del proyecto en el grafo lo enlaza (las
# bibliotecas de Qt QtQml/QtQuick si dependen de QtNetwork; ver README).
_satcfdi_dependencias_link(satcfdi_video_tutorial _satcfdi_deps_video)
foreach(_prohibido satcfdi_sat satcfdi_secrets_macos satcfdi_os_macos satcfdi_app_core satcfdi_crypto
                   satcfdi_infrastructure Qt6::Sql Qt::Sql)
    if(_prohibido IN_LIST _satcfdi_deps_video)
        message(FATAL_ERROR "satcfdi_video_tutorial no debe enlazar ${_prohibido} (T017 D10)")
    endif()
endforeach()
foreach(_dep IN LISTS _satcfdi_deps_video)
    if(_dep MATCHES "^satcfdi_")
        foreach(_propiedad LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_libs ${_dep} ${_propiedad})
            if(_libs AND ("Qt6::Network" IN_LIST _libs OR "Qt::Network" IN_LIST _libs))
                message(FATAL_ERROR "satcfdi_video_tutorial no debe enlazar Qt6::Network (via ${_dep}, T017 D15)")
            endif()
        endforeach()
    endif()
endforeach()

# Guion del tutorial (especialista-guion-tutoriales). Las pruebas usan el de
# ejemplo, mas corto.
set(SATCFDI_VIDEO_GUION "${CMAKE_CURRENT_SOURCE_DIR}/guion-tutorial.json" CACHE FILEPATH
    "Guion del video tutorial (T017)")
# Migracion: un build configurado en el corte 1 guardo el guion de ejemplo.
if(SATCFDI_VIDEO_GUION STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/guion-ejemplo.json")
    set(SATCFDI_VIDEO_GUION "${CMAKE_CURRENT_SOURCE_DIR}/guion-tutorial.json" CACHE FILEPATH
        "Guion del video tutorial (T017)" FORCE)
endif()
set(SATCFDI_VIDEO_SALIDA "${CMAKE_BINARY_DIR}/video")

# Graba el video (corte 1: sin voz) en build/video/, dentro del sandbox sin
# red y con --offline (D15).
set(SATCFDI_VIDEO_CACHE_VOZ "${SATCFDI_VIDEO_SALIDA}/tts-cache")

# Paso de voz (D5, D9, D12): unico ejecutable con cliente HTTP (Qt Network).
# Corre FUERA del sandbox sin red y solo lo ejecuta el usuario.
qt_add_executable(satcfdi_video_voz
    VozPaso.cpp
    MotoresTts.h
    MotoresTts.cpp
    ProcesadoAudio.h
    ProcesadoAudio.cpp
)
target_compile_definitions(satcfdi_video_voz PRIVATE
    SATCFDI_BINARY_DIR="${CMAKE_BINARY_DIR}"
    SATCFDI_FFMPEG="${SATCFDI_FFMPEG}"
    SATCFDI_FFPROBE="${SATCFDI_FFPROBE}"
)
target_link_libraries(satcfdi_video_voz PRIVATE satcfdi_video_guion Qt6::Core Qt6::Network)
satcfdi_enable_warnings(satcfdi_video_voz)

# Graba el video con la voz del cache (build/video/tts-cache) en build/video/,
# dentro del sandbox sin red y con --offline (D15). Sin un clip, falla
# nombrando la escena (nunca llama a OpenAI).
add_custom_target(video_tutorial
    COMMAND ${SATCFDI_VIDEO_SIN_RED} $<TARGET_FILE:satcfdi_video_tutorial> --guion ${SATCFDI_VIDEO_GUION}
            --salida ${SATCFDI_VIDEO_SALIDA} --nombre tutorial --offline --voz ${SATCFDI_VIDEO_CACHE_VOZ}
    DEPENDS satcfdi_video_tutorial
    USES_TERMINAL
    COMMENT "Grabando el video tutorial con voz en ${SATCFDI_VIDEO_SALIDA} (sin red)"
)
# Vista previa sin voz (duraciones estimadas, sin pista de audio).
add_custom_target(video_tutorial_sin_voz
    COMMAND ${SATCFDI_VIDEO_SIN_RED} $<TARGET_FILE:satcfdi_video_tutorial> --guion ${SATCFDI_VIDEO_GUION}
            --salida ${SATCFDI_VIDEO_SALIDA} --nombre tutorial-sin-voz --offline
    DEPENDS satcfdi_video_tutorial
    USES_TERMINAL
    COMMENT "Grabando el video tutorial sin voz en ${SATCFDI_VIDEO_SALIDA} (sin red)"
)
# Paso de voz con OpenAI (D12: lo ejecuta el usuario; pide confirmacion
# interactiva antes de llamar). Requiere OPENAI_API_KEY en el entorno (D9).
add_custom_target(video_tutorial_voz
    COMMAND $<TARGET_FILE:satcfdi_video_voz> --guion ${SATCFDI_VIDEO_GUION} --cache ${SATCFDI_VIDEO_CACHE_VOZ}
    DEPENDS satcfdi_video_voz
    USES_TERMINAL
    COMMENT "Paso de voz (OpenAI TTS) sobre ${SATCFDI_VIDEO_CACHE_VOZ}"
)
# Sin llamadas: lineas faltantes, caracteres y costo estimado.
add_custom_target(video_tutorial_voz_estimar
    COMMAND $<TARGET_FILE:satcfdi_video_voz> --guion ${SATCFDI_VIDEO_GUION} --cache ${SATCFDI_VIDEO_CACHE_VOZ}
            --estimar
    DEPENDS satcfdi_video_voz
    USES_TERMINAL
)

# D16: compara la grabacion con el manifiesto versionado y con una segunda
# grabacion del mismo guion y cache (por omision build/video/verificacion/,
# hecha con --salida <build>/video/verificacion --nombre tutorial). Sale != 0
# si algo no coincide.
set(SATCFDI_VIDEO_VERIFICACION_B "${SATCFDI_VIDEO_SALIDA}/verificacion/tutorial.mp4" CACHE FILEPATH
    "Segunda grabacion para la verificacion D16 (T017)")
add_custom_target(video_tutorial_verificar
    COMMAND ${CMAKE_COMMAND} -DMODO=verificar
            -DMANIFIESTO=${CMAKE_CURRENT_SOURCE_DIR}/verificacion-d16.json
            -DGUION=${SATCFDI_VIDEO_GUION}
            -DA=${SATCFDI_VIDEO_SALIDA}/tutorial.mp4 -DB=${SATCFDI_VIDEO_VERIFICACION_B}
            -DFFMPEG=${SATCFDI_FFMPEG} -DFFPROBE=${SATCFDI_FFPROBE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/verificar-d16.cmake
    USES_TERMINAL
    COMMENT "Verificacion D16 del video tutorial"
)
# T024.4 (D16): regenera el manifiesto versionado desde la grabacion aprobada
# (build/video/tutorial.linea-tiempo.json, con voz) y el guion vigente. Escribe
# tools/video-tutorial/verificacion-d16.json: revisa el diff antes de versionarlo.
add_custom_target(video_tutorial_manifiesto
    COMMAND ${CMAKE_COMMAND} -DMODO=generar -DGUION=${SATCFDI_VIDEO_GUION}
            -DLINEA_TIEMPO=${SATCFDI_VIDEO_SALIDA}/tutorial.linea-tiempo.json
            -DMANIFIESTO=${CMAKE_CURRENT_SOURCE_DIR}/verificacion-d16.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/verificar-d16.cmake
    USES_TERMINAL
    COMMENT "Regenerando verificacion-d16.json desde la grabacion aprobada"
)

# Huella SHA-256 de notificacionLocalDe (src/app_core/IntegracionWorker.cpp)
# con la que se sincronizo NotificacionesIlustradas.cpp.
set(SATCFDI_VIDEO_HUELLA_NOTIFICACIONES "176552654268ea300b97e0eb65c0ae1e75050ada0443192e047681c698a1c4b7")

# --- Pruebas (label video) -----------------------------------------------------
set(_video_prueba_dir "${CMAKE_BINARY_DIR}/video-pruebas")
set(_video_guion "${CMAKE_CURRENT_SOURCE_DIR}/guion-ejemplo.json")

# D13: el guion de ejemplo es valido (sin interfaz grafica).
add_test(NAME satcfdi_video_guion_ejemplo COMMAND satcfdi_video_tutorial --guion ${_video_guion} --validar)
# D13: el guion del tutorial es valido (presupuestos y 5-7 min), sin grabar.
add_test(NAME satcfdi_video_guion_tutorial
    COMMAND ${CMAKE_COMMAND} -DHERRAMIENTA=$<TARGET_FILE:satcfdi_video_tutorial>
            -DGUION=${CMAKE_CURRENT_SOURCE_DIR}/guion-tutorial.json -DCACHE=${SATCFDI_VIDEO_CACHE_VOZ}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/validar-tutorial.cmake)
# T024.4: --escenas con un id que no es del guion falla y lista los validos.
add_test(NAME satcfdi_video_escenas_id_invalido
    COMMAND satcfdi_video_tutorial --guion ${CMAKE_CURRENT_SOURCE_DIR}/guion-tutorial.json --validar
            --escenas perfil/bienvenida,no-existe)
set_tests_properties(satcfdi_video_escenas_id_invalido PROPERTIES LABELS "video;tools"
    PASS_REGULAR_EXPRESSION "--escenas: \"no-existe\" no es una escena del guion; usa capitulo/escena: perfil/bienvenida")
# T024.4: el id corto unico vale e imprime la duracion de la muestra.
add_test(NAME satcfdi_video_escenas_id_corto
    COMMAND satcfdi_video_tutorial --guion ${CMAKE_CURRENT_SOURCE_DIR}/guion-tutorial.json --validar
            --escenas bienvenida,pdf)
set_tests_properties(satcfdi_video_escenas_id_corto PROPERTIES LABELS "video;tools"
    PASS_REGULAR_EXPRESSION "escenas: facturas/pdf, perfil/bienvenida -> muestra de [0-9]+ cuadros")
# D13: una escena fuera de su presupuesto falla antes de grabar y la nombra.
add_test(NAME satcfdi_video_guion_presupuesto
    COMMAND satcfdi_video_tutorial --guion ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/guion-excede-presupuesto.json --validar)
set_tests_properties(satcfdi_video_guion_presupuesto PROPERTIES
    PASS_REGULAR_EXPRESSION "escena 'demo/larga': dura [0-9]+ cuadros, fuera de su presupuesto")
# T024.2: esperarExportacion acepta "resultado" opcional (exito por omision) y
# rechaza uno desconocido antes de grabar, nombrando los validos.
add_test(NAME satcfdi_video_guion_resultado_exportacion
    COMMAND satcfdi_video_tutorial --guion ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/guion-exportacion-resultado.json --validar)
set_tests_properties(satcfdi_video_guion_resultado_exportacion PROPERTIES
    PASS_REGULAR_EXPRESSION "resultado de exportacion desconocido \"terminada\" \\(usa exito, advertencia, neutro, error, esperandoNombre\\)")
# T024.4: 7 capitulos, 5-8 min, vocabulario retirado, tecla y avanzarMes.
add_test(NAME satcfdi_video_reglas_tutorial
    COMMAND ${CMAKE_COMMAND} -DHERRAMIENTA=$<TARGET_FILE:satcfdi_video_tutorial>
            -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/pruebas
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/prueba-reglas-tutorial.cmake)
set_tests_properties(satcfdi_video_reglas_tutorial PROPERTIES LABELS "video;tools")
# --salida fuera del build se rechaza.
add_test(NAME satcfdi_video_salida_fuera_build
    COMMAND satcfdi_video_tutorial --guion ${_video_guion} --salida ${CMAKE_CURRENT_SOURCE_DIR}/no-permitido --rapido)
set_tests_properties(satcfdi_video_salida_fuera_build PROPERTIES
    PASS_REGULAR_EXPRESSION "--salida debe estar dentro del directorio de build")
# D17: sin sesion grafica la herramienta termina con 77; ctest lo marca SKIP
# (simulado con SATCFDI_VIDEO_SIN_SESION=1; un SKIP no cuenta como aprobacion).
add_test(NAME satcfdi_video_sin_sesion
    COMMAND ${CMAKE_COMMAND} -E env SATCFDI_VIDEO_SIN_SESION=1
            $<TARGET_FILE:satcfdi_video_tutorial> --guion ${_video_guion} --salida ${_video_prueba_dir} --rapido)
set_tests_properties(satcfdi_video_sin_sesion PROPERTIES SKIP_RETURN_CODE 77)
# D15: la sonda conecta sin sandbox (control) y falla dentro de sin-red.sb.
add_test(NAME satcfdi_video_red_control COMMAND satcfdi_video_tutorial --sonda-red)
set_tests_properties(satcfdi_video_red_control PROPERTIES PASS_REGULAR_EXPRESSION "^RED DISPONIBLE")
add_test(NAME satcfdi_video_red_sandbox COMMAND ${SATCFDI_VIDEO_SIN_RED} $<TARGET_FILE:satcfdi_video_tutorial> --sonda-red)
set_tests_properties(satcfdi_video_red_sandbox PROPERTIES PASS_REGULAR_EXPRESSION "^RED BLOQUEADA: ")
# La copia de las acciones de notificacion sigue al original de app_core.
add_test(NAME satcfdi_video_sync_notificaciones
    COMMAND ${CMAKE_COMMAND} -DORIGEN=${PROJECT_SOURCE_DIR}/src/app_core/IntegracionWorker.cpp
            -DHUELLA=${SATCFDI_VIDEO_HUELLA_NOTIFICACIONES}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/sincronia-notificaciones.cmake)
# D14: las ilustraciones traen su rotulo "Ilustración".
add_test(NAME satcfdi_video_rotulo_ilustraciones
    COMMAND ${CMAKE_COMMAND} -DDIR=${PROJECT_SOURCE_DIR}/tools/capturas-manual/qml
            -DDIR_VIDEO=${CMAKE_CURRENT_SOURCE_DIR}/qml
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/rotulo-ilustraciones.cmake)
set_tests_properties(satcfdi_video_guion_ejemplo satcfdi_video_guion_tutorial satcfdi_video_guion_presupuesto satcfdi_video_guion_resultado_exportacion satcfdi_video_salida_fuera_build
    satcfdi_video_sin_sesion satcfdi_video_red_control satcfdi_video_red_sandbox satcfdi_video_sync_notificaciones
    satcfdi_video_rotulo_ilustraciones PROPERTIES LABELS "video;tools")

# Graficas, dentro del sandbox sin red y con --offline (D15). Sin sesion
# grafica: codigo 77 -> SKIP (D17).
add_test(NAME satcfdi_video_rapido
    COMMAND ${CMAKE_COMMAND}
            "-DHERRAMIENTA=${SATCFDI_VIDEO_SIN_RED};$<TARGET_FILE:satcfdi_video_tutorial>"
            -DGUION=${_video_guion}
            -DSALIDA=${_video_prueba_dir}
            -DFFPROBE=${SATCFDI_FFPROBE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/prueba-rapida.cmake)
# T024.4 (D16): una ilustracion reducida da los mismos pixeles en dos corridas.
add_test(NAME satcfdi_video_ilustracion_determinista
    COMMAND ${CMAKE_COMMAND}
            "-DHERRAMIENTA=${SATCFDI_VIDEO_SIN_RED};$<TARGET_FILE:satcfdi_video_tutorial>"
            -DGUION=${_video_guion}
            -DSALIDA=${_video_prueba_dir}/ilustracion
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/prueba-ilustracion-determinista.cmake)
# D6: documentoError y timeout de la barrera del visor PDF.
add_test(NAME satcfdi_video_pdf_error
    COMMAND ${SATCFDI_VIDEO_SIN_RED} $<TARGET_FILE:satcfdi_video_tutorial> --guion ${_video_guion}
            --salida ${_video_prueba_dir} --nombre pdf-error --rapido --offline --forzar-pdf fallo)
set_tests_properties(satcfdi_video_pdf_error PROPERTIES
    PASS_REGULAR_EXPRESSION "escena '[^']+': documentoError del visor PDF")
add_test(NAME satcfdi_video_pdf_timeout
    COMMAND ${SATCFDI_VIDEO_SIN_RED} $<TARGET_FILE:satcfdi_video_tutorial> --guion ${_video_guion}
            --salida ${_video_prueba_dir} --nombre pdf-timeout --rapido --offline --forzar-pdf sin-respuesta)
set_tests_properties(satcfdi_video_pdf_timeout PROPERTIES
    PASS_REGULAR_EXPRESSION "escena '[^']+': timeout de [0-9]+ ms esperando documentoListo")
# D8/D21/D22: lienzo 2560x1440 y determinismo del fondo y la transcripcion.
add_test(NAME satcfdi_video_composicion
    COMMAND ${CMAKE_COMMAND}
            "-DHERRAMIENTA=${SATCFDI_VIDEO_SIN_RED};$<TARGET_FILE:satcfdi_video_tutorial>"
            -DGUION=${_video_guion}
            -DSALIDA=${_video_prueba_dir}
            -DFFMPEG=${SATCFDI_FFMPEG}
            -DFFPROBE=${SATCFDI_FFPROBE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/prueba-composicion.cmake)
# T022 D7 (C2): contrato de la decision de agarre (Agarre.h) con items QML
# sinteticos, sin grabar.
find_package(Qt6 REQUIRED COMPONENTS Test)
qt_add_executable(satcfdi_video_agarre_tests pruebas/TestAgarre.cpp)
target_include_directories(satcfdi_video_agarre_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(satcfdi_video_agarre_tests PRIVATE Qt6::Test Qt6::Quick Qt6::Qml)
satcfdi_enable_warnings(satcfdi_video_agarre_tests)
add_test(NAME satcfdi_video_agarre COMMAND satcfdi_video_agarre_tests)
set_tests_properties(satcfdi_video_agarre PROPERTIES LABELS "video;tools" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# T024.2: barrera esperarExportacion sobre el servicio real con los puertos en
# memoria de tools/fakes-materiales (sin grabar).
qt_add_executable(satcfdi_video_barrera_exportacion_tests
    pruebas/TestBarreraExportacion.cpp
    BarreraExportacion.h
    BarreraExportacion.cpp
    ResultadosExportacion.h
)
target_include_directories(satcfdi_video_barrera_exportacion_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tools/capturas-manual
)
target_link_libraries(satcfdi_video_barrera_exportacion_tests PRIVATE
    satcfdi_fakes_materiales satcfdi_presentation Qt6::Test Qt6::Core)
satcfdi_enable_warnings(satcfdi_video_barrera_exportacion_tests)
add_test(NAME satcfdi_video_barrera_exportacion COMMAND satcfdi_video_barrera_exportacion_tests)
set_tests_properties(satcfdi_video_barrera_exportacion PROPERTIES LABELS "video;tools"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# T024.4: DemoVideo (descarga guiada simulada, Historial por lotes, respaldo
# gobernado, facturasDescargadas y acciones vigiladas), sin QML.
qt_add_executable(satcfdi_video_demo_tests
    pruebas/TestDemoT0244.cpp
    DatosDemo.h
    DatosDemo.cpp
    NotificacionesIlustradas.h
    NotificacionesIlustradas.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/os/macos/MacOSMapeos.cpp
)
target_include_directories(satcfdi_video_demo_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/tools/capturas-manual
)
target_link_libraries(satcfdi_video_demo_tests PRIVATE
    satcfdi_video_guion satcfdi_fakes_materiales satcfdi_presentation satcfdi_application satcfdi_domain
    satcfdi_ports satcfdi_cfdi_zip satcfdi_cfdi_xml satcfdi_cfdi_pdf libzip::zip Qt6::Test Qt6::Core Qt6::Gui)
satcfdi_enable_warnings(satcfdi_video_demo_tests)
add_test(NAME satcfdi_video_demo COMMAND satcfdi_video_demo_tests)
set_tests_properties(satcfdi_video_demo PROPERTIES LABELS "video;tools" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# T022 D7: perdida del foco de macOS entre press y release (inyectada tras el
# press del clic 2, "Nuevo perfil"): corregida por FiltroEntradaReal y, sin
# el filtro, detectada antes del release con traza y PNG.
foreach(_caso corregida detectada)
    add_test(NAME satcfdi_video_desactivacion_${_caso}
        COMMAND ${CMAKE_COMMAND}
                "-DHERRAMIENTA=${SATCFDI_VIDEO_SIN_RED};$<TARGET_FILE:satcfdi_video_tutorial>"
                -DGUION=${_video_guion}
                -DSALIDA=${_video_prueba_dir}
                -DCASO=${_caso}
                -DCLIC=2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/prueba-desactivacion.cmake)
endforeach()
# T024.4 (03-motion 4): portada y cierre (marca, velo final, sin ventana).
add_test(NAME satcfdi_video_portada_cierre
    COMMAND ${CMAKE_COMMAND}
            "-DHERRAMIENTA=${SATCFDI_VIDEO_SIN_RED};$<TARGET_FILE:satcfdi_video_tutorial>"
            -DGUION=${CMAKE_CURRENT_SOURCE_DIR}/pruebas/guion-portada.json
            -DDIR=${_video_prueba_dir}/portada
            -DFFMPEG=${SATCFDI_FFMPEG}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/prueba-portada.cmake)
set_tests_properties(satcfdi_video_rapido satcfdi_video_pdf_error satcfdi_video_pdf_timeout satcfdi_video_composicion
    satcfdi_video_portada_cierre satcfdi_video_ilustracion_determinista
    satcfdi_video_desactivacion_corregida satcfdi_video_desactivacion_detectada PROPERTIES
    LABELS "video;tools;grafica"
    SKIP_RETURN_CODE 77
    RUN_SERIAL TRUE
    # Una sola sesion grafica (foco, ventanas y raton sinteticos): ademas de
    # RUN_SERIAL, un candado comun por si otra prueba grafica se agrega sin el.
    RESOURCE_LOCK satcfdi_sesion_grafica
    TIMEOUT 300)

# --- Pruebas del paso de voz (label voz): motor de prueba, sin red ni clave ---
set(_voz_prueba_dir "${CMAKE_BINARY_DIR}/video-pruebas/voz")
foreach(_caso cache_completo alineacion_correcta varias_frases alineacion_falla correccion_manual cambio_pausas cambio_tempo
              cambio_linea pausa_maxima
              fallo_lote sin_clave clave_no_filtrada)
    add_test(NAME satcfdi_video_voz_${_caso}
        COMMAND ${CMAKE_COMMAND}
                -DCASO=${_caso}
                -DVOZ=$<TARGET_FILE:satcfdi_video_voz>
                -DGRABADOR=$<TARGET_FILE:satcfdi_video_tutorial>
                -DGUION_FRASES=${CMAKE_CURRENT_SOURCE_DIR}/pruebas/guion-frases.json
                "-DSIN_RED=${SATCFDI_VIDEO_SIN_RED}"
                -DGUION=${_video_guion}
                -DDIR=${_voz_prueba_dir}/${_caso}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/prueba-voz.cmake)
    set_tests_properties(satcfdi_video_voz_${_caso} PROPERTIES LABELS "video;voz;tools")
endforeach()
# Raiz "voz" del guion: tipo, claves y tipos (sin valores por omision
# silenciosos).
add_test(NAME satcfdi_video_guion_voz
    COMMAND ${CMAKE_COMMAND} -DGRABADOR=$<TARGET_FILE:satcfdi_video_tutorial>
            -DGUION=${CMAKE_CURRENT_SOURCE_DIR}/guion-ejemplo.json
            -DDIR=${CMAKE_BINARY_DIR}/video-pruebas/guion-voz
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/prueba-guion-voz.cmake)
set_tests_properties(satcfdi_video_guion_voz PROPERTIES LABELS "video;tools")

# Script D16 con videos sinteticos pequenos (sin sesion grafica).
add_test(NAME satcfdi_video_verificacion_d16
    COMMAND ${CMAKE_COMMAND} -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/verificar-d16.cmake
            -DFFMPEG=${SATCFDI_FFMPEG} -DFFPROBE=${SATCFDI_FFPROBE}
            -DDIR=${CMAKE_BINARY_DIR}/video-pruebas/d16
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/prueba-d16.cmake)
set_tests_properties(satcfdi_video_verificacion_d16 PROPERTIES LABELS "video;tools" TIMEOUT 120)

# --- Musica de fondo (R4, label musica): sin sesion grafica (--solo-audio),
# voz del motor de prueba, sin red ni clave ---
# portada_cierre (T024.4): portada sin voz con entrada de 2 s; cierre con la
# musica anclada (fundido 45-135 y silencio hasta el final).
foreach(_caso configuracion mezcla portada_cierre)
    add_test(NAME satcfdi_video_musica_${_caso}
        COMMAND ${CMAKE_COMMAND}
                -DCASO=${_caso}
                -DFFMPEG=${SATCFDI_FFMPEG}
                -DVOZ=$<TARGET_FILE:satcfdi_video_voz>
                -DGRABADOR=$<TARGET_FILE:satcfdi_video_tutorial>
                -DGUION=${CMAKE_CURRENT_SOURCE_DIR}/guion-ejemplo.json
                -DDIR=${CMAKE_BINARY_DIR}/video-pruebas/musica/${_caso}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/prueba-musica.cmake)
    set_tests_properties(satcfdi_video_musica_${_caso} PROPERTIES LABELS "video;musica;tools" TIMEOUT 300)
endforeach()
# Grabacion con voz (grafica, sandbox sin red): pista AAC, alineacion de cues
# (+-1 cuadro), loudness (-17 a -15 LUFS, pico <= -1 dBTP) y duraciones.
add_test(NAME satcfdi_video_con_voz
    COMMAND ${CMAKE_COMMAND}
            -DVOZ=$<TARGET_FILE:satcfdi_video_voz>
            "-DHERRAMIENTA=${SATCFDI_VIDEO_SIN_RED};$<TARGET_FILE:satcfdi_video_tutorial>"
            -DGUION=${_video_guion}
            -DDIR=${_voz_prueba_dir}/grabacion
            -DFFMPEG=${SATCFDI_FFMPEG}
            -DFFPROBE=${SATCFDI_FFPROBE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruebas/prueba-video-voz.cmake)
set_tests_properties(satcfdi_video_con_voz PROPERTIES
    LABELS "video;voz;tools;grafica"
    SKIP_RETURN_CODE 77
    RUN_SERIAL TRUE
    # Una sola sesion grafica (foco, ventanas y raton sinteticos): ademas de
    # RUN_SERIAL, un candado comun por si otra prueba grafica se agrega sin el.
    RESOURCE_LOCK satcfdi_sesion_grafica
    TIMEOUT 600)
