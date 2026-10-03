# satcfdi_integration_tests: AppBootstrapper + AppCompositionRoot persistido
# sobre QTemporaryDir (T003). Sin Qt6::Sql directo, red ni Keychain.
qt_add_executable(satcfdi_integration_tests
    TestComposicionPersistida.cpp
)

target_link_libraries(satcfdi_integration_tests PRIVATE
    satcfdi_app_core
    Qt6::Gui
    Qt6::Test
)

satcfdi_enable_warnings(satcfdi_integration_tests)

add_test(NAME satcfdi_integration_tests COMMAND satcfdi_integration_tests)
set_tests_properties(satcfdi_integration_tests PROPERTIES
    LABELS integration
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
