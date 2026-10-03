pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Formulario de nueva solicitud. Solo layout y estado visual: la validacion y
// el envio viven en NuevaSolicitudViewModel. Teclado: foco inicial en el
// selector de perfil; Tab recorre los campos; Enter en un campo de texto o en
// "Crear solicitud" envia; Escape o "Regresar" vuelve a la lista.
Page {
    id: pagina
    objectName: "paginaNuevaSolicitud"

    required property AppViewModel app
    required property NuevaSolicitudViewModel formulario

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Nueva solicitud de descarga masiva")

    // Foco inicial diferido: la pagina ya esta en la ventana.
    Component.onCompleted: Qt.callLater(pagina.enfocarInicial)

    function enfocarInicial() {
        selectorPerfil.forceActiveFocus(Qt.TabFocusReason)
    }

    function regresar() {
        pagina.app.mostrarLista()
    }

    Keys.onEscapePressed: regresar()

    header: EncabezadoPagina {
        titulo: qsTr("Nueva solicitud")
        mostrarRegresar: true
        onRegresarSolicitado: pagina.regresar()
    }

    ScrollView {
        id: desplazamiento
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: desplazamiento.availableWidth
            spacing: 6

            Item { implicitHeight: 8 }

            Label {
                text: qsTr("Perfil SAT")
                Layout.leftMargin: 16
                Accessible.ignored: true
            }
            ComboBox {
                id: selectorPerfil
                objectName: "campoPerfil"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                focusPolicy: Qt.StrongFocus
                model: pagina.formulario.perfilesDisponibles
                textRole: "etiqueta"
                valueRole: "id"
                // Depende de count para reevaluarse cuando llegan los perfiles.
                currentIndex: pagina.formulario.perfilesDisponibles.count > 0
                              ? pagina.formulario.perfilesDisponibles.filaDe(pagina.formulario.perfilId)
                              : -1
                displayText: currentIndex < 0 ? qsTr("Selecciona un perfil SAT") : currentText
                onActivated: (indice) => { pagina.formulario.perfilId = valueAt(indice) }
                Accessible.name: qsTr("Perfil SAT")
                Accessible.description: displayText
            }
            Label {
                visible: pagina.formulario.perfilesDisponibles.count === 0
                text: qsTr("No hay perfiles SAT activos disponibles.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
            }

            Label {
                text: qsTr("Tipo de descarga")
                Layout.leftMargin: 16
                Layout.topMargin: 8
                Accessible.ignored: true
            }
            ComboBox {
                id: selectorTipo
                objectName: "campoTipoDescarga"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                focusPolicy: Qt.StrongFocus
                textRole: "texto"
                valueRole: "clave"
                model: [
                    { clave: "Emitidos", texto: qsTr("Emitidos") },
                    { clave: "Recibidos", texto: qsTr("Recibidos") }
                ]
                currentIndex: indexOfValue(pagina.formulario.tipoDescarga)
                onActivated: (indice) => { pagina.formulario.tipoDescarga = valueAt(indice) }
                Accessible.name: qsTr("Tipo de descarga")
                Accessible.description: displayText
            }

            GridLayout {
                columns: desplazamiento.availableWidth >= 520 ? 2 : 1
                columnSpacing: 16
                rowSpacing: 6
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Fecha inicial (AAAA-MM-DD)"); Accessible.ignored: true }
                    TextField {
                        id: campoFechaInicial
                        objectName: "campoFechaInicial"
                        Layout.fillWidth: true
                        text: pagina.formulario.fechaInicial
                        placeholderText: "2026-01-01"
                        inputMethodHints: Qt.ImhDate
                        onTextEdited: pagina.formulario.fechaInicial = text
                        onAccepted: pagina.formulario.submit()
                        Accessible.name: qsTr("Fecha inicial")
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Fecha final (AAAA-MM-DD)"); Accessible.ignored: true }
                    TextField {
                        id: campoFechaFinal
                        objectName: "campoFechaFinal"
                        Layout.fillWidth: true
                        text: pagina.formulario.fechaFinal
                        placeholderText: "2026-01-31"
                        inputMethodHints: Qt.ImhDate
                        onTextEdited: pagina.formulario.fechaFinal = text
                        onAccepted: pagina.formulario.submit()
                        Accessible.name: qsTr("Fecha final")
                    }
                }
            }

            Label {
                text: qsTr("RFC contraparte (opcional)")
                Layout.leftMargin: 16
                Layout.topMargin: 8
                Accessible.ignored: true
            }
            TextField {
                id: campoRfc
                objectName: "campoRfcContraparte"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                text: pagina.formulario.rfcContraparte
                maximumLength: 13
                onTextEdited: pagina.formulario.rfcContraparte = text
                onAccepted: pagina.formulario.submit()
                Accessible.name: qsTr("RFC contraparte, opcional")
            }

            Label {
                id: mensajeError
                objectName: "mensajeError"
                visible: text.length > 0
                text: pagina.formulario.errorMessage
                color: "#b00020"
                font.bold: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                Accessible.role: Accessible.AlertMessage
                Accessible.name: qsTr("Error: %1").arg(text)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                Layout.bottomMargin: 16
                spacing: 8

                Item { Layout.fillWidth: true }
                BusyIndicator {
                    running: pagina.formulario.ocupado
                    visible: running
                    implicitWidth: 24
                    implicitHeight: 24
                    Accessible.name: qsTr("Enviando")
                }
                BotonAccion {
                    id: botonEnviar
                    objectName: "botonCrearSolicitud"
                    text: qsTr("Crear solicitud")
                    descripcion: pagina.formulario.canSubmit
                                 ? qsTr("Crear la solicitud con los datos capturados")
                                 : qsTr("Completa el formulario para crear la solicitud")
                    highlighted: true
                    enabled: !pagina.formulario.ocupado
                    onClicked: pagina.formulario.submit()
                }
            }
        }
    }
}
