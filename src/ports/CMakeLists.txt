# satcfdi_ports: contratos C++ (solo headers). Desde T003 los repositorios,
# UnitOfWork y LogSanitizer tienen metodos; SatGateway, SecretStore,
# OSIntegration y PackageStorage siguen siendo fronteras sin metodos.
add_library(satcfdi_ports INTERFACE)

target_sources(satcfdi_ports INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/SatGateway.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SecretStore.h
    ${CMAKE_CURRENT_SOURCE_DIR}/OSIntegration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/PackageStorage.h
    ${CMAKE_CURRENT_SOURCE_DIR}/LogSanitizer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/persistence/ErrorPersistencia.h
    ${CMAKE_CURRENT_SOURCE_DIR}/persistence/Exito.h
    ${CMAKE_CURRENT_SOURCE_DIR}/repositories/PerfilSatRepository.h
    ${CMAKE_CURRENT_SOURCE_DIR}/repositories/SolicitudMasivaRepository.h
    ${CMAKE_CURRENT_SOURCE_DIR}/repositories/PaqueteSolicitudRepository.h
    ${CMAKE_CURRENT_SOURCE_DIR}/repositories/LogSolicitudRepository.h
    ${CMAKE_CURRENT_SOURCE_DIR}/repositories/ConfiguracionAppRepository.h
    ${CMAKE_CURRENT_SOURCE_DIR}/repositories/UnitOfWork.h
)

# Includes relativos a src/, por ejemplo #include "ports/SatGateway.h".
target_include_directories(satcfdi_ports INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(satcfdi_ports INTERFACE satcfdi_domain)
