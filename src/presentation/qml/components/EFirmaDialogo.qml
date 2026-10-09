import QtQuick
import QtQuick.Controls.Basic
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
    // Datos del perfil para el subtitulo y el aviso de exito (la pagina los da).
    property string nombrePerfil: ""
    property string vigenteHasta: ""

    // Solo para pruebas y foco: el control, nunca su texto.
    readonly property alias campoContrasena: campoContrasena

    readonly property bool validando: dialogo.eFirma.fase === EFirmaFormViewModel.Validando
    readonly property bool exito: dialogo.eFirma.fase === EFirmaFormViewModel.Exito
    readonly property bool conError: dialogo.eFirma.fase === EFirmaFormViewModel.Error
    // Fase vista en el ultimo cambio (para enfocar solo en las transiciones).
    property int faseAnterior: EFirmaFormViewModel.Capturando
    // Campo culpable del error ("certificado", "llave", "contrasena") o vacio.
    readonly property string campoError: dialogo.conError ? dialogo.eFirma.campoConError : ""
    readonly property string subtitulo: [dialogo.eFirma.perfilRfc, dialogo.nombrePerfil]
                                        .filter(t => t.length > 0).join(" · ")

    modal: true
    focus: true
    closePolicy: dialogo.validando ? Popup.NoAutoClose : Popup.CloseOnEscape
    // Centrado en horizontal, a 104 del borde superior (ficha EFirmaDialogo).
    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.max(16, Math.min(104, parent.height - height - 16)) : 0
    width: Math.min(Theme.anchoDialogo, (parent ? parent.width : Theme.anchoDialogo) - 32)
    padding: 20
    header: null
    footer: null
    title: dialogo.eFirma.esReemplazo ? qsTr("Reemplazar e.firma") : qsTr("Registrar e.firma")

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
            campoCertificado.boton.forceActiveFocus(Qt.OtherFocusReason)
            break
        case "llave":
            campoLlave.boton.forceActiveFocus(Qt.OtherFocusReason)
            break
        case "contrasena":
            campoContrasena.forceActiveFocus(Qt.OtherFocusReason)
            break
        default:
            botonCancelar.forceActiveFocus(Qt.OtherFocusReason)
        }
    }

    onOpened: campoCertificado.boton.forceActiveFocus(Qt.TabFocusReason)
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
        // Solo al ENTRAR en Error o Exito: los demas cambios (elegir archivo,
        // escribir la contrasena) no deben mover el foco.
        function onCambio() {
            const fase = dialogo.eFirma.fase
            const anterior = dialogo.faseAnterior
            dialogo.faseAnterior = fase
            if (fase === anterior || !dialogo.opened)
                return
            if (fase === EFirmaFormViewModel.Error)
                Qt.callLater(dialogo.enfocarCampoConError)
            else if (fase === EFirmaFormViewModel.Exito)
                Qt.callLater(dialogo.enfocarCerrar)
        }
    }

    Component {
        id: selectorComp
        FileDialog {
            fileMode: FileDialog.OpenFile
        }
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

    // Base visual T013 (ficha EFirmaDialogo): cabecera con icono key en tono
    // progreso, filas CampoFormulario (etiqueta de 150) con CampoArchivo y la
    // contrasena, y pie con Cancelar y la accion primaria.
    contentItem: ColumnLayout {
        objectName: "dialogoEFirmaContenido"
        spacing: Theme.espacioM
        Accessible.role: Accessible.Dialog
        Accessible.name: dialogo.subtitulo.length > 0 ? dialogo.title + ", " + dialogo.subtitulo : dialogo.title

        RowLayout {
            spacing: Theme.espacioM
            Layout.fillWidth: true
            Rectangle {
                readonly property var colores: Theme.tono(dialogo.exito ? "exito" : "progreso")
                implicitWidth: 40
                implicitHeight: 40
                radius: 10
                color: colores.fondo
                border.width: 1
                border.color: colores.borde
                Accessible.ignored: true
                Icono {
                    anchors.centerIn: parent
                    nombre: "key"
                    color: parent.colores.texto
                    tamano: 20
                }
            }
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Label {
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
                Label {
                    objectName: "subtituloEFirma"
                    visible: text.length > 0
                    text: dialogo.subtitulo
                    color: Theme.textoSecundario
                    font.family: Theme.familia
                    font.pixelSize: Theme.leyenda.size
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }

        Label {
            visible: !dialogo.exito
            text: qsTr("Elige el certificado (.cer), la llave privada (.key) y escribe la contraseña de la llave.")
            color: Theme.texto
            font.family: Theme.familia
            font.pixelSize: Theme.cuerpo.size
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // Tras un fallo del servicio la seleccion se descarta: se pide de nuevo.
        AvisoEnLinea {
            objectName: "avisoReintentoEFirma"
            visible: dialogo.conError && dialogo.eFirma.requiereNuevaSeleccion
            variante: "advertencia"
            anunciar: false
            titulo: qsTr("Vuelve a elegir los archivos y escribe la contraseña.")
            Layout.fillWidth: true
        }

        ColumnLayout {
            visible: !dialogo.exito
            spacing: 0
            Layout.fillWidth: true
            // CampoFormulario trae 12 de margen interno: se alinea con el texto.
            Layout.leftMargin: -12
            Layout.rightMargin: -12

            CampoFormulario {
                objectName: "filaCertificado"
                etiqueta: qsTr("Certificado (.cer)")
                anchoEtiqueta: 150
                separador: false
                error: dialogo.campoError === "certificado" ? dialogo.eFirma.errorMessage : ""
                objectNameError: "errorCertificado"
                CampoArchivo {
                    id: campoCertificado
                    objectNameBoton: "botonElegirCertificado"
                    objectNameNombre: "nombreCertificado"
                    nombreArchivo: dialogo.eFirma.nombreCertificado
                    iconoArchivo: "doc"
                    conError: dialogo.campoError === "certificado"
                    nombreBoton: qsTr("Elegir certificado (.cer)")
                    enabled: !dialogo.validando && !dialogo.exito
                    Layout.fillWidth: true
                    onElegirSolicitado: dialogo.elegirArchivo(true)
                }
            }
            CampoFormulario {
                objectName: "filaLlave"
                etiqueta: qsTr("Llave privada (.key)")
                anchoEtiqueta: 150
                separador: false
                error: dialogo.campoError === "llave" ? dialogo.eFirma.errorMessage : ""
                objectNameError: "errorLlave"
                CampoArchivo {
                    id: campoLlave
                    objectNameBoton: "botonElegirLlave"
                    objectNameNombre: "nombreLlave"
                    nombreArchivo: dialogo.eFirma.nombreLlave
                    iconoArchivo: "key"
                    conError: dialogo.campoError === "llave"
                    nombreBoton: qsTr("Elegir llave privada (.key)")
                    enabled: !dialogo.validando && !dialogo.exito
                    Layout.fillWidth: true
                    onElegirSolicitado: dialogo.elegirArchivo(false)
                }
            }
            CampoFormulario {
                objectName: "filaContrasena"
                etiqueta: qsTr("Contraseña")
                anchoEtiqueta: 150
                ayuda: qsTr("Se guarda en el llavero de macOS.")
                separador: false
                error: dialogo.campoError === "contrasena" ? dialogo.eFirma.errorMessage : ""
                objectNameError: "errorContrasena"
                CampoTexto {
                    id: campoContrasena
                    objectName: "campoContrasena"
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                    conError: dialogo.campoError === "contrasena"
                    enabled: !dialogo.validando && !dialogo.exito
                    onAccepted: dialogo.enviar()
                    // Solo informa SI hay contrasena (para habilitar Registrar), nunca el texto.
                    onTextChanged: dialogo.eFirma.claveEscrita = text.length > 0
                    // Sin Accessible.passwordEdit explicito: Qt vaciaria el nombre; el
                    // echoMode Password ya expone el estado de contrasena.
                    Accessible.name: qsTr("Contraseña de la llave privada")
                    Accessible.description: dialogo.campoError === "contrasena" ? dialogo.eFirma.errorMessage
                                                                               : qsTr("Se guarda en el llavero de macOS.")
                }
            }
        }

        RowLayout {
            // Error sin campo culpable (p. ej. pareja incompatible); los demas van bajo su campo.
            objectName: "errorEFirma"
            visible: dialogo.conError && dialogo.campoError.length === 0 && textoError.text.length > 0
            spacing: Theme.espacioXs
            Layout.fillWidth: true
            Accessible.role: Accessible.AlertMessage
            Accessible.name: qsTr("Error: %1").arg(textoError.text)
            readonly property string text: textoError.text
            Icono {
                nombre: "exclamation-octagon"
                color: Theme.error
                tamano: 14
                Layout.alignment: Qt.AlignTop
            }
            Label {
                id: textoError
                text: dialogo.eFirma.errorMessage
                color: Theme.error
                font.family: Theme.familia
                font.pixelSize: Theme.cuerpo.size
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Accessible.ignored: true
            }
        }

        AvisoEnLinea {
            objectName: "exitoEFirma"
            visible: dialogo.exito
            variante: "exito"
            anunciar: false
            titulo: dialogo.eFirma.esReemplazo ? qsTr("e.firma reemplazada.") : qsTr("e.firma registrada.")
            descripcion: dialogo.vigenteHasta.length > 0
                         ? qsTr("%1 ya está disponible para solicitudes. Vigente hasta %2.")
                               .arg(dialogo.eFirma.perfilRfc).arg(dialogo.vigenteHasta)
                         : qsTr("%1 ya está disponible para solicitudes.").arg(dialogo.eFirma.perfilRfc)
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.espacioS

            RowLayout {
                objectName: "estadoValidandoEFirma"
                visible: dialogo.validando
                spacing: Theme.espacioS
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Validando e.firma")
                BusyIndicator {
                    running: dialogo.validando
                    implicitWidth: 18
                    implicitHeight: 18
                    Accessible.ignored: true
                }
                Label {
                    text: qsTr("Validando e.firma…")
                    color: Theme.textoSecundario
                    font.family: Theme.familia
                    font.pixelSize: Theme.cuerpo.size
                    Accessible.ignored: true
                }
            }
            // Que falta para habilitar la accion primaria.
            Label {
                id: textoFaltante
                objectName: "faltanteEFirma"
                visible: !dialogo.validando && !dialogo.exito && text.length > 0
                         && !(dialogo.conError && dialogo.eFirma.requiereNuevaSeleccion)
                text: dialogo.eFirma.faltante
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Item {
                visible: !textoFaltante.visible
                Layout.fillWidth: true
            }
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
                variante: "primario"
                enabled: dialogo.eFirma.puedeEnviar
                onClicked: dialogo.enviar()
            }
            BotonAccion {
                id: botonCerrar
                objectName: "botonCerrarEFirma"
                visible: dialogo.exito
                variante: "primario"
                text: qsTr("Cerrar")
                onClicked: dialogo.close()
            }
        }
    }
}
