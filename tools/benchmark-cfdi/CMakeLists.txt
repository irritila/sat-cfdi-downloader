# tools/benchmark-cfdi (T015.2, criterio de rendimiento): 10,000 CFDI
# sinteticos (20 paquetes de 500) en una base SQLite temporal con la
# migracion 006; mide la lista ordenada por fecha y la busqueda por texto
# (20 repeticiones en caliente) e imprime p50 y p95. Solo con
# -DSATCFDI_BUILD_BENCHMARK_CFDI=ON; no es parte de la app ni de ctest.
# Ejecutar: cmake --build <build> --target benchmark_cfdi
qt_add_executable(satcfdi_benchmark_cfdi
    main.cpp
)
target_link_libraries(satcfdi_benchmark_cfdi PRIVATE
    satcfdi_infrastructure
    satcfdi_cfdi_xml
    satcfdi_application
    satcfdi_domain
    satcfdi_ports
    # Fixtures de perfil/solicitud/paquetes con SQL directo.
    Qt6::Sql
    Qt6::Core
)
satcfdi_enable_warnings(satcfdi_benchmark_cfdi)

add_custom_target(benchmark_cfdi
    COMMAND $<TARGET_FILE:satcfdi_benchmark_cfdi>
    DEPENDS satcfdi_benchmark_cfdi
    USES_TERMINAL
    COMMENT "Benchmark del indice de CFDI (10,000 CFDI, 20 repeticiones en caliente)"
)
