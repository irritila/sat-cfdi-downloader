import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import SatCfdiDownloader

// Notificacion de macOS SIMULADA dentro de la ventana (T014.4 en el nuevo
// flujo): al terminar una descarga avisa con el RFC abreviado y "Ver facturas"
// abre ese perfil y mes.
Rectangle {
    id: aviso

    required property VmPrototipo vm
    readonly property var n: aviso.vm.notificacion

    visible: aviso.n !== null
    width: 340
    implicitHeight: columna.implicitHeight + 2 * Theme.espacioM
    radius: Theme.radioDialogo
    color: Theme.superficieElevada
    border.width: 1
    border.color: Theme.separador
    Accessible.role: Accessible.AlertMessage
    Accessible.name: aviso.n !== null ? aviso.n.titulo : ""

    ColumnLayout {
        id: columna
        anchors.fill: parent
        anchors.margins: Theme.espacioM
        spacing: Theme.espacioXs
        RowLayout {
            spacing: Theme.espacioS
            Icono {
                nombre: "check-circle"
                color: Theme.exito
                tamano: 18
            }
            Label {
                text: qsTr("Archivo Fiscal")
                color: Theme.textoSecundario
                font.pixelSize: Theme.leyenda.size
                Layout.fillWidth: true
            }
            BotonAccion {
                variante: "icono"
                compacto: true
                icono: "xmark"
                nombreAccesible: qsTr("Cerrar notificación")
                onClicked: aviso.vm.notificacion = null
            }
        }
        Label {
            text: aviso.n !== null ? aviso.n.titulo : ""
            color: Theme.texto
            font.pixelSize: Theme.cuerpoFuerte.size
            font.weight: Font.DemiBold
        }
        Label {
            text: aviso.n !== null ? aviso.n.cuerpo : ""
            color: Theme.textoSecundario
            font.pixelSize: Theme.cuerpo.size
        }
        BotonAccion {
            text: qsTr("Ver facturas")
            compacto: true
            Layout.alignment: Qt.AlignRight
            Layout.topMargin: Theme.espacioXs
            onClicked: aviso.vm.abrirMes(aviso.n.rfc, aviso.n.anio, aviso.n.mes)
        }
    }
}
