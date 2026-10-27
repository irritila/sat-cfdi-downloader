# T020: el wordmark y el lockup versionados son reproducibles.
#
#   cmake -DHERRAMIENTA=<branding_render> -DBRANDING=<resources/branding> -DDIR=<tmp> -P comparar-wordmark.cmake
#
# Regenera wordmark.svg, wordmark-mono.svg y lockup-video.svg en DIR con la
# Inter versionada y marca-video.svg, y exige bytes iguales a los de
# BRANDING/marca. No modifica el repositorio.
foreach(_v HERRAMIENTA BRANDING DIR)
    if(NOT ${_v})
        message(FATAL_ERROR "comparar-wordmark: falta -D${_v}")
    endif()
endforeach()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
execute_process(
    COMMAND "${HERRAMIENTA}" wordmark
            --fuente "${BRANDING}/fonts/Inter/Inter-SemiBold.ttf"
            --marca "${BRANDING}/video/marca-video.svg"
            --salida "${DIR}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "branding_render wordmark fallo (${_rc}):\n${_out}${_err}")
endif()

set(_distintos "")
foreach(_f IN ITEMS wordmark.svg wordmark-mono.svg lockup-video.svg)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${DIR}/${_f}" "${BRANDING}/marca/${_f}"
                    RESULT_VARIABLE _igual)
    if(NOT _igual EQUAL 0)
        list(APPEND _distintos "${_f}")
    endif()
endforeach()
if(_distintos)
    message(FATAL_ERROR "Wordmark versionado distinto del regenerado: ${_distintos}. "
                        "Regenerar con el target branding_wordmark (SATCFDI_BUILD_BRANDING=ON) "
                        "y revisar el diff. Salida en ${DIR}")
endif()
file(REMOVE_RECURSE "${DIR}")
message(STATUS "wordmark, wordmark-mono y lockup-video reproducibles")
