# satcfdi_sat_spike_tests (T006 corte A): pruebas SIN red ni e.firma real de
# satcfdi_sat. Un solo ejecutable registrado dos veces en ctest:
# - sat-spike;unit: C14N/firma, sobres (goldens), parser, enmascarado;
# - sat-spike;infrastructure: cliente HTTP contra QTcpServer local.
# Ambas corren en la suite por defecto. La CLI real (tools/sat_spike) NO se
# registra aqui.
#
# add_executable (no qt_add_executable): el finalizador diferido de Qt recorre
# dependencias desde la raiz y emitia advertencias sobre OpenSSL::Crypto (solo
# visible en src/infrastructure) para otros targets. No hay plugins ni QML.
set(_satcfdi_sat_tests_dir "${CMAKE_CURRENT_LIST_DIR}")
add_executable(satcfdi_sat_spike_tests
    ${_satcfdi_sat_tests_dir}/main.cpp
    ${_satcfdi_sat_tests_dir}/SatPruebasComun.h
    ${_satcfdi_sat_tests_dir}/TestC14nFirma.h
    ${_satcfdi_sat_tests_dir}/TestC14nFirma.cpp
    ${_satcfdi_sat_tests_dir}/TestSobresRespuestas.h
    ${_satcfdi_sat_tests_dir}/TestSobresRespuestas.cpp
    ${_satcfdi_sat_tests_dir}/TestClienteHttpSat.h
    ${_satcfdi_sat_tests_dir}/TestClienteHttpSat.cpp
)
set_target_properties(satcfdi_sat_spike_tests PROPERTIES AUTOMOC ON)
target_include_directories(satcfdi_sat_spike_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(satcfdi_sat_spike_tests PRIVATE
    SATCFDI_SAT_FIXTURES="${CMAKE_CURRENT_LIST_DIR}/fixtures"
    SATCFDI_SAT_GOLDENS="${CMAKE_CURRENT_LIST_DIR}/goldens"
)
target_link_libraries(satcfdi_sat_spike_tests PRIVATE
    satcfdi_sat
    OpenSSL::Crypto
    Qt6::Network
    Qt6::Test
)
satcfdi_enable_warnings(satcfdi_sat_spike_tests)

add_test(NAME satcfdi_sat_spike_tests COMMAND satcfdi_sat_spike_tests --suite unit)
set_tests_properties(satcfdi_sat_spike_tests PROPERTIES LABELS "sat-spike;unit")
add_test(NAME satcfdi_sat_spike_http_tests COMMAND satcfdi_sat_spike_tests --suite infrastructure)
set_tests_properties(satcfdi_sat_spike_http_tests PROPERTIES LABELS "sat-spike;infrastructure")
