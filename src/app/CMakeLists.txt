# satcfdi_app: ejecutable MACOSX_BUNDLE con main.cpp y la fabrica del adaptador
# de SO. Arranque, composition root, ciclo de vida e instancia unica viven en
# satcfdi_app_core. Qt6::Sql y Qt6::Network llegan de forma transitiva desde
# las bibliotecas estaticas (infraestructura y app_core).
qt_add_executable(satcfdi_app MACOSX_BUNDLE
    main.cpp
    OSIntegrationFactory.h
    OSIntegrationFactory.cpp
    SecretStoreFactory.h
    SecretStoreFactory.cpp
)

# Bundle id efectivo para los servicios Keychain del SecretStore.
target_compile_definitions(satcfdi_app PRIVATE
    SATCFDI_BUNDLE_ID="${SATCFDI_BUNDLE_IDENTIFIER}"
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

# Adaptadores macOS (MacOSIntegration, MacOSSecretStore): solo satcfdi_app los
# enlaza; app_core recibe los puertos por inyeccion.
if(APPLE)
    target_link_libraries(satcfdi_app PRIVATE satcfdi_os_macos satcfdi_secrets_macos)
endif()

satcfdi_enable_warnings(satcfdi_app)

# Firma opcional con entitlements (T005 corte F). Solo si las tres opciones de
# la raiz estan definidas; si no, queda la firma ad-hoc del linker.
set(_satcfdi_opciones_firma SATCFDI_CODESIGN_IDENTITY SATCFDI_PROVISIONING_PROFILE SATCFDI_TEAM_ID)
set(_satcfdi_firma_definidas 0)
foreach(_opcion IN LISTS _satcfdi_opciones_firma)
    if(NOT "${${_opcion}}" STREQUAL "")
        math(EXPR _satcfdi_firma_definidas "${_satcfdi_firma_definidas} + 1")
    endif()
endforeach()

if(_satcfdi_firma_definidas GREATER 0 AND NOT _satcfdi_firma_definidas EQUAL 3)
    message(FATAL_ERROR "Firma del bundle: definir las tres opciones SATCFDI_CODESIGN_IDENTITY, "
                        "SATCFDI_PROVISIONING_PROFILE y SATCFDI_TEAM_ID, o ninguna.")
endif()

if(APPLE AND _satcfdi_firma_definidas EQUAL 3)
    if(NOT EXISTS "${SATCFDI_PROVISIONING_PROFILE}")
        message(FATAL_ERROR "SATCFDI_PROVISIONING_PROFILE no existe: ${SATCFDI_PROVISIONING_PROFILE}")
    endif()
    find_program(SATCFDI_CODESIGN codesign REQUIRED)

    # Las excepciones del hardened runtime (allow-jit, disable-library-validation)
    # estan en la plantilla, justificadas alli y en docs/development.md.
    set(_satcfdi_entitlements "${CMAKE_CURRENT_BINARY_DIR}/satcfdi_app.entitlements")
    configure_file("${PROJECT_SOURCE_DIR}/resources/macos/satcfdi.entitlements.in"
                   "${_satcfdi_entitlements}" @ONLY)

    # POST_BUILD: despues del linker (y de su firma ad-hoc). Ningun otro paso
    # modifica el bundle despues; si se agrega uno (p. ej. macdeployqt), debe ir
    # antes de esta firma.
    add_custom_command(TARGET satcfdi_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy "${SATCFDI_PROVISIONING_PROFILE}"
                "$<TARGET_BUNDLE_CONTENT_DIR:satcfdi_app>/embedded.provisionprofile"
        COMMAND "${SATCFDI_CODESIGN}" --force --sign "${SATCFDI_CODESIGN_IDENTITY}"
                --entitlements "${_satcfdi_entitlements}" --options runtime --timestamp=none
                "$<TARGET_BUNDLE_DIR:satcfdi_app>"
        COMMAND "${SATCFDI_CODESIGN}" --verify --strict --verbose=1 "$<TARGET_BUNDLE_DIR:satcfdi_app>"
        # Falla el build si a los entitlements EFECTIVOS les falta una
        # excepcion requerida por el hardened runtime (regresion T005.1: sin
        # allow-jit, el FileDialog mataba la app al compilar PCRE2 con JIT).
        COMMAND /bin/sh -c "for k in com.apple.security.cs.allow-jit com.apple.security.cs.disable-library-validation keychain-access-groups; do \"$0\" -d --entitlements - --xml \"$1\" 2>/dev/null | grep -q \"<key>$k</key>\" || { echo \"error: falta el entitlement $k en $1\" >&2; exit 1; }; done"
                "${SATCFDI_CODESIGN}" "$<TARGET_BUNDLE_DIR:satcfdi_app>"
        COMMENT "Firmando satcfdi_app.app con entitlements"
        VERBATIM
    )
    set_property(TARGET satcfdi_app APPEND PROPERTY LINK_DEPENDS
                 "${_satcfdi_entitlements}" "${SATCFDI_PROVISIONING_PROFILE}")
    message(STATUS "satcfdi_app: firma con entitlements habilitada (team ${SATCFDI_TEAM_ID})")
endif()
