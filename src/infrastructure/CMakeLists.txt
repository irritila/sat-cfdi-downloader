# satcfdi_infrastructure: target minimo en T002 para conservar el grafo de
# capas. No contiene adaptadores SQLite, SAT, Keychain, archivos ni macOS; no
# enlaza Qt6::Sql ni Qt6::Network. Los adaptadores llegan desde T003.
# persistence/migrations/001_initial_schema.sql (T001) no se compila aqui.
qt_add_library(satcfdi_infrastructure STATIC
    InfrastructureAnchor.cpp
)

target_include_directories(satcfdi_infrastructure PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(satcfdi_infrastructure PUBLIC
    satcfdi_domain
    satcfdi_ports
    Qt6::Core
)

satcfdi_enable_warnings(satcfdi_infrastructure)
