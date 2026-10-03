# satcfdi_infrastructure: adaptadores de infraestructura. Desde T003 contiene
# la persistencia SQLite (ADR 0016): proveedor de conexiones por hilo, runner de
# migraciones, repositorios y UnitOfWork. Sin Qt6::Network, Keychain ni ZIP.
# Qt6::Sql lo busca el CMake raiz.

qt_add_library(satcfdi_infrastructure STATIC
    persistence/sqlite/SqliteTipos.h
    persistence/sqlite/SqliteSoporte.h
    persistence/sqlite/SqliteSoporte.cpp
    persistence/sqlite/SqliteConnectionProvider.h
    persistence/sqlite/SqliteConnectionProvider.cpp
    persistence/sqlite/SqliteMigrationRunner.h
    persistence/sqlite/SqliteMigrationRunner.cpp
    persistence/sqlite/SqliteUnitOfWork.h
    persistence/sqlite/SqliteUnitOfWork.cpp
    persistence/sqlite/SqliteRepositorios.h
    persistence/sqlite/SqlitePerfilSatRepository.cpp
    persistence/sqlite/SqliteSolicitudMasivaRepository.cpp
    persistence/sqlite/SqlitePaqueteSolicitudRepository.cpp
    persistence/sqlite/SqliteLogSolicitudRepository.cpp
    persistence/sqlite/SqliteConfiguracionAppRepository.cpp
    persistence/sqlite/SqlitePersistencia.h
    persistence/sqlite/SqlitePersistencia.cpp
)

# Migraciones embebidas: `:/migrations/001_initial_schema.sql`.
qt_add_resources(satcfdi_infrastructure "satcfdi_migrations"
    PREFIX "/migrations"
    BASE "persistence/migrations"
    FILES
        persistence/migrations/001_initial_schema.sql
)

target_include_directories(satcfdi_infrastructure PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(satcfdi_infrastructure
    PUBLIC
        satcfdi_domain
        satcfdi_ports
        Qt6::Core
    PRIVATE
        Qt6::Sql
)

satcfdi_enable_warnings(satcfdi_infrastructure)
