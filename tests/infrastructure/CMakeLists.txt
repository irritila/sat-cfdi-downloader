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
    # T015.2: migracion 006 y catalogo de CFDI.
    TestSqliteCatalogoCfdi.h
    TestSqliteCatalogoCfdi.cpp
    # T023.2: migracion 007, lotes guiados, evidencia y catalogo mensual.
    TestSqliteCatalogoMensual.h
    TestSqliteCatalogoMensual.cpp
    TestPackageStorageFs.h
    TestPackageStorageFs.cpp
    TestPackageStorageRevelable.h
    TestPackageStorageRevelable.cpp
    ../fakes/FakePackageStorage.h
)

target_compile_definitions(satcfdi_infrastructure_tests PRIVATE
    SATCFDI_MIGRACION_001_FUENTE="${CMAKE_CURRENT_SOURCE_DIR}/../../src/infrastructure/persistence/migrations/001_initial_schema.sql"
    SATCFDI_MIGRACION_002_FUENTE="${CMAKE_CURRENT_SOURCE_DIR}/../../src/infrastructure/persistence/migrations/002_credencial_metadata.sql"
    SATCFDI_MIGRACION_003_FUENTE="${CMAKE_CURRENT_SOURCE_DIR}/../../src/infrastructure/persistence/migrations/003_worker_ejecutor.sql"
    SATCFDI_MIGRACION_005_FUENTE="${CMAKE_CURRENT_SOURCE_DIR}/../../src/infrastructure/persistence/migrations/005_aviso_vencimiento_efirma.sql"
    SATCFDI_MIGRACION_004_FUENTE="${CMAKE_CURRENT_SOURCE_DIR}/../../src/infrastructure/persistence/migrations/004_reintento_paquete.sql"
    SATCFDI_MIGRACION_006_FUENTE="${CMAKE_CURRENT_SOURCE_DIR}/../../src/infrastructure/persistence/migrations/006_indice_cfdi.sql"
    SATCFDI_MIGRACION_007_FUENTE="${CMAKE_CURRENT_SOURCE_DIR}/../../src/infrastructure/persistence/migrations/007_catalogo_mensual.sql"
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

# T015.1: LectorZipLibzip (puerto CfdiArchiveReader) con ZIP generados en la
# prueba con libzip sobre QTemporaryDir. Ejecutable propio: no enlaza SQL y
# es el unico target de pruebas que usa libzip directamente.
qt_add_executable(satcfdi_cfdi_zip_tests
    TestLectorZipLibzip.cpp
    ../fakes/FakeCfdiArchiveReader.h
)
target_include_directories(satcfdi_cfdi_zip_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(satcfdi_cfdi_zip_tests PRIVATE
    satcfdi_cfdi_zip
    satcfdi_ports
    satcfdi_domain
    libzip::zip
    Qt6::Test
)
add_test(NAME satcfdi_cfdi_zip_tests COMMAND satcfdi_cfdi_zip_tests)
set_tests_properties(satcfdi_cfdi_zip_tests PROPERTIES LABELS infrastructure)
satcfdi_enable_warnings(satcfdi_cfdi_zip_tests)

# T015.4: PDF de CFDI (URL del QR, RenderizadorPdfCfdi y AlmacenPdfCfdiFs).
# Ejecutable propio con QGuiApplication offscreen (QPdfWriter necesita la base
# de fuentes) y Qt6::Pdf (QPdfDocument) para extraer el texto del PDF,
# contar paginas y rasterizar el QR. qrcodegen se enlaza para comparar cada
# modulo dibujado con el simbolo esperado.
qt_add_executable(satcfdi_cfdi_pdf_tests
    TestCfdiPdf.cpp
    CfdiPdfPruebasComun.h
)
target_include_directories(satcfdi_cfdi_pdf_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(satcfdi_cfdi_pdf_tests PRIVATE
    satcfdi_cfdi_pdf
    satcfdi_cfdi_xml
    satcfdi_qrcodegen
    satcfdi_ports
    satcfdi_domain
    Qt6::Gui
    Qt6::Pdf
    Qt6::Test
)
add_test(NAME satcfdi_cfdi_pdf_tests COMMAND satcfdi_cfdi_pdf_tests)
set_tests_properties(satcfdi_cfdi_pdf_tests PROPERTIES
    LABELS infrastructure
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
satcfdi_enable_warnings(satcfdi_cfdi_pdf_tests)
