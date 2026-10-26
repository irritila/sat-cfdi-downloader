import QtQuick
import QtQuick.Controls.Basic

// Combo del diseno (T013, control de CampoFormulario): mismo marco que
// CampoTexto (alto 28, Theme.controlFondo, borde, foco, error) con el icono
// chevron-up-down. Modelo, textRole y valueRole como en ComboBox.
//
// T024.1 M5: lista desplegable y delegado propios, solo con tokens de Theme.
// El delegado Basic pintaba el elemento resaltado con la paleta del sistema
// (texto claro sobre fondo claro en tema claro). Ahora:
// - Fondo de la lista: Theme.superficieElevada con borde Theme.borde.
// - Resaltado (highlightedIndex: lo mueven el teclado y el puntero): fondo
//   Theme.seleccion, texto Theme.texto.
// - Seleccionado (currentIndex): texto Theme.texto en seminegrita con
//   "checkmark" a la izquierda (no depende solo del color).
// Cada delegado pinta su fondo opaco, asi el contraste se mide con los
// colores efectivos (tst_componentes: >= 4.5:1 en claro y oscuro).
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

    delegate: ItemDelegate {
        id: opcion
        required property var model
        required property int index

        readonly property bool seleccionada: combo.currentIndex === opcion.index
        // Colores efectivos (los lee la prueba de contraste).
        readonly property color colorTexto: Theme.texto
        readonly property color colorFondo: opcion.highlighted ? Theme.seleccion : Theme.superficieElevada

        objectName: "opcionCombo_" + opcion.index
        width: ListView.view ? ListView.view.width : combo.width
        implicitHeight: Theme.altoControl
        leftPadding: 6
        rightPadding: 8
        text: opcion.model[combo.textRole]
        font.family: Theme.familia
        font.pixelSize: Theme.cuerpo.size
        font.weight: opcion.seleccionada ? Font.DemiBold : Font.Normal
        highlighted: combo.highlightedIndex === opcion.index
        hoverEnabled: combo.hoverEnabled
        Accessible.name: opcion.text
        Accessible.selected: opcion.seleccionada

        contentItem: Row {
            spacing: 4
            Icono {
                objectName: "marcaSeleccion"
                nombre: opcion.seleccionada ? "checkmark" : ""
                color: opcion.colorTexto
                tamano: 12
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                objectName: "textoOpcion"
                width: opcion.availableWidth - 16
                text: opcion.text
                font: opcion.font
                color: opcion.colorTexto
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                anchors.verticalCenter: parent.verticalCenter
                Accessible.ignored: true
            }
        }
        background: Rectangle {
            objectName: "fondoOpcion"
            color: opcion.colorFondo
            radius: Theme.radioControl
        }
    }

    popup: Popup {
        objectName: "listaCombo"
        y: combo.height + 2
        width: combo.width
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         combo.Window.height - topMargin - bottomMargin)
        topMargin: 6
        bottomMargin: 6
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: combo.delegateModel
            currentIndex: combo.highlightedIndex
            highlightMoveDuration: 0
            ScrollIndicator.vertical: ScrollIndicator { }
        }

        background: Rectangle {
            objectName: "fondoListaCombo"
            color: Theme.superficieElevada
            radius: Theme.radioControl
            border.width: 1
            border.color: Theme.borde
        }
    }
}
