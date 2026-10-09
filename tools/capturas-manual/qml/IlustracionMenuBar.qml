pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import SatCfdiDownloader

// Ilustración (T012, D5/D6) del menu de la app en la barra de menus de macOS,
// tema claro. No contiene textos de la app: las opciones llegan en `entradas`
// desde la definicion compartida del menu bar.
//
// entradas: [{ texto: string, habilitada: bool, separador: bool, marcada: bool,
//              icono: string }]
// - icono (UX-38): nombre del SVG de la app (assets/icons, sin extension);
//   vacio = sin icono. Si alguna entrada lo tiene, los textos se alinean en
//   una columna despues de la de iconos, como en macOS.
// - separador: dibuja una linea y omite el resto de campos.
// - habilitada (por omision true): deshabilitada se dibuja en gris.
// - marcada (por omision false): muestra la marca de verificacion.
Item {
    id: raiz

    property var entradas: []

    readonly property int anchoMenu: 320
    readonly property int altoEntrada: 24
    readonly property int altoSeparador: 11
    readonly property bool hayIconos: {
        for (let i = 0; i < raiz.entradas.length; ++i) {
            const e = raiz.entradas[i]
            if (e && e.separador !== true && e.icono && e.icono.length > 0)
                return true
        }
        return false
    }

    implicitWidth: 560
    implicitHeight: barra.height + 12 + menu.height + 40
    width: implicitWidth
    height: implicitHeight

    // Fondo de escritorio neutro.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#dfe7f1" }
            GradientStop { position: 1.0; color: "#c9d6e6" }
        }
    }

    // Barra de menus (solo la zona derecha, con iconos genericos).
    Rectangle {
        id: barra
        width: parent.width
        height: 26
        color: "#f4f4f6"
        opacity: 0.96
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#c8c8cc" }

        Row {
            id: filaIconos
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 16

            // Icono de la app resaltado (menu abierto).
            Rectangle {
                id: iconoApp
                width: 26
                height: 20
                radius: 5
                color: "#d9d9de"
                Rectangle {
                    anchors.centerIn: parent
                    width: 14
                    height: 12
                    radius: 2
                    color: "transparent"
                    border.color: "#1d1d1f"
                    border.width: 1.5
                    Rectangle { x: 3; y: 3; width: 8; height: 1.5; color: "#1d1d1f" }
                    Rectangle { x: 3; y: 6.5; width: 6; height: 1.5; color: "#1d1d1f" }
                }
            }
            // Iconos genericos del sistema.
            Repeater {
                model: 3
                Rectangle {
                    required property int index
                    width: 16
                    height: 12
                    radius: index === 1 ? 6 : 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: "transparent"
                    border.color: "#3a3a3c"
                    border.width: 1.5
                }
            }
        }
    }

    // Menu desplegado bajo el icono de la app.
    Rectangle {
        id: menu
        // Alineado con el icono de la app (la barra esta en el origen).
        x: Math.max(8, Math.min(raiz.width - raiz.anchoMenu - 8, filaIconos.x + iconoApp.x))
        y: barra.height + 4
        width: raiz.anchoMenu
        height: columna.implicitHeight + 10
        radius: 8
        color: "#f6f6f7"
        border.color: "#cfcfd4"
        border.width: 1

        // Sombra simple.
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 4
            anchors.leftMargin: -2
            anchors.rightMargin: -2
            anchors.bottomMargin: -6
            radius: 10
            color: "#22000000"
        }

        Column {
            id: columna
            x: 5
            y: 5
            width: parent.width - 10

            Repeater {
                model: raiz.entradas
                delegate: Item {
                    id: entrada
                    required property var modelData
                    readonly property bool esSeparador: modelData.separador === true
                    readonly property bool habilitada: modelData.habilitada !== false
                    width: columna.width
                    height: esSeparador ? raiz.altoSeparador : raiz.altoEntrada

                    Rectangle {
                        visible: entrada.esSeparador
                        anchors.verticalCenter: parent.verticalCenter
                        x: 9
                        width: parent.width - 18
                        height: 1
                        color: "#d6d6da"
                    }
                    Label {
                        visible: !entrada.esSeparador && entrada.modelData.marcada === true
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: "✓"
                        font.pixelSize: 13
                        color: entrada.habilitada ? "#1d1d1f" : "#a1a1a6"
                    }
                    Icono {
                        visible: !entrada.esSeparador && nombre.length > 0
                        x: 26
                        anchors.verticalCenter: parent.verticalCenter
                        nombre: entrada.esSeparador ? "" : (entrada.modelData.icono || "")
                        color: entrada.habilitada ? "#1d1d1f" : "#a1a1a6"
                        tamano: 14
                    }
                    Label {
                        visible: !entrada.esSeparador
                        x: raiz.hayIconos ? 48 : 26
                        width: parent.width - x - 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: entrada.esSeparador ? "" : (entrada.modelData.texto || "")
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        color: entrada.habilitada ? "#1d1d1f" : "#a1a1a6"
                    }
                }
            }
        }
    }

    // Rotulo obligatorio.
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
