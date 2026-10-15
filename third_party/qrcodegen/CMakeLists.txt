# qrcodegen (Nayuki, v1.8.0, MIT): codigo QR de verificacion del SAT en el PDF
# de CFDI (T015.4, T015 D8). Fuentes de terceros copiados SIN modificar (ver
# ORIGEN.txt); aqui solo se compilan como biblioteca estatica propia.
#
# Sin satcfdi_enable_warnings ni -Werror: las advertencias de terceros no deben
# romper el build del proyecto (SATCFDI_WARNINGS_AS_ERRORS solo aplica a
# targets propios). Los includes se exponen como SYSTEM para que tampoco
# generen advertencias en quien los incluye. Solo lo enlaza (PRIVATE)
# satcfdi_cfdi_pdf y sus pruebas. Empaquetado: estatico, sin dylib (T015 D12).
add_library(satcfdi_qrcodegen STATIC
    qrcodegen.hpp
    qrcodegen.cpp
)
target_include_directories(satcfdi_qrcodegen SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(satcfdi_qrcodegen PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
if(NOT MSVC)
    target_compile_options(satcfdi_qrcodegen PRIVATE -w)
endif()
