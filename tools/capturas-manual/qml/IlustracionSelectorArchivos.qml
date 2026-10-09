pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Ilustracion (T012, D5) del selector de archivos nativo de macOS (panel
// "Abrir"), tema claro. Datos ficticios (D7) por propiedades:
// - carpeta: ruta ficticia; el menu emergente muestra su ultimo componente.
// - archivos: lista de nombres (string) u objetos { nombre, tamano, fecha }.
// - seleccionado: nombre del archivo resaltado (p. ej. "demo.cer").
// Los rotulos ("Cancelar", "Abrir", barra lateral) son de macOS, no de la app.
Item {
    id: raiz

    property string carpeta: ""
    property var archivos: []
    property string seleccionado: ""

    readonly property string nombreCarpeta: {
        const partes = raiz.carpeta.split("/").filter(p => p.length > 0)
        return partes.length > 0 ? partes[partes.length - 1] : raiz.carpeta
    }

    function nombreDe(a) { return typeof a === "string" ? a : (a && a.nombre ? a.nombre : "") }
    function campoDe(a, campo) { return typeof a === "string" ? "" : (a && a[campo] ? String(a[campo]) : "") }
    function extensionDe(nombre) {
        const i = nombre.lastIndexOf(".")
        return i > 0 ? nombre.substring(i + 1).toUpperCase() : ""
    }

    implicitWidth: 760
    implicitHeight: 480
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
                    model: ["Escritorio", "Documentos", "Descargas"]
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
                width: Math.max(160, nombreCarpetaLabel.implicitWidth + 40)
                height: 24
                radius: 6
                color: "white"
                border.color: "#cfcfd4"
                Label {
                    id: nombreCarpetaLabel
                    anchors.centerIn: parent
                    text: raiz.nombreCarpeta + "  ⌄"
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

        // Lista de archivos.
        Column {
            id: lista
            anchors.left: lateral.right
            anchors.right: parent.right
            anchors.top: superior.bottom
            anchors.margins: 8

            Repeater {
                model: raiz.archivos
                delegate: Rectangle {
                    id: fila
                    required property var modelData
                    required property int index
                    readonly property string nombre: raiz.nombreDe(modelData)
                    readonly property bool marcado: nombre.length > 0 && nombre === raiz.seleccionado
                    width: lista.width
                    height: 26
                    radius: 5
                    color: marcado ? "#2f6fd6" : (index % 2 === 1 ? "#f4f5f7" : "white")

                    Row {
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Rectangle {
                            width: 16
                            height: 18
                            radius: 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: "white"
                            border.color: fila.marcado ? "white" : "#9a9aa0"
                            Label {
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: raiz.extensionDe(fila.nombre)
                                font.pixelSize: 6
                                font.bold: true
                                color: "#3a3a3c"
                            }
                        }
                        Label {
                            width: 300
                            text: fila.nombre
                            elide: Text.ElideMiddle
                            font.pixelSize: 13
                            color: fila.marcado ? "white" : "#1d1d1f"
                        }
                        Label {
                            width: 90
                            text: raiz.campoDe(fila.modelData, "tamano")
                            font.pixelSize: 13
                            color: fila.marcado ? "white" : "#6e6e73"
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
                    color: raiz.seleccionado.length > 0 ? "#2f6fd6" : "#a9c3ee"
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
            text: "Ilustracion"
            color: "white"
            font.pixelSize: 12
            font.bold: true
        }
    }
}
