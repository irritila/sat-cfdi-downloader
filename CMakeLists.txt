cmake_minimum_required(VERSION 3.21)

project(SatCfdiDownloader
    VERSION 0.1.0
    DESCRIPTION "SAT CFDI Downloader: app desktop local-first para descarga masiva de CFDI"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(warnings)

# Componentes Qt. Sql (T003) solo lo enlaza satcfdi_infrastructure (y sus
# pruebas). Network (T004) solo lo enlaza satcfdi_app_core para la instancia
# unica (QLocalServer/QLocalSocket); el cliente HTTP SAT llega con su tarea.
find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Qml
    Quick
    QuickControls2
    Sql
    Network
    Test
    QuickTest
)

qt_standard_project_setup(REQUIRES 6.8)

# Metadatos del bundle macOS consumidos por src/app (MACOSX_BUNDLE_INFO_PLIST).
set(SATCFDI_BUNDLE_NAME "SAT CFDI Downloader")
set(SATCFDI_BUNDLE_IDENTIFIER "mx.adenium.satcfdi-downloader" CACHE STRING
    "CFBundleIdentifier de satcfdi_app")
# Migracion T004: un build dir configurado antes conserva en cache el id
# anterior (un default de CACHE no lo reemplaza). Solo se migra ese valor
# exacto; un id personalizado distinto se respeta.
if(SATCFDI_BUNDLE_IDENTIFIER STREQUAL "mx.adenium.satcfdi")
    message(STATUS "SATCFDI_BUNDLE_IDENTIFIER: migrando el valor en cache "
                   "'mx.adenium.satcfdi' a 'mx.adenium.satcfdi-downloader' (T004)")
    set(SATCFDI_BUNDLE_IDENTIFIER "mx.adenium.satcfdi-downloader" CACHE STRING
        "CFBundleIdentifier de satcfdi_app" FORCE)
endif()
# Firma opcional del bundle (T005 corte F). Vacias por defecto: el build queda
# con la firma ad-hoc del linker y nada cambia. Con las tres definidas,
# satcfdi_app embebe el perfil y se firma con entitlements (data protection
# keychain). Los valores se pasan al configurar; nunca se versionan.
set(SATCFDI_CODESIGN_IDENTITY "" CACHE STRING
    "Identidad de codesign (preferir el hash SHA-1 si hay certificados con el mismo nombre)")
set(SATCFDI_PROVISIONING_PROFILE "" CACHE FILEPATH
    "Perfil de aprovisionamiento (.provisionprofile) a embeber en el bundle")
set(SATCFDI_TEAM_ID "" CACHE STRING "Team ID de Apple para los entitlements")
set(SATCFDI_MACOS_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Info.plist.in")

# LSMinimumSystemVersion. Sin CMAKE_OSX_DEPLOYMENT_TARGET explicito, clang toma
# como minimo la version mayor del SDK (p. ej. SDK 26.2 -> minos 26.0); se refleja
# lo mismo en el Info.plist. No se fija un minimo por defecto porque Qt de Homebrew se compila para la version de macOS
# del equipo y un minimo menor produce advertencias de ld.
if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(SATCFDI_MACOS_MINIMUM_VERSION "${CMAKE_OSX_DEPLOYMENT_TARGET}")
else()
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-version
        OUTPUT_VARIABLE SATCFDI_MACOS_MINIMUM_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    string(REGEX MATCH "^[0-9]+" _satcfdi_sdk_major "${SATCFDI_MACOS_MINIMUM_VERSION}")
    set(SATCFDI_MACOS_MINIMUM_VERSION "${_satcfdi_sdk_major}.0")
endif()

enable_testing()

# T005 (DA8): pruebas contra el Keychain real del sistema. Requieren bundle
# firmado con entitlements; sin ellos hacen QSKIP. Las define tests/secrets.
option(SATCFDI_KEYCHAIN_REAL_TESTS "Compilar y registrar pruebas contra Keychain real (macOS)" OFF)

# OpenSSL 3 (T005/T006) se busca en la raiz para que su target importado
# OpenSSL::Crypto sea visible tambien en tests/sat (fuera de src/infrastructure).
# En macOS se usa la de Homebrew si no se indico OPENSSL_ROOT_DIR.
if(APPLE AND NOT OPENSSL_ROOT_DIR)
    foreach(_satcfdi_openssl_dir /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3)
        if(EXISTS "${_satcfdi_openssl_dir}/include/openssl/evp.h")
            set(OPENSSL_ROOT_DIR "${_satcfdi_openssl_dir}" CACHE PATH "Raiz de OpenSSL 3")
            break()
        endif()
    endforeach()
endif()
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)

# Capas en orden de dependencia.
add_subdirectory(src/domain)
add_subdirectory(src/ports)
add_subdirectory(src/application)
add_subdirectory(src/infrastructure)
add_subdirectory(src/presentation)
add_subdirectory(src/app_core)
add_subdirectory(src/app)

# Pruebas por label: unit, infrastructure, integration, presentation.
add_subdirectory(tests/unit)
add_subdirectory(tests/infrastructure)
# T005: crypto (portable) y SecretStore macOS con KeychainApi falsa; labels
# secrets;infrastructure. Keychain real solo con SATCFDI_KEYCHAIN_REAL_TESTS.
add_subdirectory(tests/secrets)
# T006: pruebas sin red de la biblioteca SAT (labels sat-spike;unit|infrastructure).
add_subdirectory(tests/sat)
add_subdirectory(tests/integration)
add_subdirectory(tests/presentation)

# T006 (D3): CLI manual del spike SAT. Apagada por defecto; fuera del bundle.
# tests/sat (sin red) se registra desde src/infrastructure y corre siempre.
option(SATCFDI_BUILD_SAT_SPIKE "Compilar la CLI manual del spike SAT (tools/sat_spike)" OFF)
if(SATCFDI_BUILD_SAT_SPIKE)
    add_subdirectory(tools/sat_spike)
endif()

# Criterio 8 de T006: satcfdi_app no enlaza la biblioteca SAT del spike (ni
# directa ni transitivamente). Se comprueba al configurar.
function(_satcfdi_dependencias_link target salida)
    set(_pendientes ${target})
    set(_vistos "")
    while(_pendientes)
        list(POP_FRONT _pendientes _actual)
        if(_actual IN_LIST _vistos OR NOT TARGET ${_actual})
            continue()
        endif()
        list(APPEND _vistos ${_actual})
        get_target_property(_alias ${_actual} ALIASED_TARGET)
        if(_alias)
            list(APPEND _pendientes ${_alias})
        endif()
        foreach(_propiedad LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_libs ${_actual} ${_propiedad})
            if(_libs)
                foreach(_lib IN LISTS _libs)
                    string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" _lib "${_lib}")
                    if(NOT _lib MATCHES "^\\$<")
                        list(APPEND _pendientes ${_lib})
                    endif()
                endforeach()
            endif()
        endforeach()
    endwhile()
    set(${salida} ${_vistos} PARENT_SCOPE)
endfunction()
_satcfdi_dependencias_link(satcfdi_app _satcfdi_deps_app)
if("satcfdi_sat" IN_LIST _satcfdi_deps_app)
    message(FATAL_ERROR "satcfdi_app enlaza satcfdi_sat (biblioteca del spike SAT); T006 lo prohibe hasta T009")
endif()
