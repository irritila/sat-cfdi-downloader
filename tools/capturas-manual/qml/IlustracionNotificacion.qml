pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Ilustración (T012, D5/D6) de una notificacion de macOS (banner del Centro de
// notificaciones), tema claro. `titulo` y `cuerpo` salen de
// ServicioNotificaciones con un Notificador fake; aqui no hay textos de la app.
Item {
    id: raiz

    property string titulo: ""
    property string cuerpo: ""

    implicitWidth: 560
    implicitHeight: banner.y + banner.height + 52
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#dfe7f1" }
            GradientStop { position: 1.0; color: "#c9d6e6" }
        }
    }

    // Barra de menus vacia (contexto: esquina superior derecha).
    Rectangle {
        id: barra
        width: parent.width
        height: 26
        color: "#f4f4f6"
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#c8c8cc" }
    }

    Rectangle {
        id: banner
        anchors.right: parent.right
        anchors.rightMargin: 14
        y: barra.height + 12
        width: 360
        height: Math.max(64, contenido.implicitHeight + 24)
        radius: 14
        color: "#f2f2f4"
        border.color: "#d4d4d8"
        border.width: 1

        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 4
            anchors.bottomMargin: -6
            radius: 16
            color: "#26000000"
        }

        // Icono generico de la app.
        Rectangle {
            id: icono
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 36
            height: 36
            radius: 8
            color: "#2f6fd6"
            Rectangle {
                anchors.centerIn: parent
                width: 18
                height: 22
                radius: 2
                color: "white"
                Rectangle { x: 4; y: 6; width: 10; height: 2; color: "#2f6fd6" }
                Rectangle { x: 4; y: 11; width: 10; height: 2; color: "#2f6fd6" }
                Rectangle { x: 4; y: 16; width: 6; height: 2; color: "#2f6fd6" }
            }
        }

        Column {
            id: contenido
            anchors.left: icono.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Label {
                width: parent.width
                text: raiz.titulo
                font.pixelSize: 13
                font.bold: true
                color: "#1d1d1f"
                elide: Text.ElideRight
            }
            Label {
                width: parent.width
                text: raiz.cuerpo
                visible: text.length > 0
                font.pixelSize: 13
                color: "#3a3a3c"
                wrapMode: Text.WordWrap
                maximumLineCount: 4
                elide: Text.ElideRight
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
