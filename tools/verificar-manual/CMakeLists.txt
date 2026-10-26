# T024.3: verificacion del manual de usuario (label docs;manual). Sin render ni
# Qt: corre en el build principal, sin SATCFDI_BUILD_MANUAL_CAPTURAS.
# - satcfdi_manual_verificador: casos temporales, uno valido y uno negativo
#   por regla (vocabulario, imagen, huerfana, manifiesto).
# - satcfdi_manual_verificar: docs/manual/usuario.md, docs/manual/img y el
#   manifiesto de capturas (tools/capturas-manual/escenarios.json).
set(_verificar_manual_args
    -DMANUAL=${PROJECT_SOURCE_DIR}/docs/manual/usuario.md
    -DIMAGENES=${PROJECT_SOURCE_DIR}/docs/manual/img
    -DMANIFIESTO=${PROJECT_SOURCE_DIR}/tools/capturas-manual/escenarios.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/verificar-manual.cmake)
add_test(NAME satcfdi_manual_verificador
    COMMAND ${CMAKE_COMMAND}
            -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/verificar-manual.cmake
            -DDIR=${CMAKE_CURRENT_BINARY_DIR}/prueba
            -P ${CMAKE_CURRENT_SOURCE_DIR}/prueba-verificar-manual.cmake)
add_test(NAME satcfdi_manual_verificar COMMAND ${CMAKE_COMMAND} ${_verificar_manual_args})
set_tests_properties(satcfdi_manual_verificador satcfdi_manual_verificar PROPERTIES LABELS "docs;manual")
add_custom_target(verificar_manual
    COMMAND ${CMAKE_COMMAND} ${_verificar_manual_args}
    USES_TERMINAL
    COMMENT "T024.3: vocabulario, imagenes y manifiesto del manual de usuario")
