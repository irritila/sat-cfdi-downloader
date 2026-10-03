# satcfdi_app: ejecutable MACOSX_BUNDLE con main.cpp y la fabrica del adaptador
# de SO. Arranque, composition root, ciclo de vida e instancia unica viven en
# satcfdi_app_core. Qt6::Sql y Qt6::Network llegan de forma transitiva desde
# las bibliotecas estaticas (infraestructura y app_core).
qt_add_executable(satcfdi_app MACOSX_BUNDLE
    main.cpp
    OSIntegrationFactory.h
    OSIntegrationFactory.cpp
)

set_target_properties(satcfdi_app PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${SATCFDI_MACOS_INFO_PLIST}"
    MACOSX_BUNDLE_BUNDLE_NAME "${SATCFDI_BUNDLE_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${SATCFDI_BUNDLE_IDENTIFIER}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
)

target_link_libraries(satcfdi_app PRIVATE
    satcfdi_app_core
    satcfdi_presentationplugin
    Qt6::Widgets
    Qt6::Quick
)

# Adaptador de SO (MacOSIntegration): solo satcfdi_app lo enlaza.
if(APPLE)
    target_link_libraries(satcfdi_app PRIVATE satcfdi_os_macos)
endif()

satcfdi_enable_warnings(satcfdi_app)
