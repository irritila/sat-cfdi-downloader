import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Fila de formulario (T013, ficha CampoFormulario): etiqueta de
// `anchoEtiqueta` (168) a la izquierda y, a la derecha, el control (contenido
// por omision: CampoTexto, CampoCombo, SelectorSegmentado, CampoArchivo...),
// la ayuda en leyenda Theme.textoSecundario y el error en leyenda Theme.error
// con icono. Separador inferior opcional. El control debe declarar
// Layout.fillWidth y su propio nombre accesible; `descripcionAccesible`
// (error o ayuda) sirve para su Accessible.description.
Item {
    id: fila

    property string etiqueta: ""
    property string ayuda: ""
    property string error: ""
    property int anchoEtiqueta: 168
    property bool separador: true
    // objectName de la fila de error (para pruebas, D10).
    property string objectNameError: "errorCampo"
    default property alias contenido: contenedor.data

    readonly property string descripcionAccesible: fila.error.length > 0 ? fila.error : fila.ayuda

    Layout.fillWidth: true
    implicitWidth: fila.anchoEtiqueta + columna.implicitWidth + 24
    implicitHeight: Math.max(etiquetaTexto.implicitHeight, columna.implicitHeight) + 20

    Label {
        id: etiquetaTexto
        objectName: "etiquetaCampo"
        x: 12
        y: 10 + Math.max(0, (Theme.altoControl - implicitHeight) / 2)
        width: fila.anchoEtiqueta - Theme.espacioM
        text: fila.etiqueta
        color: Theme.texto
        font.family: Theme.familia
        font.pixelSize: Theme.cuerpo.size
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }

    ColumnLayout {
        id: columna
        x: fila.anchoEtiqueta + 12
        y: 10
        width: fila.width - x - 12
        spacing: 4

        ColumnLayout {
            id: contenedor
            spacing: Theme.espacioXs
            Layout.fillWidth: true
        }
        Label {
            objectName: "ayudaCampo"
            visible: fila.ayuda.length > 0 && fila.error.length === 0
            text: fila.ayuda
            color: Theme.textoSecundario
            font.family: Theme.familia
            font.pixelSize: Theme.leyenda.size
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.ignored: true
        }
        RowLayout {
            objectName: fila.objectNameError
            visible: fila.error.length > 0
            spacing: Theme.espacioXs
            Layout.fillWidth: true
            Accessible.role: Accessible.AlertMessage
            Accessible.name: qsTr("Error en %1: %2").arg(fila.etiqueta).arg(fila.error)

            Icono {
                nombre: "exclamation-octagon"
                color: Theme.error
                tamano: 13
                Layout.alignment: Qt.AlignTop
            }
            Label {
                text: fila.error
                color: Theme.error
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Accessible.ignored: true
            }
        }
    }

    Rectangle {
        visible: fila.separador
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.separador
    }
}
