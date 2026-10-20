pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import SatCfdiDownloader

// Panel de escenarios del prototipo (⌘⇧E). Reinicia los datos sinteticos y
// fija el estado pedido; tambien cambia tema y ancho de ventana.
Popup {
    id: panel

    property var escenarios: []
    property Window ventana: null

    signal escenarioElegido(string clave)

    parent: Overlay.overlay
    x: parent.width - width - Theme.espacioM
    y: Theme.espacioM
    width: 340
    height: Math.min(parent.height - 2 * Theme.espacioM, columna.implicitHeight + 2 * Theme.espacioL)
    modal: false
    focus: true
    padding: Theme.espacioL
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: Theme.superficieElevada
        radius: Theme.radioDialogo
        border.width: 1
        border.color: Theme.borde
    }

    contentItem: Flickable {
        contentHeight: columna.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        ColumnLayout {
            id: columna
            width: parent.width
            spacing: Theme.espacioXs

            Label {
                text: qsTr("Escenarios del prototipo")
                color: Theme.texto
                font.pixelSize: Theme.subtitulo.size
                font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
            }
            Label {
                text: qsTr("Datos sintéticos. Cada escenario reinicia el prototipo.")
                color: Theme.textoSecundario
                font.pixelSize: Theme.leyenda.size
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.bottomMargin: Theme.espacioS
            }
            Repeater {
                model: panel.escenarios
                delegate: BotonAccion {
                    required property var modelData
                    required property int index
                    text: (index + 1) + ". " + modelData.texto
                    variante: "plano"
                    compacto: true
                    Layout.fillWidth: true
                    onClicked: panel.escenarioElegido(modelData.clave)
                }
            }
            RowLayout {
                Layout.topMargin: Theme.espacioS
                spacing: Theme.espacioS
                BotonAccion {
                    text: Theme.oscuro ? qsTr("Tema claro") : qsTr("Tema oscuro")
                    compacto: true
                    onClicked: Theme.oscuro = !Theme.oscuro
                }
                BotonAccion {
                    text: panel.ventana !== null && panel.ventana.width < 760 ? qsTr("Ancho 960") : qsTr("Ancho 720")
                    compacto: true
                    onClicked: {
                        if (panel.ventana === null)
                            return
                        panel.ventana.width = panel.ventana.width < 760 ? 960 : 720
                        panel.ventana.height = 640
                    }
                }
            }
        }
    }
}
