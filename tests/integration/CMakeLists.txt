# satcfdi_integration_tests: AppBootstrapper, AppCompositionRoot persistido
# (T003), instancia unica y ciclo de vida con FakeOSIntegration (T004) sobre
# QTemporaryDir. Sin Qt6::Sql directo, SAT, Keychain ni permisos reales.
qt_add_executable(satcfdi_integration_tests
    main.cpp
    TestComposicionPersistida.cpp
    TestInstanciaUnica.cpp
    TestCicloDeVida.cpp
    ../fakes/FakeOSIntegration.h
    ../fakes/FakeConfiguracionAppService.h
    ../fakes/FakeSecretStore.h
    ../fakes/FakeCredencialesSatService.h
    ../fakes/PromesasPendientes.h
    TestCredencialesArranque.cpp
    TestPreparacionPerfiles.cpp
    TestMonitoreo.cpp
    TestPaquetes.cpp
    TestFlujoSat.cpp
    TestNotificacionesAccionables.cpp
    # T015.2: indice de CFDI con ZIP sinteticos (libzip).
    TestIndiceCfdi.cpp
    # T015.4: previsualizacion PDF con el root real (Qt Pdf para leer el PDF).
    TestPrevisualizacionCfdi.cpp
    # T019: raiz efectiva (migrada o vieja por conflicto) en el root real.
    TestRaizPaquetesMigrada.cpp
    # T023.3: descarga guiada con el root real y SAT simulado.
    TestDescargaGuiada.cpp
    # T023.4: preferencia del ultimo perfil de Facturas (archivo real).
    TestFacturasPreferencias.cpp
    ../fakes/LectorCfdiCompuerta.h
    ../fakes/ZipCfdiSintetico.h
    ../fakes/FakeSatGateway.h
    ../fakes/FakePackageStorage.h
    ../fakes/FakeOperacionesSat.h
    ../fakes/FakeProgramador.h
)

# Permite #include "fakes/FakeOSIntegration.h".
target_include_directories(satcfdi_integration_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tests
)

target_link_libraries(satcfdi_integration_tests PRIVATE
    satcfdi_app_core
    # T015.2: genera ZIP sinteticos en la prueba (fakes/ZipCfdiSintetico.h).
    libzip::zip
    Qt6::Gui
    # T015.4: QPdfDocument para extraer el texto del PDF generado.
    Qt6::Pdf
    Qt6::Test
)

satcfdi_enable_warnings(satcfdi_integration_tests)

# Proceso auxiliar para la prueba multiproceso de instancia unica.
qt_add_executable(satcfdi_instance_helper AyudanteInstancia.cpp)
target_link_libraries(satcfdi_instance_helper PRIVATE satcfdi_app_core)
satcfdi_enable_warnings(satcfdi_instance_helper)
add_dependencies(satcfdi_integration_tests satcfdi_instance_helper)
target_compile_definitions(satcfdi_integration_tests PRIVATE
    SATCFDI_INSTANCE_HELPER="$<TARGET_FILE:satcfdi_instance_helper>"
)

add_test(NAME satcfdi_integration_tests COMMAND satcfdi_integration_tests)
set_tests_properties(satcfdi_integration_tests PROPERTIES
    LABELS integration
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# T015.4: AccesoFinder::abrirEnVistaPrevia (directorios permitidos, `..`,
# enlaces que escapan) con FakeOSIntegration y filesystem real.
qt_add_executable(satcfdi_acceso_finder_tests
    TestAccesoFinderVistaPrevia.cpp
    ../fakes/FakeOSIntegration.h
)
target_include_directories(satcfdi_acceso_finder_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(satcfdi_acceso_finder_tests PRIVATE satcfdi_app_core Qt6::Gui Qt6::Test)
satcfdi_enable_warnings(satcfdi_acceso_finder_tests)
add_test(NAME satcfdi_acceso_finder_tests COMMAND satcfdi_acceso_finder_tests)
set_tests_properties(satcfdi_acceso_finder_tests PROPERTIES
    LABELS integration
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# DS5 (T005): centinelas de fuga con MacOSSecretStore real sobre
# FakeKeychainApi, CredencialesSatServicePersistido y SQLite real. Solo Apple.
# Reutiliza FakeKeychainApi.h y FixturesEFirma.h de tests/secrets (sin
# modificarlos) y el openssl que fija tests/secrets (SATCFDI_OPENSSL_EXECUTABLE).
if(APPLE)
    qt_add_executable(satcfdi_credenciales_centinelas_tests
        TestCentinelasCredenciales.cpp
        ../secrets/FakeKeychainApi.h
        ../secrets/FixturesEFirma.h
    )
    target_include_directories(satcfdi_credenciales_centinelas_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/tests/secrets
    )
    target_compile_definitions(satcfdi_credenciales_centinelas_tests PRIVATE
        SATCFDI_OPENSSL_EXECUTABLE="${SATCFDI_OPENSSL_EXECUTABLE}"
        SATCFDI_SCRIPT_FIXTURES="${PROJECT_SOURCE_DIR}/tests/secrets/fixtures/generar_fixtures.sh"
    )
    target_link_libraries(satcfdi_credenciales_centinelas_tests PRIVATE
        satcfdi_app_core
        satcfdi_secrets_macos
        Qt6::Gui
        Qt6::Test
    )
    satcfdi_enable_warnings(satcfdi_credenciales_centinelas_tests)
    add_test(NAME satcfdi_credenciales_centinelas_tests COMMAND satcfdi_credenciales_centinelas_tests)
    set_tests_properties(satcfdi_credenciales_centinelas_tests PROPERTIES
        LABELS "integration;secrets"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()

# T019 (D2, D4, D8): politica de migracion de la raiz de paquetes con
# FileOpsSimulado y home inyectado, PosixFileOps real sobre un home temporal
# (inode, enlaces, RENAME_EXCL, recuperacion), orden en el bootstrap,
# --data-dir y la identidad interna (AppDataLocation y display name).
qt_add_executable(satcfdi_migracion_paquetes_tests
    migracion/TestMigracionRaizPaquetes.cpp
    migracion/FileOpsSimulado.h
)
target_include_directories(satcfdi_migracion_paquetes_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(satcfdi_migracion_paquetes_tests PRIVATE satcfdi_app_core Qt6::Gui Qt6::Test)
satcfdi_enable_warnings(satcfdi_migracion_paquetes_tests)
add_test(NAME satcfdi_migracion_paquetes_tests COMMAND satcfdi_migracion_paquetes_tests)
set_tests_properties(satcfdi_migracion_paquetes_tests PROPERTIES
    LABELS integration
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
