import QtQuick

// T019 D8: franja de la ventana principal con el aviso de la carpeta de
// paquetes (conflicto o error de la migracion, o raiz nueva no disponible).
// Va arriba de todas las paginas durante toda la sesion: no tiene boton de
// cerrar y su contenido no cambia (AvisoRaizPaquetesViewModel lo fija una
// vez). Sin aviso no ocupa espacio. Patron del aviso de notificaciones de
// Solicitudes: AvisoEnLinea sobre una franja `superficie` con padding 12/16
// y separador inferior. Rol AlertMessage: VoiceOver lo lee al abrir la
// ventana; su nombre accesible es titulo + descripcion (con las rutas ~/...).
Rectangle {
    id: franja
    objectName: "franjaAvisoRaizPaquetes"

    required property AvisoRaizPaquetesViewModel aviso

    visible: franja.aviso.activo
    implicitHeight: franja.visible ? contenido.implicitHeight + 2 * Theme.espacioM + separador.height : 0
    color: Theme.superficie

    AvisoEnLinea {
        id: contenido
        objectName: "avisoRaizPaquetes"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.espacioL
        anchors.rightMargin: Theme.espacioL
        anchors.topMargin: Theme.espacioM
        variante: franja.aviso.variante
        titulo: franja.aviso.titulo
        descripcion: franja.aviso.descripcion
    }

    Rectangle {
        id: separador
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.separador
    }
}
