pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import SatCfdiDownloader

// Lista lateral de meses (D9): selector de anio y los 12 meses con estado
// (icono y texto, no solo color) y numero de facturas. ⌘↑/⌘↓ cambian de mes
// desde cualquier lugar; dentro de la lista tambien las flechas.
Rectangle {
    id: panel

    required property VmPrototipo vm

    color: Theme.superficieSeccion

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.separador
    }

    ColumnLayout {
        anchors.fill: parent
        // Mismo margen superior (espacioL) que la columna de contenido, para
        // que el selector de anio y la franja arranquen a la misma altura.
        anchors.topMargin: Theme.espacioL
        anchors.bottomMargin: Theme.espacioS
        spacing: Theme.espacioS

        // Padding horizontal espacioL, igual que las filas: los chevrones
        // quedan alineados con los nombres de mes y con los conteos.
        RowLayout {
            Layout.leftMargin: Theme.espacioL
            Layout.rightMargin: Theme.espacioL
            Layout.fillWidth: true
            BotonAccion {
                variante: "icono"
                compacto: true
                icono: "chevron-left"
                nombreAccesible: qsTr("Año anterior")
                enabled: panel.vm.anio > 2025
                onClicked: panel.vm.cambiarAnio(-1)
            }
            Label {
                text: panel.vm.anio
                color: Theme.texto
                font.pixelSize: Theme.subtitulo.size
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: qsTr("Año %1").arg(panel.vm.anio)
            }
            BotonAccion {
                variante: "icono"
                compacto: true
                icono: "chevron-right"
                nombreAccesible: qsTr("Año siguiente")
                enabled: panel.vm.anio < 2026
                onClicked: panel.vm.cambiarAnio(1)
            }
        }

        ListView {
            id: meses
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.vm.mesesDelAnio
            currentIndex: panel.vm.mes - 1
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Meses de %1").arg(panel.vm.anio)
            Keys.onUpPressed: panel.vm.moverMes(-1)
            Keys.onDownPressed: panel.vm.moverMes(1)
            // Con filas mas altas la lista puede desplazarse a 640 de alto:
            // el mes elegido se mantiene visible.
            onCurrentIndexChanged: Qt.callLater(() => meses.positionViewAtIndex(meses.currentIndex, ListView.Contain))
            Component.onCompleted: meses.positionViewAtIndex(meses.currentIndex, ListView.Contain)

            delegate: ItemDelegate {
                id: mes
                required property var modelData
                required property int index
                readonly property bool actual: panel.vm.mes === mes.modelData.mes
                readonly property var colores: Theme.tono(mes.modelData.tono)
                // Alto derivado del contenido (dos lineas) mas padding
                // vertical espacioS; antes era 42 fijo y la linea de estado
                // quedaba pegada a la fila siguiente.
                width: meses.width
                implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
                enabled: !mes.modelData.futuro
                focusPolicy: Qt.NoFocus
                leftPadding: Theme.espacioL
                rightPadding: Theme.espacioL
                topPadding: Theme.espacioS
                bottomPadding: Theme.espacioS
                Accessible.role: Accessible.ListItem
                Accessible.name: mes.modelData.nombreAccesible
                Accessible.selected: mes.actual
                onClicked: panel.vm.seleccionarMes(panel.vm.anio, mes.modelData.mes)

                background: Rectangle {
                    color: mes.actual ? Theme.seleccion : mes.hovered ? Theme.superficie : "transparent"
                    AnilloFoco {
                        interior: true
                        visible: meses.activeFocus && mes.actual
                    }
                }
                // Linea 1: mes y numero de facturas; linea 2: estado a todo el
                // ancho (asi "Disponibles parcialmente" no se recorta).
                contentItem: ColumnLayout {
                    spacing: Theme.espacioXs / 2
                    opacity: mes.modelData.futuro ? 0.55 : 1
                    RowLayout {
                        spacing: Theme.espacioS
                        Layout.fillWidth: true
                        Label {
                            text: mes.modelData.nombre
                            color: Theme.texto
                            font.pixelSize: Theme.cuerpo.size
                            font.weight: mes.actual ? Font.DemiBold : Font.Normal
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: !mes.modelData.futuro && mes.modelData.total > 0
                            text: mes.modelData.total
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.leyenda.size
                            font.features: { "tnum": 1 }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.espacioXs
                        Icono {
                            nombre: mes.modelData.icono
                            color: mes.colores.texto
                            tamano: 12
                        }
                        Label {
                            text: mes.modelData.estadoTexto
                            color: mes.modelData.tono === "neutro" ? Theme.textoSecundario : mes.colores.texto
                            font.pixelSize: Theme.leyenda.size
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }
}
