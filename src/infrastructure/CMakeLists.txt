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

# satcfdi_os_macos: adaptador MacOSIntegration (T004, ADR 0009, DA4).
# Target separado y solo bajo APPLE para no contaminar satcfdi_infrastructure:
# esta sigue siendo portable y sin Qt6::Widgets, y sus pruebas no enlazan
# AppKit. Solo satcfdi_app debe enlazar este target (composition root en
# main); domain/ports/application/presentation/app_core no lo conocen.
# Objective-C++ (.mm) con ARC; frameworks AppKit, ServiceManagement y
# UserNotifications. Qt6::Widgets por QSystemTrayIcon/QMenu.
if(APPLE)
    enable_language(OBJCXX)

    qt_add_library(satcfdi_os_macos STATIC
        os/macos/MacOSMapeos.h
        os/macos/MacOSMapeos.cpp
        os/macos/MacOSIntegration.h
        os/macos/MacOSIntegration.mm
    )

    set_target_properties(satcfdi_os_macos PROPERTIES AUTOMOC ON)

    target_include_directories(satcfdi_os_macos PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_compile_options(satcfdi_os_macos PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )

    target_link_libraries(satcfdi_os_macos
        PUBLIC
            satcfdi_ports
            Qt6::Core
        PRIVATE
            Qt6::Gui
            Qt6::Widgets
            "-framework AppKit"
            "-framework ServiceManagement"
            "-framework UserNotifications"
    )

    satcfdi_enable_warnings(satcfdi_os_macos)

    # Pruebas de los mapeos puros (sin permisos ni bundle). Viven junto al
    # adaptador porque tests/** pertenece a otros cortes en T004.
    qt_add_executable(satcfdi_os_macos_tests
        os/macos/tests/TestMacOSMapeos.cpp
    )
    target_link_libraries(satcfdi_os_macos_tests PRIVATE
        satcfdi_os_macos
        Qt6::Test
    )
    add_test(NAME satcfdi_os_macos_tests COMMAND satcfdi_os_macos_tests)
    set_tests_properties(satcfdi_os_macos_tests PROPERTIES LABELS "unit;macos")
    satcfdi_enable_warnings(satcfdi_os_macos_tests)
endif()
