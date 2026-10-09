import QtQuick
import QtQuick.Controls.Basic

// Combo del diseno (T013, control de CampoFormulario): mismo marco que
// CampoTexto (alto 28, Theme.controlFondo, borde, foco, error) con el icono
// chevron-up-down. Modelo, textRole y valueRole como en ComboBox.
ComboBox {
    id: combo

    property bool conError: false

    implicitHeight: Theme.altoControl
    leftPadding: 8
    rightPadding: 28
    font.family: Theme.familia
    font.pixelSize: Theme.cuerpo.size
    opacity: combo.enabled ? 1 : 0.5

    contentItem: Text {
        text: combo.displayText
        font: combo.font
        color: Theme.texto
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Accessible.ignored: true
    }
    indicator: Icono {
        nombre: "chevron-up-down"
        color: Theme.textoSecundario
        tamano: 14
        x: combo.width - width - 8
        y: (combo.height - height) / 2
    }
    background: Rectangle {
        id: fondo
        implicitWidth: 160
        implicitHeight: Theme.altoControl
        radius: Theme.radioControl
        color: combo.down ? Theme.superficieSeccion : Theme.controlFondo
        border.width: combo.conError ? 2 : 1
        border.color: combo.conError ? Theme.error : combo.activeFocus ? Theme.foco : Theme.borde

        AnilloFoco {
            radioBase: fondo.radius
            visible: combo.visualFocus
        }
    }
}
