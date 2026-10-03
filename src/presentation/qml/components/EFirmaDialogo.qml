import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Captura de e.firma (registro o reemplazo) para un perfil guardado (T005.1).
//
// Seguridad (DA3): cada archivo se elige con un FileDialog creado para esa
// eleccion y destruido al terminar; su QUrl se entrega de inmediato al view
// model (seleccionarCertificado/seleccionarLlave), que guarda la ruta en
// privado y expone solo el basename. QML no conserva URLs. Al enviar, la
// contrasena se entrega en una sola llamada sincrona a eFirma.enviar() (las
// URLs vacias usan la seleccion del view model) y el campo se vacia EN EL
// MISMO manejador. Tambien se vacia al cancelar, al cerrar, al ocultar la
// ventana (limpiarCaptura desde la pagina) y al destruir el componente.
//
// Teclado: foco inicial en "Elegir certificado"; Tab recorre certificado,
// llave, contrasena, Cancelar y Registrar; Enter en la contrasena envia;
// Escape cancela (salvo durante la validacion).
Dialog {
    id: dialogo
    objectName: "dialogoEFirma"

    required property EFirmaFormViewModel eFirma

    // Solo para pruebas y foco: el control, nunca su texto.
    readonly property alias campoContrasena: campoContrasena

    readonly property bool validando: dialogo.eFirma.fase === EFirmaFormViewModel.Validando
    readonly property bool exito: dialogo.eFirma.fase === EFirmaFormViewModel.Exito

    modal: true
    focus: true
    closePolicy: dialogo.validando ? Popup.NoAutoClose : Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, (parent ? parent.width : 520) - 32)
    title: dialogo.eFirma.esReemplazo ? qsTr("Reemplazar e.firma de %1").arg(dialogo.eFirma.perfilRfc)
                                      : qsTr("Registrar e.firma de %1").arg(dialogo.eFirma.perfilRfc)

    // Vacia el control de contrasena.
    function limpiarCaptura() {
        campoContrasena.clear()
    }

    // Abre un selector de archivos efimero; la QUrl elegida va directo al VM.
    function elegirArchivo(esCertificado) {
        const selector = selectorComp.createObject(dialogo, {
            title: esCertificado ? qsTr("Elegir certificado de e.firma")
                                 : qsTr("Elegir llave privada de e.firma"),
            nameFilters: esCertificado
                         ? [qsTr("Certificado (*.cer)"), qsTr("Todos los archivos (*)")]
                         : [qsTr("Llave privada (*.key)"), qsTr("Todos los archivos (*)")]
        })
        selector.accepted.connect(() => {
            if (esCertificado)
                dialogo.eFirma.seleccionarCertificado(selector.selectedFile)
            else
                dialogo.eFirma.seleccionarLlave(selector.selectedFile)
            selector.destroy()
        })
        selector.rejected.connect(() => selector.destroy())
        selector.open()
    }

    function enviar() {
        dialogo.eFirma.enviar("", "", campoContrasena.text)
        // Mismo manejador: la contrasena no sobrevive a la llamada.
        campoContrasena.clear()
    }

    function cancelar() {
        if (dialogo.validando)
            return
        dialogo.limpiarCaptura()
        dialogo.eFirma.cancelar()
        dialogo.close()
    }

    function enfocarCerrar() {
        botonCerrar.forceActiveFocus(Qt.OtherFocusReason)
    }

    function enfocarCampoConError() {
        switch (dialogo.eFirma.campoConError) {
        case "certificado":
            botonCertificado.forceActiveFocus(Qt.OtherFocusReason)
            break
        case "llave":
            botonLlave.forceActiveFocus(Qt.OtherFocusReason)
            break
        case "contrasena":
            campoContrasena.forceActiveFocus(Qt.OtherFocusReason)
            break
        default:
            botonCancelar.forceActiveFocus(Qt.OtherFocusReason)
        }
    }

    onOpened: botonCertificado.forceActiveFocus(Qt.TabFocusReason)
    // Abandona el contexto (cambio de perfil, ventana oculta): contrasena,
    // seleccion y operacion en curso se descartan; una respuesta tardia no se
    // aplica.
    function abandonar() {
        dialogo.limpiarCaptura()
        dialogo.eFirma.abandonar()
        dialogo.close()
    }

    // Cerrarse por cualquier via descarta contrasena y seleccion de archivos.
    onClosed: {
        dialogo.limpiarCaptura()
        dialogo.eFirma.descartarSeleccion()
        if (!dialogo.validando)
            dialogo.eFirma.cancelar()
    }
    Component.onDestruction: {
        campoContrasena.clear()
        if (dialogo.eFirma)
            dialogo.eFirma.descartarSeleccion()
    }

    Connections {
        target: dialogo.eFirma
        function onCambio() {
            if (dialogo.eFirma.fase === EFirmaFormViewModel.Error && dialogo.opened)
                Qt.callLater(dialogo.enfocarCampoConError)
            else if (dialogo.eFirma.fase === EFirmaFormViewModel.Exito && dialogo.opened)
                Qt.callLater(dialogo.enfocarCerrar)
        }
    }

    Component {
        id: selectorComp
        FileDialog {
            fileMode: FileDialog.OpenFile
        }
    }

    contentItem: ColumnLayout {
        objectName: "dialogoEFirmaContenido"
        spacing: 10
        Accessible.role: Accessible.Dialog
        Accessible.name: dialogo.title

        Label {
            text: dialogo.eFirma.esReemplazo
                  ? qsTr("La e.firma actual sigue registrada hasta que la nueva se valide.")
                  : qsTr("Elige el certificado (.cer), la llave privada (.key) y escribe su contrasena.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            BotonAccion {
                id: botonCertificado
                objectName: "botonElegirCertificado"
                text: qsTr("Elegir certificado (.cer)")
                descripcion: dialogo.eFirma.nombreCertificado.length > 0
                             ? qsTr("Certificado elegido: %1").arg(dialogo.eFirma.nombreCertificado)
                             : qsTr("Ningun certificado elegido")
                enabled: !dialogo.validando && !dialogo.exito
                onClicked: dialogo.elegirArchivo(true)
            }
            Label {
                objectName: "nombreCertificado"
                text: dialogo.eFirma.nombreCertificado.length > 0 ? dialogo.eFirma.nombreCertificado
                                                                 : qsTr("Sin archivo")
                font.bold: dialogo.eFirma.campoConError === "certificado"
                elide: Text.ElideMiddle
                Layout.fillWidth: true
                Accessible.ignored: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            BotonAccion {
                id: botonLlave
                objectName: "botonElegirLlave"
                text: qsTr("Elegir llave privada (.key)")
                descripcion: dialogo.eFirma.nombreLlave.length > 0
                             ? qsTr("Llave elegida: %1").arg(dialogo.eFirma.nombreLlave)
                             : qsTr("Ninguna llave elegida")
                enabled: !dialogo.validando && !dialogo.exito
                onClicked: dialogo.elegirArchivo(false)
            }
            Label {
                objectName: "nombreLlave"
                text: dialogo.eFirma.nombreLlave.length > 0 ? dialogo.eFirma.nombreLlave : qsTr("Sin archivo")
                font.bold: dialogo.eFirma.campoConError === "llave"
                elide: Text.ElideMiddle
                Layout.fillWidth: true
                Accessible.ignored: true
            }
        }

        Label {
            text: qsTr("Contrasena de la llave privada")
            Accessible.ignored: true
        }
        TextField {
            id: campoContrasena
            objectName: "campoContrasena"
            Layout.fillWidth: true
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            enabled: !dialogo.validando && !dialogo.exito
            onAccepted: dialogo.enviar()
            // Sin Accessible.passwordEdit explicito: Qt vaciaria el nombre; el
            // echoMode Password ya expone el estado de contrasena.
            Accessible.name: qsTr("Contrasena de la llave privada")
        }

        Label {
            objectName: "errorEFirma"
            visible: dialogo.eFirma.fase === EFirmaFormViewModel.Error && text.length > 0
            text: dialogo.eFirma.errorMessage
            color: "#b00020"
            font.bold: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Accessible.role: Accessible.AlertMessage
            Accessible.name: qsTr("Error: %1").arg(text)
        }

        RowLayout {
            objectName: "estadoValidandoEFirma"
            visible: dialogo.validando
            spacing: 8
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Validando e.firma")
            BusyIndicator {
                running: dialogo.validando
                implicitWidth: 24
                implicitHeight: 24
                Accessible.ignored: true
            }
            Label {
                text: qsTr("Validando e.firma...")
                Accessible.ignored: true
            }
        }

        Label {
            objectName: "exitoEFirma"
            visible: dialogo.exito
            text: dialogo.eFirma.esReemplazo ? qsTr("e.firma reemplazada.") : qsTr("e.firma registrada.")
            font.bold: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            BotonAccion {
                id: botonCancelar
                objectName: "botonCancelarEFirma"
                visible: !dialogo.exito
                text: qsTr("Cancelar")
                enabled: !dialogo.validando
                onClicked: dialogo.cancelar()
            }
            BotonAccion {
                id: botonEnviar
                objectName: "botonEnviarEFirma"
                visible: !dialogo.exito
                text: dialogo.eFirma.esReemplazo ? qsTr("Reemplazar") : qsTr("Registrar")
                descripcion: qsTr("Validar y guardar la e.firma en el llavero de macOS")
                highlighted: true
                enabled: dialogo.eFirma.puedeEnviar
                onClicked: dialogo.enviar()
            }
            BotonAccion {
                id: botonCerrar
                objectName: "botonCerrarEFirma"
                visible: dialogo.exito
                text: qsTr("Cerrar")
                onClicked: dialogo.close()
            }
        }
    }
}
