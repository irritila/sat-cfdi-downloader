import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Dialogo modal de confirmacion accesible por teclado. Foco inicial en
// "Cancelar" (opcion segura); Tab alterna; Enter/Espacio activa; Escape cancela.
// Al cerrarse, Popup restaura el foco de la ventana despues de emitir sus
// senales: quien quiera mover el foco en confirmado()/cancelado() debe
// diferirlo (Qt.callLater).
Dialog {
    id: dialogo

    property string mensaje: ""
    property string textoConfirmar: qsTr("Confirmar")
    property string textoCancelar: qsTr("Cancelar")
    property string prefijoNombre: "dialogo"

    signal confirmado()
    signal cancelado()

    // Evita emitir cancelado() cuando el cierre viene de confirmar.
    property bool resuelto: false

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, (parent ? parent.width : 460) - 32)

    onOpened: {
        dialogo.resuelto = false
        botonCancelar.forceActiveFocus(Qt.TabFocusReason)
    }
    onClosed: {
        if (!dialogo.resuelto)
            dialogo.cancelado()
    }

    // Popup no deriva de Item: la semantica accesible va en el contenido.
    contentItem: ColumnLayout {
        objectName: dialogo.prefijoNombre + "Contenido"
        spacing: 16
        Accessible.role: Accessible.Dialog
        Accessible.name: dialogo.title
        Accessible.description: dialogo.mensaje

        Label {
            objectName: dialogo.prefijoNombre + "Mensaje"
            text: dialogo.mensaje
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: dialogo.mensaje
        }

        RowLayout {
            spacing: 8
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }
            BotonAccion {
                id: botonCancelar
                objectName: dialogo.prefijoNombre + "Cancelar"
                text: dialogo.textoCancelar
                // Tab/Backtab explicitos: el foco no sale del dialogo modal.
                Keys.onTabPressed: (evento) => { botonConfirmar.forceActiveFocus(Qt.TabFocusReason); evento.accepted = true }
                Keys.onBacktabPressed: (evento) => { botonConfirmar.forceActiveFocus(Qt.BacktabFocusReason); evento.accepted = true }
                onClicked: dialogo.close()
            }
            BotonAccion {
                id: botonConfirmar
                objectName: dialogo.prefijoNombre + "Confirmar"
                text: dialogo.textoConfirmar
                highlighted: true
                Keys.onTabPressed: (evento) => { botonCancelar.forceActiveFocus(Qt.TabFocusReason); evento.accepted = true }
                Keys.onBacktabPressed: (evento) => { botonCancelar.forceActiveFocus(Qt.BacktabFocusReason); evento.accepted = true }
                onClicked: {
                    dialogo.resuelto = true
                    dialogo.close()
                    dialogo.confirmado()
                }
            }
        }
    }
}
