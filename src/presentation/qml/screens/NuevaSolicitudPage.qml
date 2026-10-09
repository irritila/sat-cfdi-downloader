pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Formulario de nueva solicitud. Solo layout y estado visual: validacion,
// evaluacion de duplicados y envio viven en NuevaSolicitudViewModel.
// Teclado: foco inicial en el selector de perfil (o en "Administrar perfiles SAT"
// si no hay perfiles); Tab recorre los campos; Enter en un campo de texto o en
// "Crear solicitud" envia; Escape o "Regresar" vuelve a la lista. El dialogo de
// duplicado inicia en "Cancelar" y Escape lo cancela.
Page {
    id: pagina
    objectName: "paginaNuevaSolicitud"

    required property AppViewModel app
    required property NuevaSolicitudViewModel formulario

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Nueva solicitud de descarga masiva")

    // Foco inicial diferido: la pagina ya esta en la ventana.
    Component.onCompleted: {
        pagina.sinPerfilesAnterior = pagina.formulario.sinPerfiles
        Qt.callLater(pagina.enfocarInicial)
    }

    // El foco inicial es diferido: si para entonces ya hay un dialogo
    // abierto, no se le quita el foco.
    function enfocarInicial() {
        if (dialogoDuplicado.visible)
            return
        if (pagina.formulario.sinPerfiles)
            botonAdministrarPerfiles.forceActiveFocus(Qt.TabFocusReason)
        else
            selectorPerfil.forceActiveFocus(Qt.TabFocusReason)
    }

    function regresar() {
        pagina.app.mostrarLista()
    }

    function enfocarEnviar() {
        botonEnviar.forceActiveFocus(Qt.TabFocusReason)
    }

    Keys.onEscapePressed: regresar()

    // Valor previo (sin binding) para detectar transiciones.
    property bool sinPerfilesAnterior: false

    Connections {
        target: pagina.formulario
        function onEstadoChanged() {
            // Mueve el foco si la seccion sin perfiles listos aparece o se oculta.
            if (pagina.formulario.sinPerfiles !== pagina.sinPerfilesAnterior) {
                pagina.sinPerfilesAnterior = pagina.formulario.sinPerfiles
                // El dialogo modal, si esta abierto, conserva el foco.
                if (!dialogoDuplicado.visible) {
                    if (pagina.formulario.sinPerfiles)
                        botonAdministrarPerfiles.forceActiveFocus(Qt.OtherFocusReason)
                    else
                        selectorPerfil.forceActiveFocus(Qt.OtherFocusReason)
                }
            }

            if (pagina.formulario.confirmacionPendiente && !dialogoDuplicado.opened)
                dialogoDuplicado.open()
            else if (!pagina.formulario.confirmacionPendiente && dialogoDuplicado.opened) {
                dialogoDuplicado.resuelto = true
                dialogoDuplicado.close()
            }
        }
    }

    DialogoConfirmacion {
        id: dialogoDuplicado
        objectName: "dialogoDuplicado"
        prefijoNombre: "dialogoDuplicado"
        title: qsTr("Solicitud posiblemente duplicada")
        mensaje: qsTr("%1\n\nPuedes crear otra solicitud con los mismos filtros o cancelar.")
                     .arg(pagina.formulario.motivoDuplicado)
        textoConfirmar: qsTr("Crear de todos modos")
        textoCancelar: qsTr("Cancelar")
        onConfirmado: pagina.formulario.confirmarDuplicado()
        onCancelado: {
            pagina.formulario.cancelarDuplicado()
            Qt.callLater(pagina.enfocarEnviar)
        }
    }

    header: EncabezadoPagina {
        titulo: qsTr("Nueva solicitud")
        mostrarRegresar: true
        onRegresarSolicitado: pagina.regresar()

        BotonAccion {
            objectName: "botonPerfilesSat"
            text: qsTr("Perfiles SAT")
            descripcion: qsTr("Administrar perfiles SAT y su e.firma")
            onClicked: pagina.app.mostrarPerfiles()
        }
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

            // Sin perfiles listos (activo + e.firma Lista): ir a Perfiles SAT.
            ColumnLayout {
                id: seccionSinPerfiles
                objectName: "seccionSinPerfiles"
                visible: pagina.formulario.sinPerfiles
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                spacing: 6

                Label {
                    objectName: "mensajeSinPerfiles"
                    text: qsTr("No hay perfiles SAT listos para solicitudes. Un perfil necesita estar activo y tener su e.firma registrada y vigente.")
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                BotonAccion {
                    id: botonAdministrarPerfiles
                    objectName: "botonAdministrarPerfiles"
                    text: qsTr("Administrar perfiles SAT")
                    descripcion: qsTr("Abrir Perfiles SAT para crear perfiles y registrar su e.firma")
                    onClicked: pagina.app.mostrarPerfiles()
                }
            }

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
                enabled: !pagina.formulario.sinPerfiles
                model: pagina.formulario.perfilesDisponibles
                textRole: "etiqueta"
                valueRole: "id"
                // Depende de count para reevaluarse cuando llegan los perfiles.
                currentIndex: pagina.formulario.perfilesDisponibles.count > 0
                              ? pagina.formulario.perfilesDisponibles.filaDe(pagina.formulario.perfilId)
                              : -1
                displayText: pagina.formulario.cargandoPerfiles && count === 0
                             ? qsTr("Cargando perfiles...")
                             : (currentIndex < 0 ? qsTr("Selecciona un perfil SAT") : currentText)
                onActivated: (indice) => { pagina.formulario.perfilId = valueAt(indice) }
                Accessible.name: qsTr("Perfil SAT")
                Accessible.description: displayText
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
                // Depende de count: al crearse, indexOfValue() se evalua antes de
                // que el modelo este listo y devolveria -1 (combo vacio).
                currentIndex: count > 0 ? indexOfValue(pagina.formulario.tipoDescarga) : -1
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
                    Label { text: qsTr("Tipo de comprobante (opcional)"); Accessible.ignored: true }
                    ComboBox {
                        objectName: "campoTipoComprobante"
                        Layout.fillWidth: true
                        focusPolicy: Qt.StrongFocus
                        textRole: "texto"
                        valueRole: "clave"
                        model: [
                            { clave: "", texto: qsTr("Todos") },
                            { clave: "I", texto: qsTr("I - Ingreso") },
                            { clave: "E", texto: qsTr("E - Egreso") },
                            { clave: "T", texto: qsTr("T - Traslado") },
                            { clave: "N", texto: qsTr("N - Nomina") },
                            { clave: "P", texto: qsTr("P - Pago") }
                        ]
                        // Depende de count (ver campoTipoDescarga).
                        currentIndex: count > 0 ? indexOfValue(pagina.formulario.tipoComprobante) : -1
                        onActivated: (indice) => { pagina.formulario.tipoComprobante = valueAt(indice) }
                        Accessible.name: qsTr("Tipo de comprobante, opcional")
                        Accessible.description: displayText
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Complemento (opcional)"); Accessible.ignored: true }
                    TextField {
                        objectName: "campoComplemento"
                        Layout.fillWidth: true
                        text: pagina.formulario.complemento
                        onTextEdited: pagina.formulario.complemento = text
                        onAccepted: pagina.formulario.submit()
                        Accessible.name: qsTr("Complemento, opcional")
                    }
                }
            }

            Label {
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

                BotonAccion {
                    objectName: "botonVerSolicitudExistente"
                    visible: pagina.formulario.solicitudExistenteId.length > 0
                    text: qsTr("Ver solicitud existente")
                    descripcion: qsTr("Abrir el detalle de la solicitud equivalente")
                    onClicked: pagina.app.abrirDetalle(pagina.formulario.solicitudExistenteId)
                }
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
