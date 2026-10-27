# tools/branding (T020 cortes 1 y 1b).
#
# - satcfdi_branding_tests (siempre, label branding): restricciones de los SVG
#   de resources/branding (sin text/filter/image/gradientes/mascaras/href/
#   fuentes; menu solo negro con alfa; viewBox de D9; dorado del wordmark).
# - branding_render (siempre; fuera de la app): wordmark/lockup a trazos con
#   Inter versionada (D8, D11), PNG de control y hoja de contactos con
#   QSvgRenderer. Ver resources/branding/README.md.
# - satcfdi_branding_wordmark (siempre, label branding): regenera wordmark,
#   wordmark-mono y lockup-video en un temporal y exige bytes iguales a los
#   versionados. Si falla tras actualizar Qt o Inter, regenerar con
#   branding_wordmark y revisar el diff.
# - Con SATCFDI_BUILD_BRANDING (OFF): targets branding_wordmark (reescribe
#   resources/branding/marca) y branding_hoja.
set(SATCFDI_BRANDING_DIR "${PROJECT_SOURCE_DIR}/resources/branding")

qt_add_executable(satcfdi_branding_tests TestRestriccionesSvg.cpp)
target_compile_definitions(satcfdi_branding_tests PRIVATE
    SATCFDI_BRANDING_DIR="${SATCFDI_BRANDING_DIR}")
target_link_libraries(satcfdi_branding_tests PRIVATE Qt6::Gui Qt6::Svg Qt6::Test)
satcfdi_enable_warnings(satcfdi_branding_tests)
add_test(NAME satcfdi_branding_tests COMMAND satcfdi_branding_tests)
set_tests_properties(satcfdi_branding_tests PROPERTIES
    LABELS branding
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(branding_render main.cpp Trazos.h Trazos.cpp)
target_link_libraries(branding_render PRIVATE Qt6::Gui Qt6::Svg)
satcfdi_enable_warnings(branding_render)
set_target_properties(branding_render PROPERTIES MACOSX_BUNDLE OFF)

add_test(NAME satcfdi_branding_wordmark
    COMMAND ${CMAKE_COMMAND}
            -DHERRAMIENTA=$<TARGET_FILE:branding_render>
            -DBRANDING=${SATCFDI_BRANDING_DIR}
            -DDIR=${CMAKE_CURRENT_BINARY_DIR}/wordmark-prueba
            -P ${CMAKE_CURRENT_SOURCE_DIR}/comparar-wordmark.cmake)
set_tests_properties(satcfdi_branding_wordmark PROPERTIES
    LABELS branding
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

option(SATCFDI_BUILD_BRANDING "Targets que reescriben resources/branding/marca y la hoja de contactos" OFF)
if(SATCFDI_BUILD_BRANDING)
    add_custom_target(branding_wordmark
        COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen
                $<TARGET_FILE:branding_render> wordmark
                --fuente ${SATCFDI_BRANDING_DIR}/fonts/Inter/Inter-SemiBold.ttf
                --marca ${SATCFDI_BRANDING_DIR}/video/marca-video.svg
                --salida ${SATCFDI_BRANDING_DIR}/marca
        DEPENDS branding_render
        USES_TERMINAL
        COMMENT "T020: wordmark.svg, wordmark-mono.svg y lockup-video.svg a trazos")

    set(SATCFDI_BRANDING_HOJA "${CMAKE_BINARY_DIR}/branding/hoja-contactos.png" CACHE FILEPATH
        "PNG de salida de la hoja de contactos de branding")
    add_custom_target(branding_hoja
        COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen
                $<TARGET_FILE:branding_render> hoja
                --raiz ${SATCFDI_BRANDING_DIR} --salida ${SATCFDI_BRANDING_HOJA}
        DEPENDS branding_render
        USES_TERMINAL
        COMMENT "T020: hoja de contactos de control")
endif()
