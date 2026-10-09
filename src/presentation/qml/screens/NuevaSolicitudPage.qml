pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Nueva solicitud (T013, traspaso "NuevaSolicitudPage"; UX-14..UX-21). Solo
// layout y estado visual: validacion, evaluacion de duplicados y envio viven en
// NuevaSolicitudViewModel.
// - Columna centrada de 640 con los grupos "Solicitud" (perfil, tipo de
//   descarga con SelectorSegmentado y periodo en una fila) y "Filtros
//   opcionales"; pie fijo con ayuda, "Cancelar" y "Crear solicitud".
// - Errores bajo su campo (campoConError) con borde de error y foco en el
//   primer campo invalido; errores sin campo (bloqueo por duplicado,
//   persistencia) en un AvisoEnLinea sobre el formulario.
// - Sin perfiles listos: EstadoVacio con "Administrar perfiles SAT".
// - Duplicado que requiere confirmacion: DialogoConfirmacion con "Cancelar"
//   por omision.
// Teclado: foco inicial en el perfil (o en "Administrar perfiles SAT");
// Tab recorre los campos y el pie; Enter en un campo de texto envia; Escape o
// "Solicitudes" vuelve a la lista.
Page {
    id: pagina
    objectName: "paginaNuevaSolicitud"

    required property AppViewModel app
    required property NuevaSolicitudViewModel formulario

    readonly property string campoError: pagina.formulario.campoConError
    readonly property bool bloqueada: pagina.formulario.solicitudExistenteId.length > 0

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Nueva solicitud de descarga masiva")

    background: Rectangle {
        color: Theme.fondo
    }

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
            sinPerfiles.botonAccion.forceActiveFocus(Qt.TabFocusReason)
        else
            selectorPerfil.forceActiveFocus(Qt.TabFocusReason)
    }

    function regresar() {
        pagina.app.mostrarLista()
    }

    function enfocarEnviar() {
        botonEnviar.forceActiveFocus(Qt.TabFocusReason)
    }

    // Lleva el foco al primer campo invalido (tras intentar crear).
    function enfocarError() {
        switch (pagina.campoError) {
        case "perfil": selectorPerfil.forceActiveFocus(Qt.OtherFocusReason); break
        case "fechaInicial": campoFechaInicial.forceActiveFocus(Qt.OtherFocusReason); break
        case "fechaFinal": campoFechaFinal.forceActiveFocus(Qt.OtherFocusReason); break
        case "rfcContraparte": campoRfc.forceActiveFocus(Qt.OtherFocusReason); break
        case "tipoComprobante": selectorComprobante.forceActiveFocus(Qt.OtherFocusReason); break
        case "complemento": campoComplemento.forceActiveFocus(Qt.OtherFocusReason); break
        }
    }

    function enviar() {
        pagina.formulario.submit()
        Qt.callLater(pagina.enfocarError)
    }

    Keys.onEscapePressed: regresar()

    // Valor previo (sin binding) para detectar transiciones.
    property bool sinPerfilesAnterior: false

    Connections {
        target: pagina.formulario
        function onEstadoChanged() {
            // Mueve el foco si el estado sin perfiles listos aparece o se oculta.
            if (pagina.formulario.sinPerfiles !== pagina.sinPerfilesAnterior) {
                pagina.sinPerfilesAnterior = pagina.formulario.sinPerfiles
                // El dialogo modal, si esta abierto, conserva el foco.
                if (!dialogoDuplicado.visible) {
                    if (pagina.formulario.sinPerfiles)
                        sinPerfiles.botonAccion.forceActiveFocus(Qt.OtherFocusReason)
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
        // Un error del servicio con campo (validacion autoritativa) enfoca el campo.
        function onOcupadoChanged() {
            if (!pagina.formulario.ocupado && pagina.campoError.length > 0)
                Qt.callLater(pagina.enfocarError)
        }
    }

    DialogoConfirmacion {
        id: dialogoDuplicado
        objectName: "dialogoDuplicado"
        prefijoNombre: "dialogoDuplicado"
        variante: "advertencia"
        title: qsTr("¿Crear otra solicitud con los mismos filtros?")
        mensaje: pagina.formulario.motivoDuplicado
        consecuencia: qsTr("Puedes crear otra solicitud con los mismos filtros o cancelar.")
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
        textoRegresar: qsTr("Solicitudes")
        onRegresarSolicitado: pagina.regresar()

        BotonAccion {
            objectName: "botonPerfilesSat"
            icono: "person-card"
            text: qsTr("Perfiles SAT")
            descripcion: qsTr("Administrar perfiles SAT y su e.firma")
            onClicked: pagina.app.mostrarPerfiles()
        }
    }

    footer: Rectangle {
        objectName: "pieNuevaSolicitud"
        visible: !pagina.formulario.sinPerfiles
        implicitHeight: Theme.altoPie
        color: Theme.fondo

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.separador
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.espacioL
            anchors.rightMargin: Theme.espacioL
            spacing: Theme.espacioS

            Label {
                text: qsTr("Al crearla, la app la envía al SAT con la e.firma del perfil.")
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            BotonAccion {
                objectName: "botonCancelarNueva"
                text: qsTr("Cancelar")
                descripcion: qsTr("Volver a la lista sin crear la solicitud")
                onClicked: pagina.regresar()
            }
            BotonAccion {
                id: botonEnviar
                objectName: "botonCrearSolicitud"
                variante: "primario"
                text: qsTr("Crear solicitud")
                descripcion: pagina.formulario.canSubmit
                             ? qsTr("Crear la solicitud con los datos capturados")
                             : qsTr("Completa el formulario para crear la solicitud")
                cargando: pagina.formulario.ocupado
                enabled: !pagina.formulario.ocupado && !pagina.bloqueada
                onClicked: pagina.enviar()
            }
        }
    }

    // Sin perfiles listos (activo + e.firma Lista): EstadoVacio hacia Perfiles SAT.
    EstadoVacio {
        id: sinPerfiles
        objectName: "seccionSinPerfiles"
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
        visible: pagina.formulario.sinPerfiles
        variante: "sinPerfiles"
        titulo: qsTr("No hay perfiles SAT listos para solicitudes")
        descripcion: qsTr("Un perfil necesita estar activo y tener su e.firma registrada y vigente.")
        textoAccion: qsTr("Administrar perfiles SAT")
        accionPrimaria: true
        objectNameTitulo: "mensajeSinPerfiles"
        objectNameAccion: "botonAdministrarPerfiles"
        onAccionSolicitada: pagina.app.mostrarPerfiles()
    }

    ScrollView {
        id: desplazamiento
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        visible: !pagina.formulario.sinPerfiles

        ColumnLayout {
            id: columna
            x: Math.max(Theme.espacioL, (desplazamiento.availableWidth - width) / 2)
            width: Math.min(Theme.anchoFormulario, desplazamiento.availableWidth - 2 * Theme.espacioL)
            spacing: Theme.espacioS

            Item { implicitHeight: Theme.espacioXl - Theme.espacioS }

            // Errores sin campo: bloqueo por duplicado, persistencia, carga.
            AvisoEnLinea {
                objectName: "mensajeError"
                visible: pagina.formulario.errorMessage.length > 0 && pagina.campoError.length === 0
                variante: "error"
                titulo: pagina.formulario.errorMessage
                textoAccion: pagina.bloqueada ? qsTr("Ver solicitud existente") : ""
                objectNameAccion: "botonVerSolicitudExistente"
                Layout.fillWidth: true
                Layout.bottomMargin: Theme.espacioS
                onAccionSolicitada: pagina.app.abrirDetalle(pagina.formulario.solicitudExistenteId)
            }

            // ---- Grupo Solicitud ----
            Label {
                text: qsTr("Solicitud")
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.etiqueta.size
                font.weight: Font.DemiBold
                leftPadding: Theme.espacioM
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: grupoSolicitud.implicitHeight
                radius: Theme.radioTarjeta
                color: Theme.superficie
                border.width: 1
                border.color: Theme.separador

                ColumnLayout {
                    id: grupoSolicitud
                    width: parent.width
                    spacing: 0

                    CampoFormulario {
                        objectName: "filaPerfil"
                        etiqueta: qsTr("Perfil SAT")
                        ayuda: qsTr("Solo aparecen perfiles activos con la e.firma lista.")
                        error: pagina.campoError === "perfil" ? pagina.formulario.errorMessage : ""

                        CampoCombo {
                            id: selectorPerfil
                            objectName: "campoPerfil"
                            Layout.fillWidth: true
                            focusPolicy: Qt.StrongFocus
                            enabled: !pagina.formulario.sinPerfiles
                            conError: pagina.campoError === "perfil"
                            model: pagina.formulario.perfilesDisponibles
                            textRole: "etiqueta"
                            valueRole: "id"
                            // Depende de count para reevaluarse cuando llegan los perfiles.
                            currentIndex: pagina.formulario.perfilesDisponibles.count > 0
                                          ? pagina.formulario.perfilesDisponibles.filaDe(pagina.formulario.perfilId)
                                          : -1
                            displayText: pagina.formulario.cargandoPerfiles && count === 0
                                         ? qsTr("Cargando perfiles…")
                                         : (currentIndex < 0 ? qsTr("Selecciona un perfil SAT") : currentText)
                            onActivated: (indice) => { pagina.formulario.perfilId = valueAt(indice) }
                            Accessible.name: qsTr("Perfil SAT")
                            Accessible.description: pagina.campoError === "perfil" ? pagina.formulario.errorMessage
                                                                                   : displayText
                        }
                    }
                    CampoFormulario {
                        objectName: "filaTipoDescarga"
                        etiqueta: qsTr("Tipo de descarga")

                        SelectorSegmentado {
                            objectName: "campoTipoDescarga"
                            nombreAccesible: qsTr("Tipo de descarga")
                            valor: pagina.formulario.tipoDescarga
                            opciones: [
                                { clave: "Emitidos", texto: qsTr("Emitidos"), icono: "arrow-up-right" },
                                { clave: "Recibidos", texto: qsTr("Recibidos"), icono: "arrow-down-left" }
                            ]
                            onActivado: (clave) => { pagina.formulario.tipoDescarga = clave }
                        }
                    }
                    CampoFormulario {
                        objectName: "filaPeriodo"
                        etiqueta: qsTr("Periodo")
                        ayuda: qsTr("Formato AAAA-MM-DD. Incluye ambos días.")
                        error: pagina.campoError === "fechaInicial" || pagina.campoError === "fechaFinal"
                               ? pagina.formulario.errorMessage : ""
                        separador: false

                        RowLayout {
                            spacing: Theme.espacioS
                            CampoTexto {
                                id: campoFechaInicial
                                objectName: "campoFechaInicial"
                                Layout.preferredWidth: 132
                                text: pagina.formulario.fechaInicial
                                placeholderText: "2026-01-01"
                                inputMethodHints: Qt.ImhDate
                                conError: pagina.campoError === "fechaInicial"
                                onTextEdited: pagina.formulario.fechaInicial = text
                                onAccepted: pagina.enviar()
                                Accessible.name: qsTr("Fecha inicial")
                            }
                            Label {
                                text: qsTr("a")
                                color: Theme.textoSecundario
                                font.family: Theme.familia
                                font.pixelSize: Theme.cuerpo.size
                                Accessible.ignored: true
                            }
                            CampoTexto {
                                id: campoFechaFinal
                                objectName: "campoFechaFinal"
                                Layout.preferredWidth: 132
                                text: pagina.formulario.fechaFinal
                                placeholderText: "2026-01-31"
                                inputMethodHints: Qt.ImhDate
                                conError: pagina.campoError === "fechaFinal"
                                onTextEdited: pagina.formulario.fechaFinal = text
                                onAccepted: pagina.enviar()
                                Accessible.name: qsTr("Fecha final")
                            }
                        }
                    }
                }
            }

            // ---- Grupo Filtros opcionales ----
            Label {
                text: qsTr("Filtros opcionales")
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.etiqueta.size
                font.weight: Font.DemiBold
                leftPadding: Theme.espacioM
                Layout.topMargin: Theme.espacioL
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: grupoFiltros.implicitHeight
                radius: Theme.radioTarjeta
                color: Theme.superficie
                border.width: 1
                border.color: Theme.separador

                ColumnLayout {
                    id: grupoFiltros
                    width: parent.width
                    spacing: 0

                    CampoFormulario {
                        objectName: "filaRfcContraparte"
                        etiqueta: qsTr("RFC contraparte")
                        error: pagina.campoError === "rfcContraparte" ? pagina.formulario.errorMessage : ""

                        CampoTexto {
                            id: campoRfc
                            objectName: "campoRfcContraparte"
                            Layout.preferredWidth: 240
                            text: pagina.formulario.rfcContraparte
                            placeholderText: qsTr("Cualquiera")
                            maximumLength: 13
                            conError: pagina.campoError === "rfcContraparte"
                            onTextEdited: pagina.formulario.rfcContraparte = text
                            onAccepted: pagina.enviar()
                            Accessible.name: qsTr("RFC contraparte, opcional")
                        }
                    }
                    CampoFormulario {
                        objectName: "filaTipoComprobante"
                        etiqueta: qsTr("Tipo de comprobante")
                        error: pagina.campoError === "tipoComprobante" ? pagina.formulario.errorMessage : ""

                        CampoCombo {
                            id: selectorComprobante
                            objectName: "campoTipoComprobante"
                            Layout.preferredWidth: 240
                            focusPolicy: Qt.StrongFocus
                            conError: pagina.campoError === "tipoComprobante"
                            textRole: "texto"
                            valueRole: "clave"
                            model: [
                                { clave: "", texto: qsTr("Todos") },
                                { clave: "I", texto: qsTr("I - Ingreso") },
                                { clave: "E", texto: qsTr("E - Egreso") },
                                { clave: "T", texto: qsTr("T - Traslado") },
                                { clave: "N", texto: qsTr("N - Nómina") },
                                { clave: "P", texto: qsTr("P - Pago") }
                            ]
                            // Depende de count (ver T012): al crearse, indexOfValue() se
                            // evalua antes de que el modelo este listo.
                            currentIndex: count > 0 ? indexOfValue(pagina.formulario.tipoComprobante) : -1
                            onActivated: (indice) => { pagina.formulario.tipoComprobante = valueAt(indice) }
                            Accessible.name: qsTr("Tipo de comprobante, opcional")
                            Accessible.description: displayText
                        }
                    }
                    CampoFormulario {
                        objectName: "filaComplemento"
                        etiqueta: qsTr("Complemento")
                        error: pagina.campoError === "complemento" ? pagina.formulario.errorMessage : ""
                        separador: false

                        CampoTexto {
                            id: campoComplemento
                            objectName: "campoComplemento"
                            Layout.preferredWidth: 240
                            text: pagina.formulario.complemento
                            placeholderText: qsTr("Cualquiera")
                            conError: pagina.campoError === "complemento"
                            onTextEdited: pagina.formulario.complemento = text
                            onAccepted: pagina.enviar()
                            Accessible.name: qsTr("Complemento, opcional")
                        }
                    }
                }
            }

            Item { implicitHeight: Theme.espacioXl }
        }
    }
}
