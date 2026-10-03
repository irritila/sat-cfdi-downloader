# Pruebas de T005 corte B: crypto (portable), MacOSSecretStore con
# FakeKeychainApi (Apple) y Keychain real (opcional). Plataforma las incluye
# desde el CMake raiz con add_subdirectory(tests/secrets).
#
# Fixtures: se generan en cada ejecucion con fixtures/generar_fixtures.sh y el
# openssl CLI (DA7); no hay fixtures binarios versionados.

find_program(SATCFDI_OPENSSL_EXECUTABLE openssl
    HINTS "${OPENSSL_ROOT_DIR}/bin"
    NO_DEFAULT_PATH
)
if(NOT SATCFDI_OPENSSL_EXECUTABLE)
    find_program(SATCFDI_OPENSSL_EXECUTABLE openssl REQUIRED)
endif()

set(_satcfdi_fixtures_defs
    SATCFDI_OPENSSL_EXECUTABLE="${SATCFDI_OPENSSL_EXECUTABLE}"
    SATCFDI_SCRIPT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/generar_fixtures.sh"
)

qt_add_executable(satcfdi_crypto_tests
    TestCrypto.cpp
    FixturesEFirma.h
)
set_target_properties(satcfdi_crypto_tests PROPERTIES AUTOMOC ON)
target_compile_definitions(satcfdi_crypto_tests PRIVATE ${_satcfdi_fixtures_defs})
target_link_libraries(satcfdi_crypto_tests PRIVATE satcfdi_crypto Qt6::Test)
add_test(NAME satcfdi_crypto_tests COMMAND satcfdi_crypto_tests)
set_tests_properties(satcfdi_crypto_tests PROPERTIES LABELS "secrets;infrastructure")
satcfdi_enable_warnings(satcfdi_crypto_tests)

if(APPLE)
    qt_add_executable(satcfdi_secrets_macos_tests
        TestMacOSSecretStore.cpp
        FixturesEFirma.h
        FakeKeychainApi.h
    )
    set_target_properties(satcfdi_secrets_macos_tests PROPERTIES AUTOMOC ON)
    target_compile_definitions(satcfdi_secrets_macos_tests PRIVATE ${_satcfdi_fixtures_defs})
    target_link_libraries(satcfdi_secrets_macos_tests PRIVATE satcfdi_secrets_macos Qt6::Test)
    add_test(NAME satcfdi_secrets_macos_tests COMMAND satcfdi_secrets_macos_tests)
    set_tests_properties(satcfdi_secrets_macos_tests PROPERTIES LABELS "secrets;infrastructure")
    satcfdi_enable_warnings(satcfdi_secrets_macos_tests)

    # Keychain real (DA8): apagado por defecto. Requiere sesion grafica y, para
    # el data protection keychain, ejecutable firmado con entitlements; si no,
    # la prueba hace QSKIP.
    option(SATCFDI_KEYCHAIN_REAL_TESTS "Compilar y registrar pruebas contra Keychain real" OFF)
    if(SATCFDI_KEYCHAIN_REAL_TESTS)
        qt_add_executable(satcfdi_keychain_real_tests
            TestKeychainReal.cpp
            FixturesEFirma.h
        )
        set_target_properties(satcfdi_keychain_real_tests PROPERTIES AUTOMOC ON)
        target_compile_definitions(satcfdi_keychain_real_tests PRIVATE ${_satcfdi_fixtures_defs})
        target_link_libraries(satcfdi_keychain_real_tests PRIVATE satcfdi_secrets_macos Qt6::Test)
        satcfdi_enable_warnings(satcfdi_keychain_real_tests)

        # Firma (T005 corte G): con las tres opciones de firma de la raiz, la
        # prueba se construye como bundle con el MISMO bundle id que la app (el
        # perfil esta atado a ese App ID), embebe el perfil y se firma con los
        # mismos entitlements y hardened runtime. Sus items usan un servicio
        # aislado y aleatorio (<bundle>.test-<aleatorio>.*) y se borran en
        # cleanupTestCase, asi que no tocan los de la app. Sin las opciones, la
        # prueba queda sin firmar y hace QSKIP (errSecMissingEntitlement).
        if(NOT SATCFDI_CODESIGN_IDENTITY STREQUAL ""
           AND NOT SATCFDI_PROVISIONING_PROFILE STREQUAL ""
           AND NOT SATCFDI_TEAM_ID STREQUAL "")
            set_target_properties(satcfdi_keychain_real_tests PROPERTIES
                MACOSX_BUNDLE TRUE
                MACOSX_BUNDLE_INFO_PLIST "${SATCFDI_MACOS_INFO_PLIST}"
                MACOSX_BUNDLE_BUNDLE_NAME "satcfdi_keychain_real_tests"
                MACOSX_BUNDLE_GUI_IDENTIFIER "${SATCFDI_BUNDLE_IDENTIFIER}"
                MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            )
            find_program(SATCFDI_CODESIGN codesign REQUIRED)
            # Mismos entitlements que satcfdi_app (ver src/app/CMakeLists.txt).
            set(_satcfdi_entitlements_prueba "${CMAKE_CURRENT_BINARY_DIR}/satcfdi_keychain_real_tests.entitlements")
            configure_file("${PROJECT_SOURCE_DIR}/resources/macos/satcfdi.entitlements.in"
                           "${_satcfdi_entitlements_prueba}" @ONLY)
            add_custom_command(TARGET satcfdi_keychain_real_tests POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy "${SATCFDI_PROVISIONING_PROFILE}"
                        "$<TARGET_BUNDLE_CONTENT_DIR:satcfdi_keychain_real_tests>/embedded.provisionprofile"
                COMMAND "${SATCFDI_CODESIGN}" --force --sign "${SATCFDI_CODESIGN_IDENTITY}"
                        --entitlements "${_satcfdi_entitlements_prueba}" --options runtime --timestamp=none
                        "$<TARGET_BUNDLE_DIR:satcfdi_keychain_real_tests>"
                COMMAND "${SATCFDI_CODESIGN}" --verify --strict --verbose=1
                        "$<TARGET_BUNDLE_DIR:satcfdi_keychain_real_tests>"
                COMMENT "Firmando satcfdi_keychain_real_tests.app con entitlements"
                VERBATIM
            )
            set_property(TARGET satcfdi_keychain_real_tests APPEND PROPERTY LINK_DEPENDS
                         "${_satcfdi_entitlements_prueba}" "${SATCFDI_PROVISIONING_PROFILE}")
        endif()

        # TARGET_FILE es el binario dentro del bundle cuando se firma.
        add_test(NAME satcfdi_keychain_real_tests COMMAND $<TARGET_FILE:satcfdi_keychain_real_tests>)
        set_tests_properties(satcfdi_keychain_real_tests PROPERTIES
            LABELS "secrets;keychain-real"
            TIMEOUT 120
        )
    endif()
endif()
