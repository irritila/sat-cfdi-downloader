# satcfdi_tests: pruebas unitarias Qt Test de dominio, puertos y aplicacion
# (servicios demo y persistidos con fakes, contrato T003, dispatcher,
# RegexLogSanitizer, contrato OSIntegration y servicio de configuracion con
# fakes; T005: tipos SecretStore y CredencialesSatService con FakeSecretStore).
# Sin presentacion, SQL, red ni archivos.
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
    TestOSIntegration.h
    TestOSIntegration.cpp
    TestConfiguracionAppService.h
    TestConfiguracionAppService.cpp
    TestCredencialesSat.h
    TestCredencialesSat.cpp
    TestPreparacionPerfiles.h
    TestPreparacionPerfiles.cpp
    TestContratoT007.h
    TestContratoT007.cpp
    EntornoOperaciones.h
    TestOperacionExecutor.h
    TestOperacionExecutor.cpp
    TestWorkerLocal.h
    TestWorkerLocal.cpp
    TestRecuperacionArchivos.h
    TestRecuperacionArchivos.cpp
    TestAlmacenamientoPaquetes.h
    TestAlmacenamientoPaquetes.cpp
    TestOperacionesSatProductivo.h
    TestOperacionesSatProductivo.cpp
    TestNotificaciones.h
    TestNotificaciones.cpp
    TestAccesoPaquetes.h
    TestAccesoPaquetes.cpp
    # Fakes compartidos header-only (T004); listados aqui para AUTOMOC.
    ../fakes/FakeOSIntegration.h
    ../fakes/FakeConfiguracionAppService.h
    ../fakes/FakeSecretStore.h
    ../fakes/PromesasPendientes.h
    ../fakes/FakePerfilesSatService.h
    ../fakes/FakeCredencialesSatService.h
    ../fakes/FakeOperacionesSat.h
    ../fakes/FakeProgramador.h
    ../fakes/FakePackageStorage.h
    ../fakes/FakeSatGateway.h
    ../fakes/FakeAccesoCredencialSat.h
    ../fakes/FakeOperacionesSatConEventLoop.h
)

# Permite #include "fakes/FakeOSIntegration.h".
target_include_directories(satcfdi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(satcfdi_tests PRIVATE
    satcfdi_domain
    satcfdi_ports
    satcfdi_application
    Qt6::Test
)

add_test(NAME satcfdi_tests COMMAND satcfdi_tests)
set_tests_properties(satcfdi_tests PROPERTIES LABELS unit)

satcfdi_enable_warnings(satcfdi_tests)
