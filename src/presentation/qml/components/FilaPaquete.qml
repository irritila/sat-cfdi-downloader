import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import "Etiquetas.js" as Etiquetas

// Paquete del SAT en la pestana Paquetes (T013, ficha FilaPaquete): columna de
// 150 con el EstadoBadge, bloque central con el nombre del paquete (mono,
// seleccionable), metadatos en leyenda y un mensaje opcional con icono de 14 en
// el tono `tonoMensaje` ("exito", "advertencia", "error" o "neutro"). En
// Descargando se muestra una barra indeterminada en lugar del mensaje. A la
// derecha, "Mostrar en Finder" (BotonAccion sm) solo en Descargado; se habilita
// con `finderHabilitado`.
Item {
    id: fila

    property string idPaquete: ""
    property string estado: ""
    property string metadatos: ""
    property string mensaje: ""
    property string tonoMensaje: "neutro"
    property bool finderHabilitado: false
    property string objectNameFinder: ""
    // objectName del texto del mensaje (para pruebas, D10).
    property string objectNameMensaje: "mensajePaquete"
    readonly property alias botonFinder: finder

    signal mostrarEnFinder()

    readonly property string textoEstado: Etiquetas.estadoDescarga(fila.estado)
    readonly property string sufijo: {
        const i = fila.idPaquete.lastIndexOf("_")
        return i >= 0 ? fila.idPaquete.substring(i) : fila.idPaquete
    }
    readonly property color colorMensaje: {
        switch (fila.tonoMensaje) {
        case "exito": return Theme.exito
        case "advertencia": return Theme.advertencia
        case "error": return Theme.error
        default: return Theme.textoSecundario
        }
    }

    Layout.fillWidth: true
    implicitWidth: 150 + centro.implicitWidth + finder.implicitWidth + 2 * Theme.espacioL + 2 * Theme.espacioM
    implicitHeight: Math.max(centro.implicitHeight, Theme.altoBadge) + 24

    Accessible.role: Accessible.ListItem
    Accessible.name: [qsTr("Paquete %1").arg(fila.sufijo), fila.textoEstado, fila.mensaje]
                     .filter(t => t.length > 0).join(", ")

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.espacioL
        anchors.rightMargin: Theme.espacioL
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        spacing: Theme.espacioM

        Item {
            Layout.preferredWidth: 150 - Theme.espacioM
            Layout.preferredHeight: badge.implicitHeight
            Layout.alignment: Qt.AlignTop
            EstadoBadge {
                id: badge
                objectName: "badgePaquete"
                clave: fila.estado
                texto: fila.textoEstado
                contexto: qsTr("Estado de descarga")
            }
        }
        ColumnLayout {
            id: centro
            spacing: 2
            Layout.fillWidth: true

            TextEdit {
                objectName: "nombrePaquete"
                text: fila.idPaquete
                readOnly: true
                selectByMouse: true
                activeFocusOnTab: false
                color: Theme.texto
                selectionColor: Theme.seleccion
                selectedTextColor: Theme.texto
                font.family: Theme.familiaMono
                font.pixelSize: Theme.mono.size
                font.weight: Font.Medium
                wrapMode: TextEdit.WrapAnywhere
                Layout.fillWidth: true
                Accessible.ignored: true
            }
            Label {
                objectName: "metadatosPaquete"
                visible: text.length > 0
                text: fila.metadatos
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Accessible.ignored: true
            }
            ProgressBar {
                objectName: "progresoPaquete"
                visible: fila.estado === "Descargando"
                indeterminate: true
                Layout.fillWidth: true
                Layout.maximumWidth: 240
                Accessible.name: qsTr("Descargando")
            }
            RowLayout {
                visible: fila.estado !== "Descargando" && fila.mensaje.length > 0
                spacing: Theme.espacioXs
                Layout.fillWidth: true
                Icono {
                    nombre: fila.tonoMensaje === "exito" ? "check-circle"
                            : fila.tonoMensaje === "error" ? "exclamation-octagon"
                            : fila.tonoMensaje === "advertencia" ? "exclamation-triangle" : "clock"
                    color: fila.colorMensaje
                    tamano: 14
                    Layout.alignment: Qt.AlignTop
                }
                Label {
                    objectName: fila.objectNameMensaje
                    text: fila.mensaje
                    Accessible.name: fila.mensaje
                    color: fila.colorMensaje
                    font.family: Theme.familia
                    font.pixelSize: Theme.leyenda.size
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
        BotonAccion {
            id: finder
            objectName: fila.objectNameFinder
            visible: fila.estado === "Descargado"
            enabled: fila.finderHabilitado
            compacto: true
            icono: "folder"
            text: qsTr("Mostrar en Finder")
            descripcion: qsTr("Mostrar el archivo del paquete %1 en Finder").arg(fila.idPaquete)
            Layout.alignment: Qt.AlignTop
            onClicked: fila.mostrarEnFinder()
        }
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.separador
    }
}
