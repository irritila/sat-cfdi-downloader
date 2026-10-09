# satcfdi_presentation_tests: Qt Test + QQmlApplicationEngine sobre el modulo
# QML real (DA2, DA7). Inyecta view models con setInitialProperties usando los
# servicios demo (via un espia local) y fakes asincronos con QPromise
# controlados por la prueba (T003). Corre en -platform offscreen.
qt_add_executable(satcfdi_presentation_tests
    TestPresentacion.cpp
    EstiloPruebas.cpp
    SolicitudesServiceEspia.h
    ServiciosAsincronosFake.h
    CredencialesSatServiceListo.h
    ../fakes/PromesasPendientes.h
    ../fakes/FakePerfilesSatService.h
    ../fakes/FakeCredencialesSatService.h
)

# Permite #include "fakes/FakePerfilesSatService.h" (T005.1).
target_include_directories(satcfdi_presentation_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)

target_link_libraries(satcfdi_presentation_tests PRIVATE
    satcfdi_presentation
    satcfdi_presentationplugin
    satcfdi_application
    satcfdi_domain
    Qt6::Qml
    Qt6::Quick
    Qt6::Test
)

satcfdi_enable_warnings(satcfdi_presentation_tests)

add_test(NAME satcfdi_presentation_tests COMMAND satcfdi_presentation_tests)
set_tests_properties(satcfdi_presentation_tests PROPERTIES
    LABELS presentation
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# satcfdi_perfiles_vm_tests (T005.1): Qt Test de los view models de perfiles
# y e.firma con fakes de promesas manuales (tests/fakes) y centinelas.
qt_add_executable(satcfdi_perfiles_vm_tests
    TestPerfilesViewModels.cpp
    EstiloPruebas.cpp
    ServiciosAsincronosFake.h
    ../fakes/PromesasPendientes.h
    ../fakes/FakePerfilesSatService.h
    ../fakes/FakeCredencialesSatService.h
)
target_include_directories(satcfdi_perfiles_vm_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(satcfdi_perfiles_vm_tests PRIVATE
    satcfdi_presentation
    satcfdi_application
    satcfdi_domain
    Qt6::Quick
    Qt6::Test
)
satcfdi_enable_warnings(satcfdi_perfiles_vm_tests)
add_test(NAME satcfdi_perfiles_vm_tests COMMAND satcfdi_perfiles_vm_tests)
set_tests_properties(satcfdi_perfiles_vm_tests PROPERTIES
    LABELS presentation
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# satcfdi_presentation_quicktests (T005.1, DA7): Qt Quick Test de
# PerfilesSatPage y EFirmaDialogo sobre el modulo QML real. Los tst_*.qml se
# leen de QUICK_TEST_SOURCE_DIR; FixturePerfiles expone view models sobre
# fakes con promesas manuales.
qt_add_executable(satcfdi_presentation_quicktests
    quick/main.cpp
    EstiloPruebas.cpp
    quick/FixturePerfiles.h
    quick/FixturePerfiles.cpp
    ServiciosAsincronosFake.h
    ../fakes/PromesasPendientes.h
    ../fakes/FakePerfilesSatService.h
    ../fakes/FakeCredencialesSatService.h
)
target_include_directories(satcfdi_presentation_quicktests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_compile_definitions(satcfdi_presentation_quicktests PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/quick"
)
target_link_libraries(satcfdi_presentation_quicktests PRIVATE
    satcfdi_presentation
    satcfdi_presentationplugin
    satcfdi_application
    satcfdi_domain
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickTest
)
satcfdi_enable_warnings(satcfdi_presentation_quicktests)
add_test(NAME satcfdi_presentation_quicktests COMMAND satcfdi_presentation_quicktests)
set_tests_properties(satcfdi_presentation_quicktests PROPERTIES
    LABELS presentation
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# satcfdi_tema_tests (T013 corte 1): estilo Basic, Theme.qml frente a
# tokens.json (claro y oscuro), Icono.qml y formato de fechas (D5) con zonas
# horarias distintas de UTC.
qt_add_executable(satcfdi_tema_tests
    TestTemaYFechas.cpp
    EstiloPruebas.cpp
)
target_compile_definitions(satcfdi_tema_tests PRIVATE
    SATCFDI_TOKENS_JSON="${PROJECT_SOURCE_DIR}/docs/design/ui-ux-v2/tokens.json"
)
target_link_libraries(satcfdi_tema_tests PRIVATE
    satcfdi_presentation
    satcfdi_presentationplugin
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Test
)
satcfdi_enable_warnings(satcfdi_tema_tests)
add_test(NAME satcfdi_tema_tests COMMAND satcfdi_tema_tests)
set_tests_properties(satcfdi_tema_tests PROPERTIES
    LABELS presentation
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
