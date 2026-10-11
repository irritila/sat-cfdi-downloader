# satcfdi_application: contratos funcionales consumidos por presentacion,
# DTOs, servicios (demo y persistidos), PersistenceDispatcher y
# RegexLogSanitizer y, desde T007, OperacionExecutor/WorkerLocal (hilo propio
# del ejecutor). Sin SQL, red, archivos ni timers (el Programador de
# produccion vive en app_core/ProgramadorQt). Unico hilo permitido:
# el QThread de PersistenceDispatcher (DA4). Los servicios Demo* y *Persistido
# solo se instancian en el composition root (o en pruebas).
qt_add_library(satcfdi_application STATIC
    common/Errores.h
    common/FechasLegibles.h
    common/FechasLegibles.cpp
    configuration/ConfiguracionAppService.h
    configuration/ConfiguracionAppServicePersistido.h
    configuration/ConfiguracionAppServicePersistido.cpp
    logging/RegexLogSanitizer.h
    logging/RegexLogSanitizer.cpp
    operaciones/OperacionesSat.h
    operaciones/OperacionesSatNulo.h
    operaciones/OperacionesSatProductivo.h
    operaciones/OperacionesSatProductivo.cpp
    operaciones/MensajesOperacionSat.h
    operaciones/MensajesOperacionSat.cpp
    operaciones/SaneamientoOperacion.h
    operaciones/SaneamientoOperacion.cpp
    notificaciones/Notificador.h
    estadoagregado/EstadoAgregado.h
    estadoagregado/EstadoAgregado.cpp
    estadoagregado/MonitorEstadoAgregado.h
    estadoagregado/MonitorEstadoAgregado.cpp
    notificaciones/ServicioNotificaciones.h
    notificaciones/ServicioNotificaciones.cpp
    operaciones/SenalCancelacion.h
    operaciones/SenalCancelacion.cpp
    operaciones/Programador.h
    operaciones/TiposOperacion.h
    operaciones/TiposOperacion.cpp
    operaciones/OperacionExecutor.h
    operaciones/OperacionExecutor.cpp
    operaciones/WorkerLocal.h
    operaciones/WorkerLocal.cpp
    paquetes/AccesoPaquetesService.h
    primeruso/ConsultaPrimerUso.h
    vencimiento/AvisoVencimientoEFirma.h
    vencimiento/AvisoVencimientoEFirma.cpp
    primeruso/ConsultaPrimerUsoPersistida.h
    primeruso/ConsultaPrimerUsoPersistida.cpp
    paquetes/AccesoPaquetesService.cpp
    persistence/PersistenceDispatcher.h
    persistence/PersistenceDispatcher.cpp
    persistence/PuertosPersistencia.h
    profiles/AccesoCredencialSat.h
    profiles/ConsultaPreparacionPerfiles.h
    profiles/ConsultaPreparacionPerfiles.cpp
    profiles/CredencialesSatService.h
    profiles/CredencialesSatServicePersistido.h
    profiles/CredencialesSatServicePersistido.cpp
    profiles/PerfilConPreparacion.h
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
