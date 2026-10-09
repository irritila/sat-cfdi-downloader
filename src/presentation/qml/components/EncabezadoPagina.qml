import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Encabezado de pagina (T013, ficha EncabezadoPagina): alto 52, fondo
// Theme.fondo con linea inferior Theme.separador. De izquierda a derecha:
// boton de navegacion (variante secundaria, `mostrarRegresar`), titulo
// (titulo 20/600, o 15/600 con `subtitulo` en leyenda debajo), `contador`
// opcional, espacio flexible y las acciones (propiedad por omision).
ToolBar {
    id: encabezado

    property string titulo: ""
    property string subtitulo: ""
    property string contador: ""
    property bool mostrarRegresar: false
    property string textoRegresar: qsTr("Regresar")
    property string descripcionRegresar: qsTr("Regresar a la lista de solicitudes (Escape)")
    // Primer elemento enfocable del encabezado (boton de regreso).
    readonly property alias botonRegresar: regresar
    default property alias acciones: contenedorAcciones.data

    signal regresarSolicitado()

    implicitHeight: Theme.altoEncabezado
    leftPadding: Theme.espacioL
    rightPadding: Theme.espacioL
    topPadding: 0
    bottomPadding: 0

    Accessible.role: Accessible.ToolBar
    Accessible.name: encabezado.titulo

    background: Rectangle {
        color: Theme.fondo
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.separador
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.espacioS

        BotonAccion {
            id: regresar
            objectName: "botonRegresar"
            visible: encabezado.mostrarRegresar
            variante: "navegacion"
            text: encabezado.textoRegresar
            descripcion: encabezado.descripcionRegresar
            onClicked: encabezado.regresarSolicitado()
        }
        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter

            RowLayout {
                spacing: Theme.espacioS
                Layout.fillWidth: true
                Label {
                    objectName: "tituloEncabezado"
                    text: encabezado.titulo
                    color: Theme.texto
                    font.family: Theme.familia
                    font.pixelSize: encabezado.subtitulo.length > 0 ? Theme.subtitulo.size : Theme.titulo.size
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: encabezado.contador.length === 0
                    Accessible.role: Accessible.Heading
                    Accessible.name: encabezado.titulo
                }
                Label {
                    objectName: "contadorEncabezado"
                    visible: encabezado.contador.length > 0
                    text: encabezado.contador
                    color: Theme.textoSecundario
                    font.family: Theme.familia
                    font.pixelSize: Theme.subtitulo.size
                    Layout.fillWidth: true
                }
            }
            Label {
                objectName: "subtituloEncabezado"
                visible: encabezado.subtitulo.length > 0
                text: encabezado.subtitulo
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        RowLayout {
            id: contenedorAcciones
            spacing: Theme.espacioS
        }
    }
}
