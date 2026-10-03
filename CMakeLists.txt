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

# Componentes Qt de T002. Prohibidos en T002: Sql (entra con T003) y Network
# (entra con las tareas SAT).
find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Qml
    Quick
    QuickControls2
    Test
    QuickTest
)

qt_standard_project_setup(REQUIRES 6.8)

# Metadatos del bundle macOS consumidos por src/app (MACOSX_BUNDLE_INFO_PLIST).
set(SATCFDI_BUNDLE_NAME "SAT CFDI Downloader")
set(SATCFDI_BUNDLE_IDENTIFIER "mx.adenium.satcfdi" CACHE STRING
    "CFBundleIdentifier de satcfdi_app")
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
add_subdirectory(src/app)

# Pruebas.
add_subdirectory(tests/unit)
add_subdirectory(tests/presentation)
