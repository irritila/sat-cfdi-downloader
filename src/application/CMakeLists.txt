# satcfdi_application: contratos funcionales consumidos por presentacion,
# Resultado<T,E>, DTOs y servicios demo de T002. Sin hilos, timers, SQL, red ni
# archivos. Los servicios Demo* solo se instancian en el composition root.
qt_add_library(satcfdi_application STATIC
    common/Resultado.h
    common/Errores.h
    profiles/PerfilResumen.h
    profiles/PerfilesSatService.h
    profiles/DemoPerfilesSatService.h
    profiles/DemoPerfilesSatService.cpp
    requests/SolicitudDtos.h
    requests/SolicitudesService.h
    requests/DemoSolicitudesService.h
    requests/DemoSolicitudesService.cpp
)

# Includes relativos a src/, por ejemplo
# #include "application/requests/SolicitudesService.h".
target_include_directories(satcfdi_application PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(satcfdi_application PUBLIC
    satcfdi_domain
    satcfdi_ports
    Qt6::Core
)

satcfdi_enable_warnings(satcfdi_application)
