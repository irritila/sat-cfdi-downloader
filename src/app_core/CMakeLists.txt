# satcfdi_app_core: arranque (AppBootstrapper), composition root persistido
# (T003 DA5), ciclo de vida (AppLifecycleController) e instancia unica
# (SingleInstanceCoordinator) de T004. Lo consumen satcfdi_app y
# satcfdi_integration_tests. No enlaza Qt6::Sql (solo infraestructura) ni
# Widgets (el dialogo de error vive en main.cpp). Qt6::Network solo por
# QLocalServer/QLocalSocket (IPC local, sin red).
qt_add_library(satcfdi_app_core STATIC
    AccesoFinder.h
    AccesoFinder.cpp
    AppBootstrapper.h
    AppBootstrapper.cpp
    AppCompositionRoot.h
    AppCompositionRoot.cpp
    AppLifecycleController.h
    ArranqueProceso.h
    ArranqueProceso.cpp
    AppLifecycleController.cpp
    ExtensionCicloDeVida.h
    IntegracionWorker.h
    IntegracionWorker.cpp
    ProgramadorQt.h
    ProgramadorQt.cpp
    SingleInstanceCoordinator.h
    SingleInstanceCoordinator.cpp
)

# Includes como "app_core/AppBootstrapper.h" desde src/.
target_include_directories(satcfdi_app_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# El canal de instancia unica usa el mismo identificador que el bundle.
target_compile_definitions(satcfdi_app_core PRIVATE
    SATCFDI_BUNDLE_ID="${SATCFDI_BUNDLE_IDENTIFIER}"
)

target_link_libraries(satcfdi_app_core
    PUBLIC
        satcfdi_presentation
        # Plugin estatico del modulo QML: lo necesita todo ejecutable que
        # llame loadFromModule("SatCfdiDownloader", ...).
        satcfdi_presentationplugin
        satcfdi_application
        satcfdi_infrastructure
        # T008: FilesystemPackageStorage (raiz de paquetes, D8).
        satcfdi_storage
        # T009: SatGatewayProductivo (lo usa el adaptador OperacionesSat
        # productivo en el hilo del ejecutor; nunca desde el hilo grafico).
        satcfdi_sat
        satcfdi_domain
        satcfdi_ports
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
    PRIVATE
        Qt6::Network
)

satcfdi_enable_warnings(satcfdi_app_core)
