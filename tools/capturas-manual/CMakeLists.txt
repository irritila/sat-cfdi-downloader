# tools/capturas-manual (T012 D2-D7): genera docs/manual/img/*.png con la UI
# real sobre servicios fijos y las ilustraciones de qml/. Solo con
# -DSATCFDI_BUILD_MANUAL_CAPTURAS=ON; no es parte de la app ni de ctest.
qt_add_executable(satcfdi_manual_capturas
    main.cpp
    ServiciosFijos.h
    # MacOSMapeos: textos variables del menu bar (MenuBarDefinicion, D6).
    ${PROJECT_SOURCE_DIR}/src/infrastructure/os/macos/MacOSMapeos.cpp
)
target_include_directories(satcfdi_manual_capturas PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(satcfdi_manual_capturas PRIVATE
    SATCFDI_CAPTURAS_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
target_link_libraries(satcfdi_manual_capturas PRIVATE
    satcfdi_presentation
    satcfdi_presentationplugin
    satcfdi_application
    satcfdi_domain
    satcfdi_ports
    Qt6::Widgets
    Qt6::Quick
    Qt6::Qml
)
satcfdi_enable_warnings(satcfdi_manual_capturas)

# Ejecuta la herramienta en una sesion grafica real (D4: cocoa, no offscreen).
add_custom_target(manual_capturas
    COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=cocoa
            $<TARGET_FILE:satcfdi_manual_capturas> --salida ${PROJECT_SOURCE_DIR}/docs/manual/img
    DEPENDS satcfdi_manual_capturas
    USES_TERMINAL
    COMMENT "Generando las imagenes del manual en docs/manual/img"
)
