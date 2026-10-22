# satcfdi_ports: contratos C++. Desde T004 es una libreria estatica minima
# (Qt Core + MOC) porque OSIntegration es un QObject abstracto con senales y
# Q_ENUM. Desde T005 incluye secrets/SecretStoreTypes.cpp (buffers move-only
# con limpieza best effort, CredencialRef). El resto de puertos son headers. Sin SQL, red,
# Widgets ni frameworks nativos.
qt_add_library(satcfdi_ports STATIC
    SatGateway.h
    SecretStore.h
    secrets/SecretStoreTypes.h
    secrets/SecretStoreTypes.cpp
    OSIntegration.h
    OSIntegration.cpp
    PackageStorage.h
    LogSanitizer.h
    persistence/ErrorPersistencia.h
    persistence/Exito.h
    repositories/PerfilSatRepository.h
    repositories/CredencialSatRepository.h
    repositories/OperacionesSolicitudRepository.h
    repositories/SolicitudMasivaRepository.h
    repositories/PaqueteSolicitudRepository.h
    repositories/LogSolicitudRepository.h
    repositories/ConfiguracionAppRepository.h
    repositories/AvisoVencimientoRepository.h
    repositories/UnitOfWork.h
    # T015.1: lector de paquetes ZIP de CFDI (implementacion con libzip).
    cfdi/CfdiArchiveReader.h
    # T015.2: indice de CFDI (catalogo SQLite y archivos ZIP locales).
    cfdi/ArchivosZipCfdi.h
    repositories/CfdiCatalogRepository.h
    # T023.2: catalogo mensual y lotes de descarga guiada.
    repositories/CatalogoMensualRepository.h
    repositories/LoteDescargaGuiadaRepository.h
    # T025.1: contenedor de respaldo cifrado AFB1.
    respaldo/ContenedorRespaldo.h
    # T025.2: captura consistente para el respaldo.
    respaldo/FuenteRespaldo.h
    respaldo/FuenteRespaldo.cpp
    # T025.3: destino del respaldo.
    respaldo/DestinoRespaldo.h
)

set_target_properties(satcfdi_ports PROPERTIES AUTOMOC ON)

# Includes relativos a src/, por ejemplo #include "ports/SatGateway.h".
target_include_directories(satcfdi_ports PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(satcfdi_ports PUBLIC
    satcfdi_domain
    Qt6::Core
)

satcfdi_enable_warnings(satcfdi_ports)
