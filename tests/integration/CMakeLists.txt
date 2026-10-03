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
)

# Permite #include "fakes/FakeOSIntegration.h".
target_include_directories(satcfdi_integration_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tests
)

target_link_libraries(satcfdi_integration_tests PRIVATE
    satcfdi_app_core
    Qt6::Gui
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
