# satcfdi_infrastructure_tests: pruebas Qt Test de la persistencia SQLite real
# (T003) sobre QTemporaryDir. Unico target de pruebas que enlaza Qt6::Sql (para
# fixtures y verificaciones de bajo nivel). Desde T004 incluye configuracion_app
# y ConfiguracionAppServicePersistido con PersistenceDispatcher real. T005:
# credencial_sat (repositorio y migracion 002). Sin red, Keychain ni ZIP.
# sql/verify_t001.py (T001) se conserva como verificacion independiente.
qt_add_executable(satcfdi_infrastructure_tests
    main.cpp
    SqlitePruebasComun.h
    TestSqliteMigraciones.h
    TestSqliteMigraciones.cpp
    TestSqliteConexion.h
    TestSqliteConexion.cpp
    TestSqliteRepositorios.h
    TestSqliteRepositorios.cpp
    TestSqliteCentinelas.h
    TestSqliteCentinelas.cpp
    TestSqliteConfiguracion.h
    TestSqliteConfiguracion.cpp
    TestSqliteCredenciales.h
    TestSqliteCredenciales.cpp
    TestSqlitePerfiles.h
    TestSqlitePerfiles.cpp
    TestSqliteOperaciones.h
    TestSqliteOperaciones.cpp
    TestPackageStorageFs.h
    TestPackageStorageFs.cpp
    ../fakes/FakePackageStorage.h
)

target_compile_definitions(satcfdi_infrastructure_tests PRIVATE
    SATCFDI_MIGRACION_001_FUENTE="${CMAKE_CURRENT_SOURCE_DIR}/../../src/infrastructure/persistence/migrations/001_initial_schema.sql"
    SATCFDI_MIGRACION_002_FUENTE="${CMAKE_CURRENT_SOURCE_DIR}/../../src/infrastructure/persistence/migrations/002_credencial_metadata.sql"
    SATCFDI_MIGRACION_003_FUENTE="${CMAKE_CURRENT_SOURCE_DIR}/../../src/infrastructure/persistence/migrations/003_worker_ejecutor.sql"
)

# Permite #include "fakes/FakeSecretStore.h" (T005).
target_include_directories(satcfdi_infrastructure_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(satcfdi_infrastructure_tests PRIVATE
    satcfdi_infrastructure
    # T008: FilesystemPackageStorage.
    satcfdi_storage
    # RegexLogSanitizer real para la prueba de centinelas de secretos.
    satcfdi_application
    satcfdi_domain
    satcfdi_ports
    Qt6::Sql
    Qt6::Test
)

add_test(NAME satcfdi_infrastructure_tests COMMAND satcfdi_infrastructure_tests)
set_tests_properties(satcfdi_infrastructure_tests PROPERTIES LABELS infrastructure)

satcfdi_enable_warnings(satcfdi_infrastructure_tests)
