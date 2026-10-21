# satcfdi_presentation_tests: Qt Test + QQmlApplicationEngine sobre el modulo
# QML real (DA2, DA7). Inyecta view models con setInitialProperties usando los
# servicios demo (via un espia local) y fakes asincronos con QPromise
# controlados por la prueba (T003). Corre en -platform offscreen.
qt_add_executable(satcfdi_presentation_tests
    TestPresentacion.cpp
    FuenteFacturasFake.h
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

# satcfdi_explorador_tests (T015.3): ExploradorCfdiViewModel, CfdiListModel,
# "Ver facturas" del detalle y navegacion, con ConsultaCatalogoFake.
qt_add_executable(satcfdi_explorador_tests
    TestExploradorCfdi.cpp
    EstiloPruebas.cpp
    ConsultaCatalogoFake.h
    ServiciosAsincronosFake.h
    ../fakes/PromesasPendientes.h
    ../fakes/FakePerfilesSatService.h
    ../fakes/FakeCredencialesSatService.h
)
target_include_directories(satcfdi_explorador_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(satcfdi_explorador_tests PRIVATE
    satcfdi_presentation
    satcfdi_application
    satcfdi_domain
    Qt6::Quick
    Qt6::Test
)
satcfdi_enable_warnings(satcfdi_explorador_tests)
add_test(NAME satcfdi_explorador_tests COMMAND satcfdi_explorador_tests)
set_tests_properties(satcfdi_explorador_tests PROPERTIES
    LABELS presentation
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# satcfdi_visor_pdf_tests (T015.4): VisorPdfCfdiViewModel y su conexion con
# el explorador, con PrevisualizacionFake y FinderVistaPreviaFake.
qt_add_executable(satcfdi_visor_pdf_tests
    TestVisorPdfCfdi.cpp
    ConsultaCatalogoFake.h
    PrevisualizacionFake.h
    ../fakes/PromesasPendientes.h
)
target_include_directories(satcfdi_visor_pdf_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(satcfdi_visor_pdf_tests PRIVATE
    satcfdi_presentation
    satcfdi_application
    satcfdi_domain
    Qt6::Test
)
satcfdi_enable_warnings(satcfdi_visor_pdf_tests)
add_test(NAME satcfdi_visor_pdf_tests COMMAND satcfdi_visor_pdf_tests)
set_tests_properties(satcfdi_visor_pdf_tests PROPERTIES
    LABELS presentation
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# satcfdi_aviso_raiz_tests (T019 D8): AvisoRaizPaquetesViewModel sin aviso
# y con cada motivo (conflicto, error, raiz no disponible).
qt_add_executable(satcfdi_aviso_raiz_tests
    TestAvisoRaizPaquetes.cpp
)
target_link_libraries(satcfdi_aviso_raiz_tests PRIVATE
    satcfdi_presentation
    Qt6::Test
)
satcfdi_enable_warnings(satcfdi_aviso_raiz_tests)
add_test(NAME satcfdi_aviso_raiz_tests COMMAND satcfdi_aviso_raiz_tests)
set_tests_properties(satcfdi_aviso_raiz_tests PROPERTIES
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
    quick/FixtureExplorador.h
    quick/FixtureExplorador.cpp
    quick/FixtureFacturas.h
    quick/FixtureFacturas.cpp
    quick/FixtureAvisoRaiz.h
    ConsultaCatalogoFake.h
    PrevisualizacionFake.h
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

# satcfdi_historial_vm_tests (T023.5): regla del historial, HistorialViewModel
# y navegacion de AppViewModel (Historial, Diagnostico, Atras, Facturas por mes).
qt_add_executable(satcfdi_historial_vm_tests
    TestHistorialViewModel.cpp
    FuenteFacturasFake.h
    ServiciosAsincronosFake.h
    ../fakes/PromesasPendientes.h
    ../fakes/FakePerfilesSatService.h
    ../fakes/FakeCredencialesSatService.h
)
target_include_directories(satcfdi_historial_vm_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(satcfdi_historial_vm_tests PRIVATE
    satcfdi_presentation
    satcfdi_application
    satcfdi_domain
    Qt6::Quick
    Qt6::Test
)
satcfdi_enable_warnings(satcfdi_historial_vm_tests)
add_test(NAME satcfdi_historial_vm_tests COMMAND satcfdi_historial_vm_tests)
set_tests_properties(satcfdi_historial_vm_tests PROPERTIES
    LABELS presentation
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# satcfdi_facturas_vm_tests (T023.4): FacturasViewModel, franja, meses y lista
# con una FuenteFacturasMes falsa y evaluaciones reales de cobertura.
qt_add_executable(satcfdi_facturas_vm_tests
    TestFacturasViewModel.cpp
    FuenteFacturasFake.h
    ServiciosAsincronosFake.h
    ../fakes/PromesasPendientes.h
    ../fakes/FakePerfilesSatService.h
    ../fakes/FakeCredencialesSatService.h
)
target_include_directories(satcfdi_facturas_vm_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(satcfdi_facturas_vm_tests PRIVATE
    satcfdi_presentation
    satcfdi_application
    satcfdi_domain
    Qt6::Quick
    Qt6::Test
)
satcfdi_enable_warnings(satcfdi_facturas_vm_tests)
add_test(NAME satcfdi_facturas_vm_tests COMMAND satcfdi_facturas_vm_tests)
set_tests_properties(satcfdi_facturas_vm_tests PROPERTIES
    LABELS presentation
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
