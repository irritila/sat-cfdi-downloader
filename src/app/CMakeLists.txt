# satcfdi_app: ejecutable MACOSX_BUNDLE con main.cpp. El arranque y el
# composition root viven en satcfdi_app_core (T003). Sin Network; Qt6::Sql
# solo llega de forma transitiva desde satcfdi_infrastructure (estatica).
qt_add_executable(satcfdi_app MACOSX_BUNDLE
    main.cpp
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

satcfdi_enable_warnings(satcfdi_app)
