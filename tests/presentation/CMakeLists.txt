# satcfdi_presentation_tests: Qt Test + QQmlApplicationEngine sobre el modulo
# QML real (DA2, DA7). Inyecta view models con setInitialProperties usando los
# servicios demo (via un espia local) y fakes asincronos con QPromise
# controlados por la prueba (T003). Corre en -platform offscreen.
qt_add_executable(satcfdi_presentation_tests
    TestPresentacion.cpp
    SolicitudesServiceEspia.h
    ServiciosAsincronosFake.h
)

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
