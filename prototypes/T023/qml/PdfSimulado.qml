pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import SatCfdiDownloader

// Vista previa del PDF SIMULADA. VisorPdfCfdi real requiere
// VisorPdfCfdiViewModel (C++) y el renderizador de PDF sobre XML real; aqui
// se dibuja una hoja con los datos sinteticos del CFDI. Escape cierra.
Popup {
    id: visor

    required property VmPrototipo vm
    readonly property var c: visor.vm.pdf

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 2 * Theme.espacioXl, 620)
    height: parent.height - 2 * Theme.espacioXl
    modal: true
    focus: true
    padding: 0
    visible: visor.c !== null
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onClosed: visor.vm.pdf = null

    Overlay.modal: Rectangle { color: Theme.velo }

    background: Rectangle {
        color: Theme.superficieElevada
        radius: Theme.radioDialogo
        border.width: 1
        border.color: Theme.separador
    }

    contentItem: ColumnLayout {
        spacing: 0
        // Cabecera: espacioM vertical, espacioL horizontal (margen de pagina).
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.espacioL
            Layout.rightMargin: Theme.espacioL
            Layout.topMargin: Theme.espacioM
            Layout.bottomMargin: Theme.espacioM
            spacing: Theme.espacioS
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Label {
                    text: qsTr("Vista previa del PDF")
                    color: Theme.texto
                    font.pixelSize: Theme.subtitulo.size
                    font.weight: Font.DemiBold
                    Accessible.role: Accessible.Heading
                }
                Label {
                    text: visor.c !== null ? visor.c.tipoTexto + " " + visor.c.serieFolio + " · " + visor.c.uuid : ""
                    color: Theme.textoSecundario
                    font.pixelSize: Theme.leyenda.size
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
            BotonAccion {
                text: qsTr("Guardar PDF…")
                icono: "arrow-down-circle"
                onClicked: visor.close()
            }
            BotonAccion {
                text: qsTr("Cerrar")
                variante: "primario"
                onClicked: visor.close()
            }
        }
        // Area de la hoja: esquinas inferiores con el radio del dialogo
        // (antes eran rectas y sobresalian del borde redondeado).
        Rectangle {
            id: areaHoja
            color: Theme.superficieSeccion
            radius: Theme.radioDialogo - 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 1
            Layout.rightMargin: 1
            Layout.bottomMargin: 1
            clip: true

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: areaHoja.radius
                color: Theme.superficieSeccion
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.separador
            }

            // Hoja carta (siempre blanca, como el PDF real).
            Rectangle {
                id: hoja
                anchors.horizontalCenter: parent.horizontalCenter
                y: Theme.espacioL
                // Margen espacioL arriba y abajo: la hoja ya no toca el borde.
                width: Math.min(parent.width - 2 * Theme.espacioL, (parent.height - 2 * Theme.espacioL) / 1.29)
                height: hoja.width * 1.29
                color: "#ffffff"
                border.color: "#d0d0d6"
                readonly property real u: hoja.width / 100

                Column {
                    anchors.fill: parent
                    anchors.margins: hoja.u * 6
                    spacing: hoja.u * 2.2
                    visible: visor.c !== null
                    component T: Text {
                        color: "#1d1d1f"
                        font.pixelSize: Math.max(7, hoja.u * 2.2)
                        width: parent.width
                        elide: Text.ElideRight
                    }
                    T { text: visor.c ? visor.c.emisorNombre : ""; font.pixelSize: hoja.u * 3.6; font.bold: true }
                    T { text: visor.c ? "RFC " + visor.c.emisorRfc + " · Régimen 601" : "" }
                    T { text: visor.c ? "Factura " + visor.c.serieFolio + " · " + visor.c.tipoTexto + " · " + visor.c.fecha : ""; font.bold: true }
                    Rectangle { width: parent.width; height: 1; color: "#c8c8ce" }
                    T { text: "Receptor"; color: "#5c5c63" }
                    T { text: visor.c ? visor.c.receptorNombre + " · " + visor.c.receptorRfc : "" }
                    Rectangle { width: parent.width; height: 1; color: "#c8c8ce" }
                    Row {
                        width: parent.width
                        T { width: parent.width * 0.6; text: "Concepto"; color: "#5c5c63" }
                        T { width: parent.width * 0.4; text: "Importe"; color: "#5c5c63"; horizontalAlignment: Text.AlignRight }
                    }
                    Row {
                        width: parent.width
                        T { width: parent.width * 0.6; text: visor.c ? visor.c.concepto : "" }
                        T { width: parent.width * 0.4; text: visor.c ? visor.c.total : ""; horizontalAlignment: Text.AlignRight }
                    }
                    Rectangle { width: parent.width; height: 1; color: "#c8c8ce" }
                    T { text: visor.c ? "Total " + visor.c.total + " " + visor.c.moneda : ""; font.bold: true; horizontalAlignment: Text.AlignRight }
                    T { text: visor.c ? "Método de pago " + (visor.c.metodoPago || "—") : "" }
                    Item { width: 1; height: hoja.u * 4 }
                    Row {
                        spacing: hoja.u * 3
                        Grid {
                            columns: 9
                            Repeater {
                                model: 81
                                Rectangle {
                                    required property int index
                                    width: hoja.u * 1.8
                                    height: hoja.u * 1.8
                                    color: (visor.c && visor.c.uuid.charCodeAt(index % 36) % 3 === 0) || index % 10 === 0 ? "#1d1d1f" : "#ffffff"
                                }
                            }
                        }
                        Column {
                            spacing: hoja.u
                            T { width: hoja.width * 0.6; text: "Folio fiscal (UUID)"; color: "#5c5c63" }
                            T { width: hoja.width * 0.6; text: visor.c ? visor.c.uuid : ""; font.family: Theme.familiaMono }
                            T { width: hoja.width * 0.6; text: "Este documento es una representación impresa de un CFDI." }
                        }
                    }
                }
                Text {
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.margins: hoja.u * 3
                    text: "PDF simulado (prototipo)"
                    color: "#9a9aa2"
                    font.pixelSize: Math.max(7, hoja.u * 2)
                }
            }
        }
    }
}
