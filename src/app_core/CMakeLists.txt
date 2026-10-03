# satcfdi_app_core: arranque (AppBootstrapper) y composition root persistido
# (T003 DA5). Lo consumen satcfdi_app y satcfdi_integration_tests. No enlaza
# Qt6::Sql (solo infraestructura), Widgets (el dialogo de error vive en
# main.cpp) ni Network.
qt_add_library(satcfdi_app_core STATIC
    AppBootstrapper.h
    AppBootstrapper.cpp
    AppCompositionRoot.h
    AppCompositionRoot.cpp
)

# Includes como "app_core/AppBootstrapper.h" desde src/.
target_include_directories(satcfdi_app_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(satcfdi_app_core
    PUBLIC
        satcfdi_presentation
        # Plugin estatico del modulo QML: lo necesita todo ejecutable que
        # llame loadFromModule("SatCfdiDownloader", ...).
        satcfdi_presentationplugin
        satcfdi_application
        satcfdi_infrastructure
        satcfdi_domain
        satcfdi_ports
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
)

satcfdi_enable_warnings(satcfdi_app_core)
