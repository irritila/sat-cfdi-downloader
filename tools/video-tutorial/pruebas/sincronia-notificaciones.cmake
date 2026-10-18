# T017: la ilustracion de notificaciones copia la regla de acciones de
# notificacionLocalDe (src/app_core/IntegracionWorker.cpp) porque la
# herramienta no enlaza satcfdi_app_core. Esta prueba falla si el original
# cambia: revisa tools/video-tutorial/NotificacionesIlustradas.cpp y actualiza
# la huella SATCFDI_VIDEO_HUELLA_NOTIFICACIONES en su CMakeLists.txt.
# Variables: ORIGEN (IntegracionWorker.cpp), HUELLA (SHA-256 esperado).
file(READ "${ORIGEN}" texto)
set(inicio "OSIntegration::NotificacionLocal notificacionLocalDe(const Notificacion& n)")
string(FIND "${texto}" "${inicio}" pos)
if(pos EQUAL -1)
    message(FATAL_ERROR "no se encontro notificacionLocalDe en ${ORIGEN}")
endif()
string(SUBSTRING "${texto}" ${pos} -1 resto)
string(FIND "${resto}" "\n}\n" fin)
if(fin EQUAL -1)
    message(FATAL_ERROR "no se encontro el fin de notificacionLocalDe")
endif()
math(EXPR largo "${fin} + 3")
string(SUBSTRING "${resto}" 0 ${largo} funcion)
string(SHA256 huella "${funcion}")
if(NOT huella STREQUAL HUELLA)
    message(FATAL_ERROR "notificacionLocalDe cambio (huella ${huella}); revisa la copia en "
                        "tools/video-tutorial/NotificacionesIlustradas.cpp y actualiza la huella")
endif()
message("OK notificacionLocalDe sin cambios (${huella})")
