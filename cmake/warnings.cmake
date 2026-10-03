# Advertencias de compilacion por target.
#
# Uso:
#   satcfdi_enable_warnings(<target>)
#
# No se activa -Werror de forma global. Para tratar advertencias como errores
# (por ejemplo en CI) configurar con -DSATCFDI_WARNINGS_AS_ERRORS=ON.

option(SATCFDI_WARNINGS_AS_ERRORS "Tratar advertencias como errores en targets del proyecto" OFF)

function(satcfdi_enable_warnings target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "satcfdi_enable_warnings: '${target}' no es un target")
    endif()

    get_target_property(_type ${target} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY")
        set(_scope INTERFACE)
    else()
        set(_scope PRIVATE)
    endif()

    if(MSVC)
        target_compile_options(${target} ${_scope} /W4 /permissive-)
        if(SATCFDI_WARNINGS_AS_ERRORS)
            target_compile_options(${target} ${_scope} /WX)
        endif()
    else()
        target_compile_options(${target} ${_scope}
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wcast-align
            -Wconversion
            -Wsign-conversion
            -Wnull-dereference
            -Wimplicit-fallthrough
        )
        if(SATCFDI_WARNINGS_AS_ERRORS)
            target_compile_options(${target} ${_scope} -Werror)
        endif()
    endif()
endfunction()
