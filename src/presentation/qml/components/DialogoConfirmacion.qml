import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Dialogo modal de confirmacion (T013, ficha DialogoConfirmacion).
// - variante "advertencia" (duplicado, reemplazo; icono exclamation-triangle en
//   tono advertencia) o "destructivo" (eliminar; icono trash en tono error y
//   boton de accion destructivo).
// - mensaje: motivo (600); consecuencia: texto secundario opcional.
// - procesando: BusyIndicator en la accion y botones deshabilitados.
// - mensajeError: AvisoEnLinea de error sobre los botones.
// Contenedor Theme.superficieElevada (radio 12) con velo Theme.velo; ancho 480,
// padding 20. Foco inicial en "Cancelar" (opcion segura y boton por omision);
// Tab alterna; Enter/Espacio activa; Escape cancela. Al cerrarse, Popup
// restaura el foco despues de emitir sus senales: quien quiera mover el foco
// en confirmado()/cancelado() debe diferirlo (Qt.callLater).
Dialog {
    id: dialogo

    property string mensaje: ""
    property string consecuencia: ""
    property string variante: "advertencia"
    property bool procesando: false
    property string mensajeError: ""
    property string textoConfirmar: qsTr("Confirmar")
    property string textoCancelar: qsTr("Cancelar")
    property string prefijoNombre: "dialogo"

    signal confirmado()
    signal cancelado()

    // Evita emitir cancelado() cuando el cierre viene de confirmar.
    property bool resuelto: false

    readonly property var colores: Theme.tono(dialogo.variante === "destructivo" ? "error" : "advertencia")

    modal: true
    focus: true
    closePolicy: dialogo.procesando ? Popup.NoAutoClose : Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(Theme.anchoDialogo, (parent ? parent.width : Theme.anchoDialogo) - 32)
    padding: 20
    header: null
    footer: null

    onOpened: {
        dialogo.resuelto = false
        botonCancelar.forceActiveFocus(Qt.TabFocusReason)
    }
    onClosed: {
        if (!dialogo.resuelto)
            dialogo.cancelado()
    }

    Overlay.modal: Rectangle {
        color: Theme.velo
    }

    background: Rectangle {
        radius: 12
        color: Theme.superficieElevada
        border.width: 1
        border.color: Theme.separador
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 6
            anchors.bottomMargin: -10
            anchors.leftMargin: -4
            anchors.rightMargin: -4
            radius: 16
            color: Theme.sombra
        }
    }

    // Popup no deriva de Item: la semantica accesible va en el contenido.
    contentItem: ColumnLayout {
        objectName: dialogo.prefijoNombre + "Contenido"
        spacing: Theme.espacioM
        Accessible.role: Accessible.Dialog
        Accessible.name: dialogo.title
        Accessible.description: dialogo.mensaje

        RowLayout {
            spacing: Theme.espacioM
            Layout.fillWidth: true
            Rectangle {
                implicitWidth: 40
                implicitHeight: 40
                radius: 10
                color: dialogo.colores.fondo
                border.width: 1
                border.color: dialogo.colores.borde
                Icono {
                    anchors.centerIn: parent
                    nombre: dialogo.variante === "destructivo" ? "trash" : "exclamation-triangle"
                    color: dialogo.colores.texto
                    tamano: 20
                }
            }
            Label {
                objectName: dialogo.prefijoNombre + "Titulo"
                text: dialogo.title
                color: Theme.texto
                font.family: Theme.familia
                font.pixelSize: Theme.subtitulo.size
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
        }

        Label {
            objectName: dialogo.prefijoNombre + "Mensaje"
            text: dialogo.mensaje
            color: Theme.texto
            font.family: Theme.familia
            font.pixelSize: Theme.cuerpo.size
            font.weight: dialogo.consecuencia.length > 0 ? Font.DemiBold : Font.Normal
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: dialogo.mensaje
        }
        Label {
            objectName: dialogo.prefijoNombre + "Consecuencia"
            visible: dialogo.consecuencia.length > 0
            text: dialogo.consecuencia
            color: Theme.textoSecundario
            font.family: Theme.familia
            font.pixelSize: Theme.cuerpo.size
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            Layout.fillWidth: true
        }
        AvisoEnLinea {
            objectName: dialogo.prefijoNombre + "Error"
            visible: dialogo.mensajeError.length > 0
            variante: "error"
            descripcion: dialogo.mensajeError
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: Theme.espacioS
            Layout.fillWidth: true
            Layout.topMargin: Theme.espacioXs

            Item { Layout.fillWidth: true }
            BotonAccion {
                id: botonCancelar
                objectName: dialogo.prefijoNombre + "Cancelar"
                text: dialogo.textoCancelar
                enabled: !dialogo.procesando
                // Tab/Backtab explicitos: el foco no sale del dialogo modal.
                Keys.onTabPressed: (evento) => { botonConfirmar.forceActiveFocus(Qt.TabFocusReason); evento.accepted = true }
                Keys.onBacktabPressed: (evento) => { botonConfirmar.forceActiveFocus(Qt.BacktabFocusReason); evento.accepted = true }
                onClicked: dialogo.close()
            }
            BotonAccion {
                id: botonConfirmar
                objectName: dialogo.prefijoNombre + "Confirmar"
                text: dialogo.textoConfirmar
                variante: dialogo.variante === "destructivo" ? "destructivo" : "primario"
                enabled: !dialogo.procesando
                cargando: dialogo.procesando
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
