# T017 D14: con "rotuloHerramienta": false (omision) el rotulo "Ilustración"
# lo pone cada ilustracion de tools/capturas-manual/qml. Esta prueba falla si
# alguna de las que usa el video deja de mostrarlo. Variable: DIR.
foreach(archivo IlustracionMenuBar IlustracionIconoMenuBar IlustracionFinder IlustracionSelectorArchivos
                IlustracionNotificacion)
    file(READ "${DIR}/${archivo}.qml" qml)
    string(FIND "${qml}" "text: \"Ilustración\"" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "${archivo}.qml ya no muestra el rotulo \"Ilustración\" (D14)")
    endif()
endforeach()
message("OK rotulo en las 5 ilustraciones")
