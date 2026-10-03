# satcfdi_application: contratos funcionales consumidos por presentacion,
# DTOs, servicios (demo y persistidos), PersistenceDispatcher y
# RegexLogSanitizer. Sin SQL, red, archivos ni timers. Unico hilo permitido:
# el QThread de PersistenceDispatcher (DA4). Los servicios Demo* y *Persistido
# solo se instancian en el composition root (o en pruebas).
qt_add_library(satcfdi_application STATIC
    common/Errores.h
    logging/RegexLogSanitizer.h
    logging/RegexLogSanitizer.cpp
    persistence/PersistenceDispatcher.h
    persistence/PersistenceDispatcher.cpp
    persistence/PuertosPersistencia.h
    profiles/NuevoPerfilSimuladoRequest.h
    profiles/PerfilResumen.h
    profiles/PerfilesSatService.h
    profiles/PerfilesSatServicePersistido.h
    profiles/PerfilesSatServicePersistido.cpp
    profiles/DemoPerfilesSatService.h
    profiles/DemoPerfilesSatService.cpp
    requests/SolicitudDtos.h
    requests/PreparacionSolicitud.h
    requests/PreparacionSolicitud.cpp
    requests/SolicitudesService.h
    requests/SolicitudesServicePersistido.h
    requests/SolicitudesServicePersistido.cpp
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
