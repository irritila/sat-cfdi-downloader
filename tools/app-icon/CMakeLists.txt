# tools/app-icon (T020 D5, label branding). Sin Xcode: el build normal no
# compila el icono; usa resources/macos/AppIcon-compilado/ (versionado).
# - satcfdi_app_icon_manifiesto: las fuentes (AppIcon.icon y capas de
#   resources/branding/icono) y las salidas coinciden con el manifiesto.
# - satcfdi_app_icon_verificador: casos del verificador sobre copias.
# Regenerar (requiere Xcode): tools/app-icon/regenerar_app_icon [--check].
add_test(NAME satcfdi_app_icon_manifiesto
    COMMAND ${CMAKE_COMMAND} -DRAIZ=${PROJECT_SOURCE_DIR}
            -DMINIMO=${SATCFDI_MACOS_MINIMUM_VERSION}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/verificar-manifiesto.cmake)
add_test(NAME satcfdi_app_icon_verificador
    COMMAND ${CMAKE_COMMAND}
            -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/verificar-manifiesto.cmake
            -DORIGEN=${PROJECT_SOURCE_DIR}
            -DDIR=${CMAKE_CURRENT_BINARY_DIR}/prueba
            -P ${CMAKE_CURRENT_SOURCE_DIR}/prueba-verificar-manifiesto.cmake)
set_tests_properties(satcfdi_app_icon_manifiesto satcfdi_app_icon_verificador
    PROPERTIES LABELS branding)
