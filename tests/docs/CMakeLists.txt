# T012 D8: verificacion de enlaces de imagenes del manual (label docs). Sin
# render: solo lee docs/manual/usuario.md y lista docs/manual/img/.
add_test(NAME satcfdi_manual_enlaces
    COMMAND ${CMAKE_COMMAND}
        -DMANUAL=${PROJECT_SOURCE_DIR}/docs/manual/usuario.md
        -DIMAGENES=${PROJECT_SOURCE_DIR}/docs/manual/img
        -P ${CMAKE_CURRENT_SOURCE_DIR}/VerificarEnlacesManual.cmake
)
set_tests_properties(satcfdi_manual_enlaces PROPERTIES LABELS docs)
