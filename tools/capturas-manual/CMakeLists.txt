# tools/capturas-manual (T012 D2-D7, T024.2): genera docs/manual/img/*.png con
# la UI real sobre los fakes sinteticos comunes (tools/fakes-materiales) y
# servicios fijos, y las ilustraciones de qml/. Las capturas y su entorno salen
# del manifiesto escenarios.json. Solo con -DSATCFDI_BUILD_MANUAL_CAPTURAS=ON;
# sus pruebas (label capturas) solo existen con la opcion.
qt_add_executable(satcfdi_manual_capturas
    main.cpp
    ServiciosFijos.h
    # MacOSMapeos: textos variables del menu bar (MenuBarDefinicion, D6), del
    # icono y de las acciones de notificacion (T016).
    ${PROJECT_SOURCE_DIR}/src/infrastructure/os/macos/MacOSMapeos.cpp
    # T016: dibujo real de las variantes del icono del menu bar (QPainter, sin AppKit).
    ${PROJECT_SOURCE_DIR}/src/infrastructure/os/macos/IconoMenuBar.cpp
)
target_include_directories(satcfdi_manual_capturas PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(satcfdi_manual_capturas PRIVATE
    SATCFDI_CAPTURAS_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    SATCFDI_CAPTURAS_MANIFIESTO="${CMAKE_CURRENT_SOURCE_DIR}/escenarios.json"
)
target_link_libraries(satcfdi_manual_capturas PRIVATE
    # T024.2 (T024 D9): Facturas, Historial, Exportacion y Respaldo sinteticos.
    satcfdi_fakes_materiales
    # T016: notificacionLocalDe (acciones de notificacion por tipo, T014.4 D1).
    satcfdi_app_core
    satcfdi_presentation
    satcfdi_presentationplugin
    satcfdi_application
    satcfdi_domain
    satcfdi_ports
    # T015.4: PDF del visor con el parser y el renderer reales (XML sintetico).
    satcfdi_cfdi_xml
    satcfdi_cfdi_pdf
    Qt6::Widgets
    Qt6::Quick
    # T024.3: QPdfDocument comprueba que el PDF del visor es la factura de la fila.
    Qt6::Pdf
    Qt6::Qml
)
satcfdi_enable_warnings(satcfdi_manual_capturas)

# T020 D6: IconoMenuBar.cpp renderiza los SVG del menu bar desde
# :/menubar/...; sin ellos las ilustraciones 11 y 15 no tenian icono
# (regresion de T020, corregida en T024.4). Mismos archivos que
# satcfdi_os_macos.
set(_capturas_menu_svg_dir ${PROJECT_SOURCE_DIR}/resources/branding/menu)
set(_capturas_menu_svg)
foreach(_lado 18 36)
    foreach(_estado "" "-trabajando" "-pausa" "-atencion")
        list(APPEND _capturas_menu_svg ${_capturas_menu_svg_dir}/menu-${_lado}${_estado}.svg)
    endforeach()
endforeach()
qt_add_resources(satcfdi_manual_capturas "satcfdi_capturas_menubar"
    PREFIX "/menubar"
    BASE ${_capturas_menu_svg_dir}
    FILES ${_capturas_menu_svg}
)

# T024.2: la herramienta fija su entorno (offscreen, Qt Quick software, escala
# 2, reloj de animaciones virtual); no requiere sesion grafica ni roba el foco.
add_custom_target(manual_capturas
    COMMAND $<TARGET_FILE:satcfdi_manual_capturas> --tema claro --salida ${PROJECT_SOURCE_DIR}/docs/manual/img
    DEPENDS satcfdi_manual_capturas
    USES_TERMINAL
    COMMENT "Generando las imagenes del manual en docs/manual/img"
)

# T013 D9: las mismas pantallas en tema oscuro, como evidencia de revision.
# Se escriben en el build (fuera del repo); el manual sigue en claro.
add_custom_target(manual_capturas_oscuro
    COMMAND $<TARGET_FILE:satcfdi_manual_capturas> --tema oscuro --salida ${CMAKE_BINARY_DIR}/capturas-oscuro
    DEPENDS satcfdi_manual_capturas
    USES_TERMINAL
    COMMENT "Generando las capturas en oscuro en ${CMAKE_BINARY_DIR}/capturas-oscuro"
)

# T024.2: dos ejecuciones limpias en el build (capturas-reproducibles/a y /b)
# deben dar el mismo SHA-256 por captura del manifiesto; una diferencia falla.
# Despues, escaneo de las salidas (PNG y textos visibles): solo los RFC
# genericos del SAT, sin el usuario, nombre completo, equipo ni HOME reales.
set(_capturas_verificacion "${CMAKE_BINARY_DIR}/capturas-reproducibles")
add_test(NAME satcfdi_capturas_reproducibles
    COMMAND ${CMAKE_COMMAND}
            -DHERRAMIENTA=$<TARGET_FILE:satcfdi_manual_capturas>
            -DMANIFIESTO=${CMAKE_CURRENT_SOURCE_DIR}/escenarios.json
            -DDIR=${_capturas_verificacion}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/verificar-capturas.cmake)
add_test(NAME satcfdi_capturas_escaneo
    COMMAND ${CMAKE_COMMAND}
            -DDIR=${_capturas_verificacion}/a
            -P ${CMAKE_CURRENT_SOURCE_DIR}/escanear-capturas.cmake)
set_tests_properties(satcfdi_capturas_reproducibles PROPERTIES
    LABELS "capturas;tools" RUN_SERIAL TRUE TIMEOUT 900 FIXTURES_SETUP capturas_generadas)
set_tests_properties(satcfdi_capturas_escaneo PROPERTIES
    LABELS "capturas;tools" FIXTURES_REQUIRED capturas_generadas)
# T024.2: una asercion equivocada hace fallar la captura (no basta el SHA).
add_test(NAME satcfdi_capturas_asercion_negativa
    COMMAND ${CMAKE_COMMAND}
            -DHERRAMIENTA=$<TARGET_FILE:satcfdi_manual_capturas>
            -DMANIFIESTO=${CMAKE_CURRENT_SOURCE_DIR}/escenarios.json
            -DDIR=${CMAKE_BINARY_DIR}/capturas-asercion-negativa
            -P ${CMAKE_CURRENT_SOURCE_DIR}/asercion-negativa.cmake)
set_tests_properties(satcfdi_capturas_asercion_negativa PROPERTIES LABELS "capturas;tools" TIMEOUT 120)
add_custom_target(manual_capturas_verificar
    COMMAND ${CMAKE_COMMAND}
            -DHERRAMIENTA=$<TARGET_FILE:satcfdi_manual_capturas>
            -DMANIFIESTO=${CMAKE_CURRENT_SOURCE_DIR}/escenarios.json
            -DDIR=${_capturas_verificacion}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/verificar-capturas.cmake
    COMMAND ${CMAKE_COMMAND} -DDIR=${_capturas_verificacion}/a
            -P ${CMAKE_CURRENT_SOURCE_DIR}/escanear-capturas.cmake
    DEPENDS satcfdi_manual_capturas
    USES_TERMINAL
    COMMENT "T024.2: reproducibilidad (SHA-256 de dos ejecuciones) y escaneo de las capturas"
)
