# satcfdi_domain: entidades, value objects, estados, registros persistibles y
# reglas puras. Solo depende de Qt6::Core (sin Quick, QML, SQL, red, archivos
# ni Keychain).
qt_add_library(satcfdi_domain STATIC
    common/Resultado.h
    common/UuidCanonico.h
    common/UuidCanonico.cpp
    common/TimestampUtc.h
    common/TimestampUtc.cpp
    common/Rfc.h
    common/Rfc.cpp
    configuracion/ConfiguracionApp.h
    credenciales/CredencialSat.h
    credenciales/VencimientoEFirma.h
    credenciales/VencimientoEFirma.cpp
    logs/LogSolicitud.h
    logs/LogSolicitud.cpp
    perfiles/PerfilId.h
    perfiles/PerfilId.cpp
    perfiles/PerfilSat.h
    solicitudes/SolicitudId.h
    solicitudes/SolicitudId.cpp
    solicitudes/EstadosSolicitud.h
    solicitudes/EstadosSolicitud.cpp
    solicitudes/EstadoResumen.h
    solicitudes/EstadoResumen.cpp
    solicitudes/OperacionSat.h
    solicitudes/OperacionSat.cpp
    solicitudes/DedupKey.h
    solicitudes/DedupKey.cpp
    solicitudes/SolicitudCanonica.h
    solicitudes/SolicitudCanonica.cpp
    solicitudes/Duplicados.h
    solicitudes/Duplicados.cpp
    solicitudes/SolicitudPersistida.h
    paquetes/EstadoDescarga.h
    paquetes/EstadoDescarga.cpp
    paquetes/PaquetePersistido.h
    paquetes/PaquetePersistido.cpp
    operaciones/FallaOperacion.h
    operaciones/FallaOperacion.cpp
    operaciones/ResultadosOperacion.h
    operaciones/RegistrosOperacion.h
    operaciones/PoliticasOperacion.h
    operaciones/PoliticasOperacion.cpp
    operaciones/TransicionNotificable.h
    # T015.1: CFDI (resumen, detalle, decimal exacto, UUID).
    cfdi/ImporteCfdi.h
    cfdi/ImporteCfdi.cpp
    cfdi/UuidCfdi.h
    cfdi/UuidCfdi.cpp
    cfdi/TipoComprobante.h
    cfdi/TipoComprobante.cpp
    cfdi/ResumenCfdi.h
    cfdi/DetalleCfdi.h
    cfdi/ResultadoParseoCfdi.h
    # T015.2: estado de indice, huella y texto de busqueda.
    cfdi/IndiceCfdi.h
    cfdi/IndiceCfdi.cpp
    # T023.2: mes calendario (Centro de Mexico) y regla de cobertura mensual.
    cfdi/MesCalendario.h
    cfdi/MesCalendario.cpp
    cfdi/CoberturaMensual.h
    cfdi/CoberturaMensual.cpp
    cfdi/LoteDescargaGuiada.h
    cfdi/LoteDescargaGuiada.cpp
    # T023.3: plan puro de la descarga guiada y oferta.
    cfdi/PlanDescargaGuiada.h
    cfdi/PlanDescargaGuiada.cpp
    # T026.1: alcance, plan, nombres y omitidas de la exportacion de facturas.
    cfdi/ExportacionFacturas.h
    cfdi/ExportacionFacturas.cpp
)

# Convencion de includes: rutas relativas a src/, por ejemplo
# #include "domain/solicitudes/SolicitudId.h".
target_include_directories(satcfdi_domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(satcfdi_domain PUBLIC cxx_std_20)

target_link_libraries(satcfdi_domain PUBLIC Qt6::Core)

satcfdi_enable_warnings(satcfdi_domain)
