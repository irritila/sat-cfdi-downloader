pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Ilustración (T012, D5/D6; T016) de notificaciones de macOS (banners del
// Centro de notificaciones), tema claro. Los textos de la app llegan en
// `notificaciones`: titulo y cuerpo de ServicioNotificaciones con un
// Notificador fake; acciones con los textos reales de las acciones de
// notificacion (T014.4 D1).
//
// notificaciones: [{ titulo: string, cuerpo: string, acciones: [string] }]
// - acciones no vacias: el banner muestra el boton "Opciones" de macOS con su
//   menu desplegado (asi aparecen las acciones al pasar el cursor).
Item {
    id: raiz

    property var notificaciones: []

    readonly property int anchoBanner: 480
    readonly property int altoMenuAccion: 26

    implicitWidth: 680
    implicitHeight: columna.y + columna.implicitHeight + 52
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

    Column {
        id: columna
        anchors.right: parent.right
        anchors.rightMargin: 14
        y: barra.height + 12
        spacing: 16

        Repeater {
            model: raiz.notificaciones
            delegate: Item {
                id: notificacion
                required property var modelData
                readonly property var acciones: modelData.acciones || []
                readonly property bool conAcciones: acciones.length > 0

                width: raiz.anchoBanner
                height: banner.height + (conAcciones ? menuAcciones.height + 6 : 0)

                Rectangle {
                    id: banner
                    width: parent.width
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
                        anchors.right: opciones.visible ? opciones.left : parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        Label {
                            width: parent.width
                            text: notificacion.modelData.titulo
                            font.pixelSize: 13
                            font.bold: true
                            color: "#1d1d1f"
                            elide: Text.ElideRight
                        }
                        Label {
                            width: parent.width
                            text: notificacion.modelData.cuerpo
                            visible: text.length > 0
                            font.pixelSize: 13
                            color: "#3a3a3c"
                            wrapMode: Text.WordWrap
                            maximumLineCount: 4
                            elide: Text.ElideRight
                        }
                    }

                    // Boton "Opciones" de macOS (visible al pasar el cursor).
                    Rectangle {
                        id: opciones
                        visible: notificacion.conAcciones
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: textoOpciones.implicitWidth + 30
                        height: 24
                        radius: 6
                        color: "#e2e2e6"
                        border.color: "#c8c8cc"
                        border.width: 1
                        Label {
                            id: textoOpciones
                            x: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Opciones"
                            font.pixelSize: 12
                            color: "#1d1d1f"
                        }
                        Label {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: "⌄"
                            font.pixelSize: 11
                            color: "#1d1d1f"
                        }
                    }
                }

                // Menu desplegado de "Opciones" con las acciones de la app.
                Rectangle {
                    id: menuAcciones
                    visible: notificacion.conAcciones
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    y: banner.height + 6
                    width: Math.max(160, anchoAcciones.implicitWidth + 36)
                    height: listaAcciones.implicitHeight + 10
                    radius: 8
                    color: "#f6f6f7"
                    border.color: "#cfcfd4"
                    border.width: 1

                    Rectangle {
                        z: -1
                        anchors.fill: parent
                        anchors.topMargin: 3
                        anchors.bottomMargin: -5
                        radius: 10
                        color: "#22000000"
                    }

                    // Ancho del texto mas largo (solo medida).
                    Label {
                        id: anchoAcciones
                        visible: false
                        font.pixelSize: 13
                        text: notificacion.acciones.reduce((a, b) => a.length >= b.length ? a : b, "")
                    }

                    Column {
                        id: listaAcciones
                        x: 5
                        y: 5
                        width: parent.width - 10
                        Repeater {
                            model: notificacion.acciones
                            delegate: Rectangle {
                                id: accion
                                required property string modelData
                                required property int index
                                width: listaAcciones.width
                                height: raiz.altoMenuAccion
                                radius: 5
                                // La primera accion aparece resaltada (cursor encima).
                                color: index === 0 ? "#2f6fd6" : "transparent"
                                Label {
                                    x: 13
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: accion.modelData
                                    font.pixelSize: 13
                                    color: accion.index === 0 ? "white" : "#1d1d1f"
                                }
                            }
                        }
                    }
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
