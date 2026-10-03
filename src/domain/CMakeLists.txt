# satcfdi_domain: entidades, value objects, estados y reglas puras.
# Solo depende de Qt6::Core (sin Quick, QML, SQL, red, archivos ni Keychain).
qt_add_library(satcfdi_domain STATIC
    common/UuidCanonico.h
    common/UuidCanonico.cpp
    perfiles/PerfilId.h
    perfiles/PerfilId.cpp
    solicitudes/SolicitudId.h
    solicitudes/SolicitudId.cpp
    solicitudes/EstadosSolicitud.h
    solicitudes/EstadosSolicitud.cpp
    solicitudes/EstadoResumen.h
    solicitudes/EstadoResumen.cpp
    paquetes/EstadoDescarga.h
    paquetes/EstadoDescarga.cpp
)

# Convencion de includes: rutas relativas a src/, por ejemplo
# #include "domain/solicitudes/SolicitudId.h".
target_include_directories(satcfdi_domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(satcfdi_domain PUBLIC cxx_std_20)

target_link_libraries(satcfdi_domain PUBLIC Qt6::Core)

satcfdi_enable_warnings(satcfdi_domain)
