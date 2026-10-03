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
add_subdirectory(tests/integration)
add_subdirectory(tests/presentation)
