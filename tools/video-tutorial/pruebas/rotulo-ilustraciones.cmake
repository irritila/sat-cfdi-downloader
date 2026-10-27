# T017 D14: con "rotuloHerramienta": false (omision) el rotulo "Ilustración"
# lo pone cada ilustracion de tools/capturas-manual/qml. Esta prueba falla si
# alguna de las que usa el video deja de mostrarlo. Variables: DIR (capturas)
# y DIR_VIDEO (T024.4: ilustraciones propias del video y banner de
# notificacion de Superposicion.qml).
foreach(archivo IlustracionMenuBar IlustracionIconoMenuBar IlustracionFinder IlustracionSelectorArchivos
                IlustracionNotificacion)
    file(READ "${DIR}/${archivo}.qml" qml)
    string(FIND "${qml}" "text: \"Ilustración\"" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "${archivo}.qml ya no muestra el rotulo \"Ilustración\" (D14)")
    endif()
endforeach()
file(READ "${DIR_VIDEO}/IlustracionSelectorCarpeta.qml" qml)
string(FIND "${qml}" "text: \"Ilustración\"" pos)
if(pos EQUAL -1)
    message(FATAL_ERROR "IlustracionSelectorCarpeta.qml ya no muestra el rotulo \"Ilustración\" (D14)")
endif()
# El banner (mostrarBanner) lleva su propio rotulo, con la opacidad del banner.
file(READ "${DIR_VIDEO}/Superposicion.qml" qml)
string(REGEX MATCH "objectName: \"rotuloBanner\"[^}]*text: \"Ilustración\"" rotulo "${qml}")
if(rotulo STREQUAL "")
    message(FATAL_ERROR "el banner de notificacion de Superposicion.qml ya no muestra el rotulo \"Ilustración\" (D14)")
endif()
message("OK rotulo en las 6 ilustraciones y en el banner")
