# satcfdi_infrastructure: adaptadores de infraestructura. Desde T003 contiene
# la persistencia SQLite (ADR 0016): proveedor de conexiones por hilo, runner de
# migraciones, repositorios y UnitOfWork. Sin Qt6::Network, Keychain ni ZIP.
# Qt6::Sql lo busca el CMake raiz.

qt_add_library(satcfdi_infrastructure STATIC
    persistence/sqlite/SqliteTipos.h
    persistence/sqlite/SqliteSoporte.h
    persistence/sqlite/SqliteSoporte.cpp
    persistence/sqlite/SqliteConnectionProvider.h
    persistence/sqlite/SqliteConnectionProvider.cpp
    persistence/sqlite/SqliteMigrationRunner.h
    persistence/sqlite/SqliteMigrationRunner.cpp
    persistence/sqlite/SqliteUnitOfWork.h
    persistence/sqlite/SqliteUnitOfWork.cpp
    persistence/sqlite/SqliteRepositorios.h
    persistence/sqlite/SqlitePerfilSatRepository.cpp
    persistence/sqlite/SqliteSolicitudMasivaRepository.cpp
    persistence/sqlite/SqlitePaqueteSolicitudRepository.cpp
    persistence/sqlite/SqliteLogSolicitudRepository.cpp
    persistence/sqlite/SqliteConfiguracionAppRepository.cpp
    persistence/sqlite/SqliteCredencialSatRepository.cpp
    persistence/sqlite/SqliteFilas.h
    persistence/sqlite/SqliteFilas.cpp
    persistence/sqlite/SqliteOperacionesConsultas.h
    persistence/sqlite/SqliteOperacionesSolicitudRepository.cpp
    persistence/sqlite/SqlitePersistencia.h
    persistence/sqlite/SqlitePersistencia.cpp
)

# Migraciones embebidas: `:/migrations/00N_*.sql` (lista en SqliteMigrationRunner.cpp).
qt_add_resources(satcfdi_infrastructure "satcfdi_migrations"
    PREFIX "/migrations"
    BASE "persistence/migrations"
    FILES
        persistence/migrations/001_initial_schema.sql
        persistence/migrations/002_credencial_metadata.sql
        persistence/migrations/003_worker_ejecutor.sql
)

target_include_directories(satcfdi_infrastructure PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(satcfdi_infrastructure
    PUBLIC
        satcfdi_domain
        satcfdi_ports
        Qt6::Core
    PRIVATE
        Qt6::Sql
)

satcfdi_enable_warnings(satcfdi_infrastructure)

# satcfdi_os_macos: adaptador MacOSIntegration (T004, ADR 0009, DA4).
# Target separado y solo bajo APPLE para no contaminar satcfdi_infrastructure:
# esta sigue siendo portable y sin Qt6::Widgets, y sus pruebas no enlazan
# AppKit. Solo satcfdi_app debe enlazar este target (composition root en
# main); domain/ports/application/presentation/app_core no lo conocen.
# Objective-C++ (.mm) con ARC; frameworks AppKit, ServiceManagement y
# UserNotifications. Qt6::Widgets por QSystemTrayIcon/QMenu.
if(APPLE)
    enable_language(OBJCXX)

    qt_add_library(satcfdi_os_macos STATIC
        os/macos/MacOSMapeos.h
        os/macos/MacOSMapeos.cpp
        os/macos/MacOSIntegration.h
        os/macos/MacOSIntegration.mm
    )

    set_target_properties(satcfdi_os_macos PROPERTIES AUTOMOC ON)

    target_include_directories(satcfdi_os_macos PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_compile_options(satcfdi_os_macos PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )

    target_link_libraries(satcfdi_os_macos
        PUBLIC
            satcfdi_ports
            Qt6::Core
        PRIVATE
            Qt6::Gui
            Qt6::Widgets
            "-framework AppKit"
            "-framework ServiceManagement"
            "-framework UserNotifications"
    )

    satcfdi_enable_warnings(satcfdi_os_macos)

    # Pruebas de los mapeos puros (sin permisos ni bundle). Viven junto al
    # adaptador porque tests/** pertenece a otros cortes en T004.
    qt_add_executable(satcfdi_os_macos_tests
        os/macos/tests/TestMacOSMapeos.cpp
    )
    target_link_libraries(satcfdi_os_macos_tests PRIVATE
        satcfdi_os_macos
        Qt6::Test
    )
    add_test(NAME satcfdi_os_macos_tests COMMAND satcfdi_os_macos_tests)
    set_tests_properties(satcfdi_os_macos_tests PROPERTIES LABELS "unit;macos")
    satcfdi_enable_warnings(satcfdi_os_macos_tests)
endif()

# --- T005 corte B (especialista-macos) ---------------------------------------
# OpenSSL 3 como dependencia DIRECTA (DU1): no se confia en la carga
# transitiva de QtNetwork. En macOS se usa la de Homebrew si no se indico
# OPENSSL_ROOT_DIR. Su empaquetado en el bundle queda para distribucion.
if(APPLE AND NOT OPENSSL_ROOT_DIR)
    foreach(_satcfdi_openssl_dir /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3)
        if(EXISTS "${_satcfdi_openssl_dir}/include/openssl/evp.h")
            set(OPENSSL_ROOT_DIR "${_satcfdi_openssl_dir}" CACHE PATH "Raiz de OpenSSL 3")
            break()
        endif()
    endforeach()
endif()
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)

# satcfdi_crypto: validacion de e.firma (X.509 DER, PKCS#8 DER cifrado, RFC por
# OID 2.5.4.45, regla OU/CSD, vigencia) y contenedor AEAD v1 (DA6, DS1-DS4).
# Portable: sin Security.framework ni Qt Widgets. Los headers no exponen
# tipos de OpenSSL (enlace PRIVATE).
qt_add_library(satcfdi_crypto STATIC
    crypto/EFirmaOpenSsl.h
    crypto/EFirmaOpenSsl.cpp
    crypto/ContenedorCredencial.h
    crypto/ContenedorCredencial.cpp
)
target_include_directories(satcfdi_crypto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(satcfdi_crypto
    PUBLIC
        satcfdi_domain
        satcfdi_ports
        Qt6::Core
    PRIVATE
        OpenSSL::Crypto
)
satcfdi_enable_warnings(satcfdi_crypto)

# satcfdi_secrets_macos: MacOSSecretStore + KeychainApi (real y falsable),
# contenedores en disco (DA6, DS3, DS6). Solo Apple: Security.framework y
# CoreFoundation. Solo el composition root (app_core/app) debe enlazarlo.
if(APPLE)
    qt_add_library(satcfdi_secrets_macos STATIC
        secrets/macos/KeychainApi.h
        secrets/macos/KeychainErrores.cpp
        secrets/macos/KeychainApiMacOS.h
        secrets/macos/KeychainApiMacOS.cpp
        secrets/macos/DirectorioCredenciales.h
        secrets/macos/DirectorioCredenciales.cpp
        secrets/macos/MacOSSecretStore.h
        secrets/macos/MacOSSecretStore.cpp
    )
    target_include_directories(satcfdi_secrets_macos PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    target_link_libraries(satcfdi_secrets_macos
        PUBLIC
            satcfdi_ports
            Qt6::Core
        PRIVATE
            satcfdi_crypto
            "-framework Security"
            "-framework CoreFoundation"
    )
    satcfdi_enable_warnings(satcfdi_secrets_macos)
endif()

# --- T006 corte A (core): biblioteca SAT del spike -------------------------
# satcfdi_sat: C14N (libxml2 del SDK), digest/firma RSA-SHA1 (OpenSSL 3),
# sobres SOAP/WS-Security, parser de respuestas y Faults (QXmlStreamReader),
# cliente HTTP con fases (QNetworkAccessManager) y enmascarado de evidencia
# (D2, D3, D5, D6). Reutilizable por T009; NO lo enlaza satcfdi_app (solo la
# CLI del spike, tools/sat_spike, y sus pruebas). Headers sin tipos de libxml2
# ni OpenSSL.
find_package(LibXml2 REQUIRED)

qt_add_library(satcfdi_sat STATIC
    sat/SatOperaciones.h
    sat/SatOperaciones.cpp
    sat/XmlC14n.h
    sat/XmlC14n.cpp
    sat/FirmaXml.h
    sat/FirmaXml.cpp
    sat/SobresSat.h
    sat/SobresSat.cpp
    sat/TokenSat.h
    sat/RespuestasSat.h
    sat/RespuestasSat.cpp
    sat/ClienteHttpSat.h
    sat/ClienteHttpSat.cpp
    sat/EnmascaradorEvidencia.h
    sat/EnmascaradorEvidencia.cpp
)
set_target_properties(satcfdi_sat PROPERTIES AUTOMOC ON)
target_include_directories(satcfdi_sat PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(satcfdi_sat
    PUBLIC
        satcfdi_domain
        satcfdi_ports
        satcfdi_crypto
        Qt6::Core
        Qt6::Network
    PRIVATE
        LibXml2::LibXml2
        OpenSSL::Crypto
)
satcfdi_enable_warnings(satcfdi_sat)

# Las pruebas sin red del spike (tests/sat) las registra el CMake raiz.
