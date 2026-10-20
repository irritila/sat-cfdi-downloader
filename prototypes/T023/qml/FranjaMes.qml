import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import SatCfdiDownloader

// Franja de estado del mes (forma de ResumenEstado): badge de los cinco
// estados de mes (D8) con icono y texto, titular, descripcion y UNA accion
// (Descargar facturas, Actualizar mes, Completar mes o Reintentar). En
// Requiere atencion agrega el enlace "Ver detalles técnicos" (Diagnostico).
// ResumenEstado no se reutiliza tal cual porque su badge solo acepta claves
// de solicitud; se componen EstadoBadge (tonoDirecto) y BotonAccion reales.
Rectangle {
    id: franja

    required property VmPrototipo vm
    readonly property var f: franja.vm.franja
    readonly property var e: franja.vm.estadoMesActual

    implicitHeight: fila.implicitHeight + 2 * Theme.espacioL
    radius: Theme.radioTarjeta
    color: Theme.superficie
    border.width: 1
    border.color: Theme.separador
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Estado de %1").arg(franja.vm.tituloMes)

    // Anuncio una vez por cambio de estado (accesibilidad).
    onEChanged: franja.Accessible.announce(franja.e.texto + ". " + franja.f.titular)

    RowLayout {
        id: fila
        anchors.fill: parent
        anchors.margins: Theme.espacioL
        spacing: Theme.espacioL

        ColumnLayout {
            spacing: Theme.espacioS
            Layout.fillWidth: true
            RowLayout {
                spacing: Theme.espacioM
                Layout.fillWidth: true
                EstadoBadge {
                    grande: true
                    texto: franja.e.texto
                    tonoDirecto: franja.e.tono
                    iconoDirecto: franja.e.icono
                    contexto: qsTr("Estado del mes")
                    Layout.alignment: Qt.AlignTop
                }
                Label {
                    text: franja.f.titular
                    color: Theme.texto
                    font.pixelSize: Theme.subtitulo.size
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                // 18: tamano de icono de avisos (README, Iconografia).
                BusyIndicator {
                    visible: franja.f.ocupado
                    running: franja.f.ocupado
                    implicitWidth: 18
                    implicitHeight: 18
                    Accessible.ignored: true
                }
            }
            Label {
                visible: text.length > 0
                text: franja.f.descripcion
                color: Theme.textoSecundario
                font.pixelSize: Theme.cuerpo.size
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            // BotonAccion plano trae padding espacioS: se compensa para que
            // el icono del enlace quede alineado con el texto de la franja.
            BotonAccion {
                visible: franja.f.enlace.length > 0
                variante: "plano"
                compacto: true
                Layout.leftMargin: -Theme.espacioS
                text: franja.f.enlace
                icono: "chevron-right"
                onClicked: franja.vm.abrirDiagnostico({ rfc: franja.vm.perfil.rfc, anio: franja.vm.anio, mes: franja.vm.mes })
            }
        }

        ColumnLayout {
            spacing: Theme.espacioXs
            visible: franja.f.textoAccion.length > 0
            Layout.alignment: Qt.AlignTop | Qt.AlignRight
            Layout.maximumWidth: 240
            BotonAccion {
                text: franja.f.textoAccion
                variante: "primario"
                atajo: "⌘R"
                enabled: franja.f.habilitada
                Layout.alignment: Qt.AlignRight
                onClicked: franja.vm.accionFranja()
            }
            Label {
                visible: !franja.f.habilitada && franja.f.motivo.length > 0
                text: franja.f.motivo
                color: Theme.textoSecundario
                font.pixelSize: Theme.leyenda.size
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
                Layout.preferredWidth: 200
            }
        }
    }
}
