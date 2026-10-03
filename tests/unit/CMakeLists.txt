# satcfdi_tests: pruebas unitarias Qt Test de dominio, puertos y aplicacion
# (servicios demo y persistidos con fakes, contrato T003, dispatcher,
# RegexLogSanitizer). Sin presentacion, SQL, red ni archivos.
qt_add_executable(satcfdi_tests
    main.cpp
    TestDominio.h
    TestDominio.cpp
    TestPuertos.h
    TestPuertos.cpp
    TestResultado.h
    TestResultado.cpp
    TestDemoSolicitudesService.h
    TestDemoSolicitudesService.cpp
    TestDemoPerfilesSatService.h
    TestDemoPerfilesSatService.cpp
    TestContratoT003.h
    TestContratoT003.cpp
    FakesPersistencia.h
    TestServiciosPersistidos.h
    TestServiciosPersistidos.cpp
    TestRegexLogSanitizer.h
    TestRegexLogSanitizer.cpp
)

target_link_libraries(satcfdi_tests PRIVATE
    satcfdi_domain
    satcfdi_ports
    satcfdi_application
    Qt6::Test
)

add_test(NAME satcfdi_tests COMMAND satcfdi_tests)
set_tests_properties(satcfdi_tests PROPERTIES LABELS unit)

satcfdi_enable_warnings(satcfdi_tests)
