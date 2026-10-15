pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Ilustración (T016, T014.4 D2) de las cuatro variantes del icono de la app en
// la barra de menus de macOS, tema claro. No contiene textos de la app: cada
// variante llega en `variantes` con el dibujo real (IconoMenuBar) y el texto
// real al pasar el cursor (textoEstadoIcono).
//
// variantes: [{ imagen: url (18 pt a 2x), ampliada: url (48 pt a 2x),
//               texto: string }]
// Cada fila muestra el icono ampliado (para distinguir la insignia), una barra
// de menus con el icono a su tamano real y el texto que macOS muestra al pasar
// el cursor.
Item {
    id: raiz

    property var variantes: []

    readonly property int anchoBarra: 400
    readonly property int altoFila: 64

    implicitWidth: columna.implicitWidth + 2 * 28
    implicitHeight: columna.implicitHeight + 28 + 56
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

    Column {
        id: columna
        x: 28
        y: 28
        spacing: 18

        Repeater {
            model: raiz.variantes
            delegate: Row {
                id: fila
                required property var modelData
                spacing: 20

                // Icono ampliado.
                Rectangle {
                    width: raiz.altoFila
                    height: raiz.altoFila
                    radius: 12
                    color: "#f6f6f7"
                    border.color: "#cfcfd4"
                    border.width: 1
                    Image {
                        anchors.centerIn: parent
                        width: 48
                        height: 48
                        source: fila.modelData.ampliada
                        smooth: true
                        opacity: 0.85
                    }
                }

                // Barra de menus con el icono a tamano real y el texto al
                // pasar el cursor.
                Item {
                    width: raiz.anchoBarra
                    height: raiz.altoFila

                    Rectangle {
                        id: barra
                        width: parent.width
                        height: 26
                        color: "#f4f4f6"
                        border.color: "#c8c8cc"
                        border.width: 1

                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 16

                            Item {
                                id: icono
                                width: 26
                                height: 20
                                Image {
                                    anchors.centerIn: parent
                                    width: 18
                                    height: 18
                                    source: fila.modelData.imagen
                                    smooth: true
                                    opacity: 0.85
                                }
                            }
                            // Iconos genericos del sistema.
                            Repeater {
                                model: 2
                                Rectangle {
                                    required property int index
                                    width: 16
                                    height: 12
                                    radius: index === 0 ? 6 : 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: "transparent"
                                    border.color: "#3a3a3c"
                                    border.width: 1.5
                                }
                            }
                        }
                    }

                    // Texto al pasar el cursor (tooltip de macOS).
                    Rectangle {
                        anchors.right: parent.right
                        anchors.rightMargin: 40
                        y: barra.height + 8
                        width: textoTooltip.implicitWidth + 16
                        height: textoTooltip.implicitHeight + 8
                        radius: 4
                        color: "#fbfbfc"
                        border.color: "#c8c8cc"
                        border.width: 1
                        Label {
                            id: textoTooltip
                            anchors.centerIn: parent
                            text: fila.modelData.texto
                            font.pixelSize: 12
                            color: "#1d1d1f"
                        }
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
