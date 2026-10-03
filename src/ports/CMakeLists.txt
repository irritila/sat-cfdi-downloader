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
    repositories/SolicitudMasivaRepository.h
    repositories/PaqueteSolicitudRepository.h
    repositories/LogSolicitudRepository.h
    repositories/ConfiguracionAppRepository.h
    repositories/UnitOfWork.h
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
