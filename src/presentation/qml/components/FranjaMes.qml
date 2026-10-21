import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Franja de disponibilidad del mes en Facturas (T023.4, D7, D11, D17; forma
// de ResumenEstado segun el prototipo ajustado de T023.1). Solo representa
// FranjaMesViewModel: estado, textos, accion y motivo vienen de C++.
// - EstadoBadge grande (icono y texto: el estado no depende solo del color),
//   titular y descripcion; causa (Requiere atencion) si no repite la
//   descripcion; etapa con progreso indeterminado y nota mientras se prepara;
//   aviso puntual.
// - UNA accion primaria (Cmd+R en Main.qml): `accionSolicitada`. Deshabilitada
//   muestra su motivo debajo y como descripcion accesible.
// - Enlace "Ver detalles tecnicos" (BotonAccion "enlace", alineado con el
//   texto): `enlaceSolicitado`.
// El anuncio del cambio de estado lo hace la pagina con FacturasViewModel.anuncio.
Rectangle {
    id: franja
    objectName: "franjaMes"

    required property FranjaMesViewModel vm

    readonly property alias botonAccion: accion
    readonly property alias enlace: enlace
    readonly property bool conAccion: franja.vm.accion !== FranjaMesViewModel.Ninguna
                                      && franja.vm.textoAccion.length > 0

    signal accionSolicitada()
    signal enlaceSolicitado()

    function tonoTheme(tono) {
        switch (tono) {
        case "info": return "progreso"
        case "aviso": return "advertencia"
        default: return tono.length > 0 ? tono : "neutro"
        }
    }

    implicitHeight: fila.implicitHeight + 2 * Theme.espacioL
    radius: Theme.radioTarjeta
    color: Theme.superficie
    border.width: 1
    border.color: Theme.separador
    Accessible.role: Accessible.Grouping
    Accessible.name: franja.vm.nombreAccesible

    RowLayout {
        id: fila
        anchors.fill: parent
        anchors.margins: Theme.espacioL
        spacing: Theme.espacioL

        ColumnLayout {
            spacing: Theme.espacioS
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop

            RowLayout {
                spacing: Theme.espacioM
                Layout.fillWidth: true
                EstadoBadge {
                    objectName: "badgeFranja"
                    visible: franja.vm.estadoTexto.length > 0
                    grande: true
                    texto: franja.vm.estadoTexto
                    tonoDirecto: franja.tonoTheme(franja.vm.tono)
                    iconoDirecto: franja.vm.icono
                    contexto: qsTr("Estado del mes")
                    Layout.alignment: Qt.AlignTop
                }
                Label {
                    objectName: "titularFranja"
                    text: franja.vm.titular
                    color: Theme.texto
                    font.family: Theme.familia
                    font.pixelSize: Theme.subtitulo.size
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
            }
            Label {
                objectName: "descripcionFranja"
                // Preparando: la etapa ya se muestra con su progreso.
                visible: text.length > 0 && !(franja.vm.ocupado && text === franja.vm.etapa)
                text: franja.vm.descripcion
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.cuerpo.size
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Accessible.ignored: true
            }
            Label {
                objectName: "causaFranja"
                visible: text.length > 0 && franja.vm.descripcion.indexOf(text) < 0
                text: franja.vm.causa
                color: Theme.texto
                font.family: Theme.familia
                font.pixelSize: Theme.cuerpo.size
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Accessible.ignored: true
            }
            // Preparando: etapa en lenguaje de usuario con progreso indeterminado.
            RowLayout {
                objectName: "etapaFranja"
                visible: franja.vm.ocupado
                spacing: Theme.espacioS
                Layout.fillWidth: true
                // 18: tamano de icono de avisos (README, Iconografia).
                BusyIndicator {
                    running: parent.visible
                    implicitWidth: 18
                    implicitHeight: 18
                    Accessible.ignored: true
                }
                Label {
                    text: franja.vm.etapa
                    color: Theme.texto
                    font.family: Theme.familia
                    font.pixelSize: Theme.cuerpo.size
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
            Label {
                objectName: "notaFranja"
                visible: text.length > 0
                text: franja.vm.nota
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                Layout.fillWidth: true
            }
            AvisoEnLinea {
                objectName: "avisoFranja"
                visible: franja.vm.aviso.length > 0
                variante: "advertencia"
                titulo: franja.vm.aviso
                Layout.fillWidth: true
            }
            BotonAccion {
                id: enlace
                objectName: "enlaceDetallesTecnicos"
                visible: franja.vm.textoEnlace.length > 0
                variante: "enlace"
                compacto: true
                text: franja.vm.textoEnlace
                onClicked: franja.enlaceSolicitado()
            }
        }

        ColumnLayout {
            spacing: Theme.espacioXs
            visible: franja.conAccion
            Layout.alignment: Qt.AlignTop | Qt.AlignRight
            Layout.maximumWidth: 240
            BotonAccion {
                id: accion
                objectName: "botonAccionFranja"
                text: franja.vm.textoAccion
                variante: "primario"
                atajo: "⌘R"
                cargando: franja.vm.ejecutando
                enabled: franja.vm.accionHabilitada && !franja.vm.ejecutando
                descripcion: franja.vm.accionHabilitada ? "" : franja.vm.motivoDeshabilitada
                Layout.alignment: Qt.AlignRight
                onClicked: franja.accionSolicitada()
            }
            Label {
                objectName: "motivoFranja"
                visible: !franja.vm.accionHabilitada && text.length > 0
                text: franja.vm.motivoDeshabilitada
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignRight
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Layout.preferredWidth: 200
            }
        }
    }
}
