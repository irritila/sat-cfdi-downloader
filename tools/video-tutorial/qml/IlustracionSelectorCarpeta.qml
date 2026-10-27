pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Ilustración (T024.4, D10; 03-motion 2a) del selector de carpetas nativo de
// macOS (FolderDialog), tema claro. El video nunca abre el dialogo real: el
// guion hace `clic` con `sinEfecto`, muestra esta ilustracion y luego inyecta
// la carpeta con `elegirCarpeta`. Datos ficticios por propiedades:
// - carpeta: ruta ficticia (/Users/usuario...); el menu muestra su ultimo
//   componente (o `nombreCarpeta` si se da, p. ej. "iCloud Drive").
// - carpetas: nombres de las subcarpetas listadas.
// - seleccionada: carpeta resaltada.
// Los rotulos ("Cancelar", "Abrir", barra lateral) son de macOS, no de la app.
// Mismo estilo que tools/capturas-manual/qml/IlustracionSelectorArchivos.qml.
Item {
    id: raiz

    property string carpeta: "/Users/usuario"
    property string nombreCarpeta: ""
    property var carpetas: ["Descargas", "Documentos", "Escritorio", "Películas", "Público"]
    property string seleccionada: "Documentos"

    readonly property string nombreMostrado: {
        if (raiz.nombreCarpeta.length > 0)
            return raiz.nombreCarpeta
        const partes = raiz.carpeta.split("/").filter(p => p.length > 0)
        return partes.length > 0 ? partes[partes.length - 1] : raiz.carpeta
    }

    implicitWidth: 720
    implicitHeight: 440
    width: implicitWidth
    height: implicitHeight

    // Ventana de la app atenuada detras del panel.
    Rectangle {
        anchors.fill: parent
        color: "#e9e9ec"
        Rectangle {
            width: parent.width
            height: 28
            color: "#f6f6f7"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#d6d6da" }
        }
    }

    Rectangle {
        id: panel
        anchors.horizontalCenter: parent.horizontalCenter
        y: 28
        width: parent.width - 60
        height: parent.height - 28 - 44
        radius: 10
        color: "white"
        border.color: "#c6c6cb"
        clip: true

        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.margins: -3
            anchors.topMargin: 2
            radius: 12
            color: "#1f000000"
        }

        // Barra lateral.
        Rectangle {
            id: lateral
            width: 160
            height: parent.height
            radius: 10
            color: "#ececef"
            Rectangle { anchors.right: parent.right; width: 10; height: parent.height; color: parent.color }
            Column {
                x: 12
                y: 16
                spacing: 4
                Label { text: "Favoritos"; font.pixelSize: 11; font.bold: true; color: "#8a8a8e"; bottomPadding: 2 }
                Repeater {
                    model: ["Escritorio", "Documentos", "Descargas", "iCloud Drive"]
                    Label {
                        required property string modelData
                        text: modelData
                        font.pixelSize: 13
                        color: "#1d1d1f"
                        leftPadding: 6
                    }
                }
            }
        }

        // Barra superior con la carpeta actual y busqueda.
        Rectangle {
            id: superior
            anchors.left: lateral.right
            anchors.right: parent.right
            height: 44
            color: "#f7f7f8"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#dcdce0" }
            Rectangle {
                anchors.centerIn: parent
                width: Math.max(160, etiquetaCarpeta.implicitWidth + 40)
                height: 24
                radius: 6
                color: "white"
                border.color: "#cfcfd4"
                Label {
                    id: etiquetaCarpeta
                    anchors.centerIn: parent
                    text: raiz.nombreMostrado + "  ⌄"
                    font.pixelSize: 13
                    color: "#1d1d1f"
                }
            }
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 120
                height: 22
                radius: 6
                color: "#ececef"
                Label { x: 8; anchors.verticalCenter: parent.verticalCenter; text: "⌕  Buscar"; font.pixelSize: 12; color: "#8a8a8e" }
            }
        }

        // Solo carpetas (lo que distingue este panel del de e.firma).
        Column {
            id: lista
            anchors.left: lateral.right
            anchors.right: parent.right
            anchors.top: superior.bottom
            anchors.margins: 8

            Repeater {
                model: raiz.carpetas
                delegate: Rectangle {
                    id: fila
                    required property string modelData
                    required property int index
                    readonly property bool marcada: fila.modelData === raiz.seleccionada
                    width: lista.width
                    height: 26
                    radius: 5
                    color: marcada ? "#2f6fd6" : (index % 2 === 1 ? "#f4f5f7" : "white")

                    Row {
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        // Icono de carpeta: pestana y cuerpo.
                        Item {
                            width: 16
                            height: 13
                            anchors.verticalCenter: parent.verticalCenter
                            Rectangle { x: 0; y: 0; width: 7; height: 4; radius: 1; color: fila.marcada ? "white" : "#5aa0f2" }
                            Rectangle { x: 0; y: 2; width: 16; height: 11; radius: 2; color: fila.marcada ? "white" : "#5aa0f2" }
                        }
                        Label {
                            width: 360
                            text: fila.modelData
                            elide: Text.ElideMiddle
                            font.pixelSize: 13
                            color: fila.marcada ? "white" : "#1d1d1f"
                        }
                    }
                }
            }
        }

        // Botones del panel.
        Rectangle {
            anchors.left: lateral.right
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 48
            color: "#f7f7f8"
            Rectangle { width: parent.width; height: 1; color: "#dcdce0" }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Rectangle {
                    width: 84
                    height: 24
                    radius: 6
                    color: "white"
                    border.color: "#cfcfd4"
                    Label { anchors.centerIn: parent; text: "Cancelar"; font.pixelSize: 13; color: "#1d1d1f" }
                }
                Rectangle {
                    width: 84
                    height: 24
                    radius: 6
                    color: raiz.seleccionada.length > 0 ? "#2f6fd6" : "#a9c3ee"
                    Label { anchors.centerIn: parent; text: "Abrir"; font.pixelSize: 13; color: "white" }
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 10
        width: rotulo.implicitWidth + 16
        height: rotulo.implicitHeight + 6
        radius: height / 2
        color: "#1d1d1f"
        Label {
            id: rotulo
            anchors.centerIn: parent
            text: "Ilustración"
            color: "white"
            font.pixelSize: 12
            font.bold: true
        }
    }
}
