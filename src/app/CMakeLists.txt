# satcfdi_app: ejecutable MACOSX_BUNDLE con main.cpp y el composition root de
# T002. Sin satcfdi_app_core (entra con T003). Sin Sql ni Network.
qt_add_executable(satcfdi_app MACOSX_BUNDLE
    main.cpp
    AppCompositionRoot.h
    AppCompositionRoot.cpp
)

set_target_properties(satcfdi_app PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${SATCFDI_MACOS_INFO_PLIST}"
    MACOSX_BUNDLE_BUNDLE_NAME "${SATCFDI_BUNDLE_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${SATCFDI_BUNDLE_IDENTIFIER}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
)

target_link_libraries(satcfdi_app PRIVATE
    satcfdi_presentation
    satcfdi_presentationplugin
    satcfdi_application
    satcfdi_infrastructure
    Qt6::Widgets
    Qt6::Quick
)

satcfdi_enable_warnings(satcfdi_app)
