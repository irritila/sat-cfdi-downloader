import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Archivo elegido y boton "Elegir…" (T013, ficha CampoArchivo). Solo muestra
// el nombre del archivo, nunca la ruta. Variantes: vacio (`textoVacio` en
// Theme.textoSecundario), elegido (icono `iconoArchivo`, nombre y
// check-circle en Theme.exito) y `conError` (borde Theme.error). Solo el boton
// recibe foco; su nombre accesible es `nombreBoton` y su descripcion, el
// archivo actual.
RowLayout {
    id: campo

    property string nombreArchivo: ""
    property string textoVacio: qsTr("Ningún archivo")
    property string iconoArchivo: "doc"
    property bool conError: false
    property string textoBoton: qsTr("Elegir…")
    property string nombreBoton: campo.textoBoton
    // objectName del boton y del texto del nombre (para pruebas, D10).
    property string objectNameBoton: ""
    property string objectNameNombre: ""
    readonly property alias boton: elegir
    readonly property alias textoNombre: nombre

    signal elegirSolicitado()

    readonly property bool elegido: campo.nombreArchivo.length > 0

    spacing: Theme.espacioS
    opacity: campo.enabled ? 1 : 0.5

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Theme.altoControl
        implicitWidth: 200
        radius: Theme.radioControl
        color: Theme.superficieSeccion
        border.width: campo.conError ? 2 : 1
        border.color: campo.conError ? Theme.error : Theme.separador

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: Theme.espacioXs

            Icono {
                visible: campo.elegido
                nombre: campo.iconoArchivo
                color: Theme.texto
                tamano: 14
            }
            Label {
                id: nombre
                objectName: campo.objectNameNombre
                text: campo.elegido ? campo.nombreArchivo : campo.textoVacio
                color: campo.elegido ? Theme.texto : Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.cuerpo.size
                elide: Text.ElideMiddle
                Layout.fillWidth: true
                Accessible.ignored: true
            }
            Icono {
                visible: campo.elegido
                nombre: "check-circle"
                color: Theme.exito
                tamano: 14
            }
        }
    }
    BotonAccion {
        id: elegir
        objectName: campo.objectNameBoton
        text: campo.textoBoton
        nombreAccesible: campo.nombreBoton
        Accessible.name: campo.nombreBoton
        descripcion: campo.elegido ? campo.nombreArchivo : campo.textoVacio
        onClicked: campo.elegirSolicitado()
    }
}
