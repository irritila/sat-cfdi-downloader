import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Encabezado comun: boton opcional de regreso, titulo y acciones a la derecha.
ToolBar {
    id: encabezado

    property string titulo: ""
    property bool mostrarRegresar: false
    property string textoRegresar: qsTr("Regresar")
    property string descripcionRegresar: qsTr("Regresar a la lista de solicitudes (Escape)")
    // Primer elemento enfocable del encabezado (boton de regreso).
    readonly property alias botonRegresar: regresar
    default property alias acciones: contenedorAcciones.data

    signal regresarSolicitado()

    Accessible.role: Accessible.ToolBar
    Accessible.name: encabezado.titulo

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 8

        BotonAccion {
            id: regresar
            objectName: "botonRegresar"
            visible: encabezado.mostrarRegresar
            text: encabezado.textoRegresar
            descripcion: encabezado.descripcionRegresar
            onClicked: encabezado.regresarSolicitado()
        }
        Label {
            text: encabezado.titulo
            font.pixelSize: 18
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.role: Accessible.Heading
            Accessible.name: encabezado.titulo
        }
        RowLayout {
            id: contenedorAcciones
            spacing: 8
        }
    }
}
