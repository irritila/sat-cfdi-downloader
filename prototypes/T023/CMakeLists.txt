# prototypes/T023 (T023.1, D5): prototipo navegable del recorrido Facturas por
# perfil y mes. Solo con -DSATCFDI_BUILD_PROTOTIPO_T023=ON; no forma parte de la
# app, del bundle ni de ctest.
#
# Reutiliza el modulo QML real SatCfdiDownloader (Theme y components/) enlazando
# satcfdi_presentation: esa biblioteca solo depende de application, domain y
# ports (Qt Core/Qml/Quick), sin SQLite, SAT, Keychain ni sistema de archivos.
# Los view models del prototipo son falsos (QML/JS) con datos sinteticos.
qt_add_executable(satcfdi_prototipo_t023
    main.cpp
)

set(_prototipo_qml_files
    qml/Main.qml
    qml/VmPrototipo.qml
    qml/DatosSinteticos.js
    qml/FacturasPage.qml
    qml/ListaMeses.qml
    qml/FranjaMes.qml
    qml/GuiaPrimerUso.qml
    qml/PdfSimulado.qml
    qml/HistorialPage.qml
    qml/DiagnosticoPage.qml
    qml/PerfilesPage.qml
    qml/PanelEscenarios.qml
    qml/NotificacionSimulada.qml
)
foreach(_archivo IN LISTS _prototipo_qml_files)
    get_filename_component(_nombre "${_archivo}" NAME)
    set_source_files_properties("${_archivo}" PROPERTIES QT_RESOURCE_ALIAS "${_nombre}")
endforeach()

qt_add_qml_module(satcfdi_prototipo_t023
    URI PrototipoT023
    VERSION 1.0
    QML_FILES ${_prototipo_qml_files}
    # Para que qmlcachegen/qmllint resuelvan "import SatCfdiDownloader".
    IMPORT_PATH ${PROJECT_BINARY_DIR}/src/presentation
    DEPENDENCIES SatCfdiDownloader
)

target_include_directories(satcfdi_prototipo_t023 PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(satcfdi_prototipo_t023 PRIVATE
    satcfdi_presentation
    satcfdi_presentationplugin
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
)
satcfdi_enable_warnings(satcfdi_prototipo_t023)

# T023.1: el prototipo no enlaza persistencia, SAT, secretos ni archivos de
# paquetes. Se comprueba al configurar (como el control del spike SAT). Qt6::Network
# no se excluye: lo arrastra Qt6::Qml (QQmlNetworkAccessManager), no el prototipo.
_satcfdi_dependencias_link(satcfdi_prototipo_t023 _prototipo_deps)
foreach(_prohibido IN ITEMS satcfdi_infrastructure satcfdi_storage satcfdi_sat satcfdi_crypto
                            satcfdi_secrets_macos satcfdi_cfdi_zip satcfdi_cfdi_xml satcfdi_cfdi_pdf
                            satcfdi_app_core satcfdi_os_macos Qt6::Sql)
    if("${_prohibido}" IN_LIST _prototipo_deps)
        message(FATAL_ERROR "satcfdi_prototipo_t023 no debe enlazar ${_prohibido}")
    endif()
endforeach()

# Capturas por estado (claro y oscuro, 960 y 720 de ancho) en el build.
add_custom_target(prototipo_t023_capturas
    COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=cocoa
            $<TARGET_FILE:satcfdi_prototipo_t023> --capturas ${CMAKE_BINARY_DIR}/capturas-prototipo-t023
    DEPENDS satcfdi_prototipo_t023
    USES_TERMINAL
    COMMENT "Generando capturas del prototipo T023 en ${CMAKE_BINARY_DIR}/capturas-prototipo-t023"
)
