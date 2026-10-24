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
    # T023.4: fuente de Facturas y preferencia del ultimo perfil.
    FuenteFacturasRoot.h
    FuenteFacturasRoot.cpp
    # T025.2: captura consistente para el respaldo (staging y preferencias).
    FuenteRespaldoLocal.h
    FuenteRespaldoLocal.cpp
    # T025.3: id de instalacion local para reconocer los respaldos propios.
    IdInstalacionRespaldo.h
    IdInstalacionRespaldo.cpp
    # T025.4: configuracion del respaldo en preferencias.ini.
    ConfiguracionRespaldoIni.h
    ConfiguracionRespaldoIni.cpp
    # T025.5: restauracion (marcador durable, staging, origen y arranque).
    ArchivosRestauracion.h
    ArchivosRestauracion.cpp
    MarcadorRestauracion.h
    MarcadorRestauracion.cpp
    AreaRestauracionLocal.h
    AreaRestauracionLocal.cpp
    RestauracionArranque.h
    RestauracionArranque.cpp
    DatosPreviosRestauracion.h
    DatosPreviosRestauracion.cpp
    # T025.6 (corte B): adaptadores de los puertos de presentacion del respaldo.
    RespaldoPresentacionRoot.h
    RespaldoPresentacionRoot.cpp
    AppLifecycleController.h
    ArranqueProceso.h
    ArranqueProceso.cpp
    AppLifecycleController.cpp
    ExtensionCicloDeVida.h
    IntegracionWorker.h
    IntegracionWorker.cpp
    # T019 D4/D8: politica de migracion de la raiz de paquetes del home.
    MigracionRaizPaquetes.h
    MigracionRaizPaquetes.cpp
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
    # T025.2: version de la app en los metadatos del respaldo.
    SATCFDI_VERSION_APP="${PROJECT_VERSION}"
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
        # T025.3: contenedor AFB1 del respaldo (OpenSSL 3.2+).
        satcfdi_crypto
        # T009: SatGatewayProductivo (lo usa el adaptador OperacionesSat
        # productivo en el hilo del ejecutor; nunca desde el hilo grafico).
        satcfdi_sat
        # T015.2: lector ZIP (libzip; el bundle pasa a depender de libzip.dylib,
        # empaquetado y firma en T011 o sucesora, T015 D12) y parser de CFDI.
        satcfdi_cfdi_zip
        satcfdi_cfdi_xml
        # T015.4: renderer PDF (Qt Gui, qrcodegen estatico) y cache de PDF.
        satcfdi_cfdi_pdf
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
