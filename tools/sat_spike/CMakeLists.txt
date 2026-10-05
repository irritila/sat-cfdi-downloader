# tools/sat_spike: CLI manual del spike SAT (T006, D1, D3-D6). Solo con
# -DSATCFDI_BUILD_SAT_SPIKE=ON. Fuera del bundle; satcfdi_app no la enlaza.
# add_executable (no qt_add_executable): ver la nota de tests/sat sobre el
# finalizador de Qt y OpenSSL::Crypto.

add_library(satcfdi_sat_spike_core STATIC
    SpikeCli.h
    SpikeCli.cpp
)
set_target_properties(satcfdi_sat_spike_core PROPERTIES AUTOMOC ON)
target_include_directories(satcfdi_sat_spike_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(satcfdi_sat_spike_core PUBLIC satcfdi_sat satcfdi_crypto Qt6::Core Qt6::Network)
satcfdi_enable_warnings(satcfdi_sat_spike_core)

add_executable(satcfdi_sat_spike main.cpp)
set_target_properties(satcfdi_sat_spike PROPERTIES AUTOMOC ON)
# Raiz del repo: la CLI rechaza .cer/.key y directorio de salida dentro de ella.
target_compile_definitions(satcfdi_sat_spike PRIVATE SATCFDI_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(satcfdi_sat_spike PRIVATE satcfdi_sat_spike_core)
satcfdi_enable_warnings(satcfdi_sat_spike)

# Binario SOLO para pruebas: misma main.cpp con una definicion que permite
# leer la contrasena de stdin sin TTY. No se instala ni se documenta para uso.
add_executable(satcfdi_sat_spike_prueba main.cpp)
set_target_properties(satcfdi_sat_spike_prueba PROPERTIES AUTOMOC ON)
target_compile_definitions(satcfdi_sat_spike_prueba PRIVATE
    SATCFDI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    SATCFDI_SPIKE_CONTRASENA_STDIN_PRUEBA
)
target_link_libraries(satcfdi_sat_spike_prueba PRIVATE satcfdi_sat_spike_core)
satcfdi_enable_warnings(satcfdi_sat_spike_prueba)

# Pruebas de la CLI (criterio 7): fixtures sinteticos de tests/secrets, sin
# red. La CLI contra el SAT real NO se registra en ctest.
add_executable(satcfdi_sat_spike_cli_tests
    tests/TestSpikeCli.cpp
    ${PROJECT_SOURCE_DIR}/tests/secrets/FixturesEFirma.h
)
set_target_properties(satcfdi_sat_spike_cli_tests PROPERTIES AUTOMOC ON)
target_include_directories(satcfdi_sat_spike_cli_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/secrets)
target_compile_definitions(satcfdi_sat_spike_cli_tests PRIVATE
    SATCFDI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    SATCFDI_SAT_SPIKE_BIN="$<TARGET_FILE:satcfdi_sat_spike>"
    SATCFDI_SAT_SPIKE_PRUEBA_BIN="$<TARGET_FILE:satcfdi_sat_spike_prueba>"
    SATCFDI_OPENSSL_EXECUTABLE="${SATCFDI_OPENSSL_EXECUTABLE}"
    SATCFDI_SCRIPT_FIXTURES="${PROJECT_SOURCE_DIR}/tests/secrets/fixtures/generar_fixtures.sh"
)
target_link_libraries(satcfdi_sat_spike_cli_tests PRIVATE satcfdi_sat_spike_core Qt6::Test)
add_dependencies(satcfdi_sat_spike_cli_tests satcfdi_sat_spike satcfdi_sat_spike_prueba)
satcfdi_enable_warnings(satcfdi_sat_spike_cli_tests)
add_test(NAME satcfdi_sat_spike_cli_tests COMMAND satcfdi_sat_spike_cli_tests)
# Temporales (fixtures con .cer/.key SINTETICOS, directorios de salida) en
# /tmp en lugar del TMPDIR por usuario: asi no los ve el escaneo de temporales
# de satcfdi_credenciales_centinelas_tests aunque otras suites corran en
# paralelo. No puede ser un directorio del build: la CLI rechaza .cer/.key
# dentro del repositorio. QTemporaryDir los crea con 0700 y los borra al
# terminar (autoRemove); cleanupTestCase verifica que no queden restos.
set_tests_properties(satcfdi_sat_spike_cli_tests PROPERTIES
    LABELS "sat-spike;unit"
    ENVIRONMENT "TMPDIR=/tmp/"
)
