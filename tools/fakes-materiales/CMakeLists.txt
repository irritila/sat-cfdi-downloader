# tools/fakes-materiales (T024.2, T024 D9): fakes sinteticos comunes de
# tools/capturas-manual y tools/video-tutorial. Implementan los puertos
# vigentes de presentacion (FuenteFacturasMes, PreferenciasFacturas,
# FuenteHistorial, puertos de respaldo) y construyen el
# ExportacionFacturasService real sobre puertos en memoria. Datos declarados,
# sin hilos propios, temporizadores ni reloj real.
#
# Solo enlaza capas que el video ya admite (T017 D10): sin app_core, SAT,
# Keychain, adaptador macOS, SQLite ni Qt Network.
qt_add_library(satcfdi_fakes_materiales STATIC
    DatosSinteticos.h
    DatosSinteticos.cpp
    FuentesSinteticas.h
    FuentesSinteticas.cpp
    RespaldoSintetico.h
    RespaldoSintetico.cpp
    ExportacionSintetica.h
    ExportacionSintetica.cpp
)
target_include_directories(satcfdi_fakes_materiales PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(satcfdi_fakes_materiales PUBLIC
    satcfdi_presentation
    satcfdi_application
    satcfdi_domain
    satcfdi_ports
    Qt6::Core
)
satcfdi_enable_warnings(satcfdi_fakes_materiales)
