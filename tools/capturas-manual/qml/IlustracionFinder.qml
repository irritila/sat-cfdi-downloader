pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Ilustración (T012, D5) de una ventana del Finder de macOS en vista de lista,
// tema claro. Datos ficticios (D7) por propiedades:
// - carpeta: ruta ficticia (p. ej. "/Users/usuario/Documents/SAT/descargas");
//   el titulo es su ultimo componente y la barra de ruta la muestra completa.
// - archivos: lista de nombres (string) u objetos { nombre, tamano, fecha }.
// - seleccionado: nombre del archivo resaltado.
// Los rotulos de columnas y barra lateral son de macOS, no de la app.
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
    implicitHeight: 460
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#dfe7f1" }
            GradientStop { position: 1.0; color: "#c9d6e6" }
        }
    }

    Rectangle {
        id: ventana
        anchors.fill: parent
        anchors.margins: 24
        anchors.bottomMargin: 40
        radius: 10
        color: "white"
        border.color: "#c6c6cb"
        clip: true

        // Barra lateral.
        Rectangle {
            id: lateral
            width: 170
            height: parent.height
            radius: 10
            color: "#ececef"
            Rectangle { anchors.right: parent.right; width: 10; height: parent.height; color: parent.color }

            Row {
                x: 14
                y: 14
                spacing: 8
                Repeater {
                    model: ["#ff5f57", "#febc2e", "#28c840"]
                    Rectangle {
                        required property string modelData
                        width: 12
                        height: 12
                        radius: 6
                        color: modelData
                    }
                }
            }

            Column {
                x: 12
                y: 48
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

        // Barra de herramientas con el nombre de la carpeta.
        Rectangle {
            id: herramientas
            anchors.left: lateral.right
            anchors.right: parent.right
            height: 44
            color: "#f7f7f8"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#dcdce0" }
            Label {
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                text: "‹  ›    " + raiz.nombreCarpeta
                font.pixelSize: 14
                font.bold: true
                color: "#1d1d1f"
            }
        }

        // Encabezados de columnas.
        Row {
            id: encabezados
            anchors.left: lateral.right
            anchors.top: herramientas.bottom
            anchors.leftMargin: 16
            height: 26
            Label { width: 300; text: "Nombre"; font.pixelSize: 11; color: "#6e6e73"; verticalAlignment: Text.AlignVCenter; height: parent.height }
            Label { width: 150; text: "Fecha de modificacion"; font.pixelSize: 11; color: "#6e6e73"; verticalAlignment: Text.AlignVCenter; height: parent.height }
            Label { width: 80; text: "Tamano"; font.pixelSize: 11; color: "#6e6e73"; verticalAlignment: Text.AlignVCenter; height: parent.height }
        }

        Column {
            anchors.left: lateral.right
            anchors.right: parent.right
            anchors.top: encabezados.bottom
            anchors.leftMargin: 8
            anchors.rightMargin: 8

            Repeater {
                model: raiz.archivos
                delegate: Rectangle {
                    id: fila
                    required property var modelData
                    required property int index
                    readonly property string nombre: raiz.nombreDe(modelData)
                    readonly property bool marcado: nombre.length > 0 && nombre === raiz.seleccionado
                    width: parent.width
                    height: 24
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
                            width: 268
                            text: fila.nombre
                            elide: Text.ElideMiddle
                            font.pixelSize: 13
                            color: fila.marcado ? "white" : "#1d1d1f"
                        }
                        Label {
                            width: 150
                            text: raiz.campoDe(fila.modelData, "fecha")
                            font.pixelSize: 13
                            color: fila.marcado ? "white" : "#6e6e73"
                        }
                        Label {
                            width: 80
                            text: raiz.campoDe(fila.modelData, "tamano")
                            font.pixelSize: 13
                            color: fila.marcado ? "white" : "#6e6e73"
                        }
                    }
                }
            }
        }

        // Barra de ruta.
        Rectangle {
            anchors.left: lateral.right
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 24
            color: "#f7f7f8"
            Rectangle { width: parent.width; height: 1; color: "#dcdce0" }
            Label {
                x: 12
                width: parent.width - 24
                anchors.verticalCenter: parent.verticalCenter
                text: raiz.carpeta.split("/").filter(p => p.length > 0).join("  ›  ")
                elide: Text.ElideLeft
                font.pixelSize: 11
                color: "#6e6e73"
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
