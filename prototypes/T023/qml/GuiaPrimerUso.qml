pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import SatCfdiDownloader

// Sin perfiles: guia de primer uso de T014.3 con el paso 3 del nuevo flujo
// ("Descarga las facturas de un mes"). Cada paso lleva su accion.
Item {
    id: guia

    required property VmPrototipo vm

    readonly property var pasos: [
        { texto: qsTr("Crea un perfil SAT con el RFC del contribuyente"), accion: qsTr("Perfiles SAT") },
        { texto: qsTr("Registra la e.firma del perfil"), accion: qsTr("Registrar e.firma…") },
        { texto: qsTr("Descarga las facturas de un mes"), accion: qsTr("Ver facturas") }
    ]

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(guia.width - 2 * Theme.espacioXl, 460)
        spacing: Theme.espacioL

        EstadoVacio {
            variante: "sinPerfiles"
            titulo: qsTr("Empieza con un perfil fiscal")
            descripcion: qsTr("Archivo Fiscal guarda en este equipo las facturas emitidas y recibidas de cada RFC, mes por mes.")
            Layout.fillWidth: true
        }

        Rectangle {
            color: Theme.superficie
            radius: Theme.radioTarjeta
            border.width: 1
            border.color: Theme.separador
            Layout.fillWidth: true
            implicitHeight: columnaPasos.implicitHeight + 2 * Theme.espacioM

            // Padding espacioM vertical / espacioL horizontal, como las filas.
            ColumnLayout {
                id: columnaPasos
                anchors.fill: parent
                anchors.topMargin: Theme.espacioM
                anchors.bottomMargin: Theme.espacioM
                anchors.leftMargin: Theme.espacioL
                anchors.rightMargin: Theme.espacioL
                spacing: Theme.espacioS
                Repeater {
                    model: guia.pasos
                    delegate: RowLayout {
                        id: paso
                        required property var modelData
                        required property int index
                        spacing: Theme.espacioM
                        Layout.fillWidth: true
                        Accessible.role: Accessible.ListItem
                        Accessible.name: qsTr("Paso %1 de 3: %2").arg(paso.index + 1).arg(paso.modelData.texto)
                        // Numero de paso: circulo del alto de badge.
                        Rectangle {
                            implicitWidth: Theme.altoBadge
                            implicitHeight: Theme.altoBadge
                            radius: Theme.altoBadge / 2
                            color: paso.index === 0 ? Theme.acento : Theme.controlSecundario
                            Label {
                                anchors.centerIn: parent
                                text: paso.index + 1
                                color: paso.index === 0 ? Theme.textoSobreAcento : Theme.texto
                                font.pixelSize: Theme.etiqueta.size
                                font.weight: Font.DemiBold
                            }
                        }
                        Label {
                            text: paso.modelData.texto
                            color: Theme.texto
                            font.pixelSize: Theme.cuerpo.size
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        BotonAccion {
                            text: paso.modelData.accion
                            compacto: true
                            variante: paso.index === 0 ? "primario" : "secundario"
                            enabled: paso.index === 0
                            onClicked: guia.vm.irA("perfiles")
                        }
                    }
                }
            }
        }
    }
}
