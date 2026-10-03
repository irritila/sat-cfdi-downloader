pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "Etiquetas.js" as Etiquetas

// Detalle de una solicitud cargada por id. Estados: Cargando, Error,
// NoEncontrada y ConDatos. Con datos separa solicitud, filtros, estado
// (local/SAT y codigos), paquetes e historial. "Eliminar solicitud" pide
// confirmacion; al terminar se regresa a la lista. Teclado: foco inicial en
// "Regresar"; Tab llega a "Eliminar solicitud"; Escape regresa.
Page {
    id: pagina
    objectName: "paginaDetalleSolicitud"

    required property AppViewModel app
    required property SolicitudDetailViewModel detalle

    readonly property bool sinEstadoSat: pagina.detalle.estadoSat === null
    readonly property bool conDatos: pagina.detalle.cargada

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Detalle de solicitud")

    // Foco inicial diferido: la pagina ya esta en la ventana.
    Component.onCompleted: Qt.callLater(pagina.enfocarInicial)

    // Diferido: no quita el foco a un dialogo ya abierto.
    function enfocarInicial() {
        if (dialogoEliminar.visible)
            return
        encabezado.botonRegresar.forceActiveFocus(Qt.TabFocusReason)
    }

    function regresar() {
        pagina.app.mostrarLista()
    }

    function enfocarEliminar() {
        if (botonEliminar.visible)
            botonEliminar.forceActiveFocus(Qt.TabFocusReason)
    }

    function textoOpcional(valor) {
        return valor === null || valor === undefined || valor === "" ? "-" : String(valor)
    }

    Keys.onEscapePressed: regresar()

    header: EncabezadoPagina {
        id: encabezado
        titulo: qsTr("Detalle de solicitud")
        mostrarRegresar: true
        onRegresarSolicitado: pagina.regresar()

        BotonAccion {
            id: botonEliminar
            objectName: "botonEliminarSolicitud"
            visible: pagina.conDatos
            enabled: !pagina.detalle.eliminando
            text: qsTr("Eliminar solicitud")
            descripcion: qsTr("Eliminar la solicitud de este equipo; no modifica nada en el SAT")
            onClicked: dialogoEliminar.open()
        }
    }

    DialogoConfirmacion {
        id: dialogoEliminar
        objectName: "dialogoEliminar"
        prefijoNombre: "dialogoEliminar"
        title: qsTr("Eliminar solicitud")
        mensaje: qsTr("La solicitud se eliminara de esta aplicacion. No se modifica nada en el SAT.")
        textoConfirmar: qsTr("Eliminar")
        textoCancelar: qsTr("Cancelar")
        onConfirmado: pagina.detalle.eliminar()
        onCancelado: Qt.callLater(pagina.enfocarEliminar)
    }

    // Estado: cargando
    ColumnLayout {
        objectName: "estadoCargandoDetalle"
        anchors.centerIn: parent
        visible: pagina.detalle.estado === SolicitudDetailViewModel.Cargando && !pagina.conDatos
        spacing: 8
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("Cargando solicitud")

        BusyIndicator {
            running: parent.visible
            Layout.alignment: Qt.AlignHCenter
            Accessible.ignored: true
        }
        Label {
            text: qsTr("Cargando solicitud...")
            Layout.alignment: Qt.AlignHCenter
            Accessible.ignored: true
        }
    }

    // Estado: no encontrada
    ColumnLayout {
        objectName: "estadoNoEncontrada"
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420)
        visible: pagina.detalle.estado === SolicitudDetailViewModel.NoEncontrada
        spacing: 8
        Accessible.role: Accessible.AlertMessage
        Accessible.name: qsTr("Solicitud no encontrada. Pudo haber sido eliminada.")

        Label {
            text: qsTr("Solicitud no encontrada")
            font.pixelSize: 18
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Accessible.ignored: true
        }
        Label {
            text: qsTr("La solicitud no existe o fue eliminada.")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }

    // Estado: error
    ColumnLayout {
        objectName: "estadoErrorDetalle"
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420)
        visible: pagina.detalle.estado === SolicitudDetailViewModel.Error
        spacing: 12

        Label {
            objectName: "errorDetalle"
            text: pagina.detalle.errorMessage
            color: "#b00020"
            font.bold: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Accessible.role: Accessible.AlertMessage
            Accessible.name: qsTr("Error: %1").arg(text)
        }
        BotonAccion {
            objectName: "botonReintentarDetalle"
            text: qsTr("Reintentar")
            Layout.alignment: Qt.AlignHCenter
            onClicked: pagina.detalle.recargar()
        }
    }

    // Estado: con datos
    ScrollView {
        id: desplazamiento
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        visible: pagina.conDatos

        ColumnLayout {
            width: desplazamiento.availableWidth
            spacing: 12

            Item { implicitHeight: 4 }

            Label {
                objectName: "errorEliminacion"
                visible: text.length > 0
                text: pagina.detalle.errorEliminacion
                color: "#b00020"
                font.bold: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Accessible.role: Accessible.AlertMessage
                Accessible.name: qsTr("Error: %1").arg(text)
            }

            GroupBox {
                objectName: "seccionMetadata"
                title: qsTr("Solicitud")
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Accessible.role: Accessible.Grouping
                Accessible.name: title

                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 6

                    CampoDetalle { etiqueta: qsTr("Identificador local"); valor: pagina.detalle.solicitudId }
                    CampoDetalle { objectName: "campoPerfilRfc"; etiqueta: qsTr("Perfil SAT (RFC)"); valor: pagina.detalle.perfilRfc }
                    CampoDetalle { etiqueta: qsTr("Tipo de descarga"); valor: Etiquetas.tipoDescarga(pagina.detalle.tipoDescarga) }
                    CampoDetalle { etiqueta: qsTr("Creada"); valor: Etiquetas.fechaHora(pagina.detalle.creadaEn) }
                }
            }

            GroupBox {
                objectName: "seccionFiltros"
                title: qsTr("Filtros")
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Accessible.role: Accessible.Grouping
                Accessible.name: title

                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 6

                    CampoDetalle { etiqueta: qsTr("Fecha inicial"); valor: pagina.detalle.fechaInicialSat || pagina.detalle.fechaInicial }
                    CampoDetalle { etiqueta: qsTr("Fecha final"); valor: pagina.detalle.fechaFinalSat || pagina.detalle.fechaFinal }
                    CampoDetalle {
                        objectName: "campoContrapartes"
                        etiqueta: qsTr("RFC contraparte")
                        valor: pagina.detalle.rfcContrapartes.length > 0
                               ? pagina.detalle.rfcContrapartes.join(", ")
                               : pagina.detalle.rfcContraparte
                    }
                    CampoDetalle {
                        objectName: "campoTipoComprobante"
                        etiqueta: qsTr("Tipo de comprobante")
                        valor: Etiquetas.tipoComprobante(pagina.detalle.tipoComprobante)
                    }
                    CampoDetalle { etiqueta: qsTr("Complemento"); valor: pagina.detalle.complemento }
                }
            }

            GroupBox {
                objectName: "seccionEstados"
                title: qsTr("Estado")
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Accessible.role: Accessible.Grouping
                Accessible.name: title

                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 8

                    GridLayout {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 8
                        Layout.fillWidth: true

                        Label { text: qsTr("Estado local"); Layout.preferredWidth: 160; opacity: 0.75; Accessible.ignored: true }
                        EstadoBadge {
                            objectName: "badgeEstadoLocal"
                            clave: pagina.detalle.estadoLocal
                            texto: Etiquetas.estadoLocal(pagina.detalle.estadoLocal)
                            contexto: qsTr("Estado local")
                        }

                        Label { text: qsTr("Estado SAT"); Layout.preferredWidth: 160; opacity: 0.75; Accessible.ignored: true }
                        EstadoBadge {
                            objectName: "badgeEstadoSat"
                            clave: pagina.sinEstadoSat ? "" : pagina.detalle.estadoSat
                            texto: Etiquetas.estadoSat(pagina.detalle.estadoSat)
                            contexto: qsTr("Estado SAT")
                        }

                        Label { text: qsTr("Resumen"); Layout.preferredWidth: 160; opacity: 0.75; Accessible.ignored: true }
                        EstadoBadge {
                            objectName: "badgeEstadoResumen"
                            clave: pagina.detalle.estadoResumen
                            contexto: qsTr("Estado resumido")
                        }
                    }

                    CampoDetalle { etiqueta: qsTr("Id solicitud SAT"); valor: pagina.detalle.idSolicitudSat }
                    CampoDetalle {
                        objectName: "campoCodigoSolicitud"
                        etiqueta: qsTr("Codigo de solicitud SAT")
                        valor: pagina.detalle.codEstatusSolicitud
                               + (pagina.detalle.mensajeSolicitudSat ? " - " + pagina.detalle.mensajeSolicitudSat : "")
                    }
                    CampoDetalle {
                        etiqueta: qsTr("Codigo de verificacion SAT")
                        valor: pagina.detalle.codigoEstadoSolicitud
                               + (pagina.detalle.mensajeVerificacionSat ? " - " + pagina.detalle.mensajeVerificacionSat : "")
                    }
                    CampoDetalle { etiqueta: qsTr("CFDI reportados"); valor: pagina.textoOpcional(pagina.detalle.numeroCfdi) }
                    CampoDetalle { etiqueta: qsTr("Enviada"); valor: Etiquetas.fechaHora(pagina.detalle.enviadaEn) }
                    CampoDetalle { etiqueta: qsTr("Ultima verificacion"); valor: Etiquetas.fechaHora(pagina.detalle.ultimaVerificacionEn) }
                    CampoDetalle { etiqueta: qsTr("Ultimo error"); valor: pagina.detalle.ultimoError }
                }
            }

            GroupBox {
                objectName: "seccionPaquetes"
                title: qsTr("Paquetes (%1)").arg(pagina.detalle.totalPaquetes)
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Accessible.role: Accessible.Grouping
                Accessible.name: title

                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 8

                    Label {
                        objectName: "sinPaquetes"
                        visible: pagina.detalle.totalPaquetes === 0
                        text: qsTr("Sin paquetes registrados.")
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Repeater {
                        model: pagina.detalle.paquetes

                        delegate: RowLayout {
                            id: paquete
                            required property var modelData
                            readonly property string textoEstado: Etiquetas.estadoDescarga(modelData.estadoDescarga)

                            Layout.fillWidth: true
                            spacing: 8
                            Accessible.role: Accessible.ListItem
                            Accessible.name: qsTr("Paquete %1, %2").arg(modelData.idPaqueteSat).arg(textoEstado)

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: paquete.modelData.idPaqueteSat
                                    font.bold: true
                                    wrapMode: Text.WrapAnywhere
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }
                                Label {
                                    text: {
                                        const d = paquete.modelData
                                        let partes = [qsTr("Disponible %1").arg(Etiquetas.fechaHora(d.disponibleEn))]
                                        if (d.descargadoEn !== null)
                                            partes.push(qsTr("Descargado %1").arg(Etiquetas.fechaHora(d.descargadoEn)))
                                        if (d.vencidoEn !== null)
                                            partes.push(qsTr("Vencido %1").arg(Etiquetas.fechaHora(d.vencidoEn)))
                                        if (d.codigoDescargaSat !== "")
                                            partes.push(qsTr("Codigo SAT %1").arg(d.codigoDescargaSat))
                                        return partes.join(" · ")
                                    }
                                    opacity: 0.75
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }
                            }
                            EstadoBadge {
                                clave: paquete.modelData.estadoDescarga
                                texto: paquete.textoEstado
                                contexto: qsTr("Estado de descarga")
                                Layout.alignment: Qt.AlignTop
                            }
                        }
                    }
                }
            }

            GroupBox {
                objectName: "seccionHistorial"
                title: qsTr("Historial (%1)").arg(pagina.detalle.logs.length)
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16
                Accessible.role: Accessible.Grouping
                Accessible.name: title

                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 6

                    Label {
                        visible: pagina.detalle.logs.length === 0
                        text: qsTr("Sin eventos registrados.")
                        Layout.fillWidth: true
                    }

                    Repeater {
                        model: pagina.detalle.logs

                        delegate: Label {
                            id: evento
                            required property var modelData
                            required property int index
                            objectName: "eventoLog_" + index

                            readonly property string resumen: {
                                const l = evento.modelData
                                let partes = [Etiquetas.fechaHora(l.creadoEn), Etiquetas.eventoLog(l.tipoEvento)]
                                const origen = Etiquetas.origenLog(l.origen)
                                if (origen !== "")
                                    partes.push(qsTr("origen %1").arg(origen))
                                if (l.codigoSat !== "")
                                    partes.push(qsTr("codigo SAT %1").arg(l.codigoSat))
                                if (l.mensajeSat !== "")
                                    partes.push(l.mensajeSat)
                                return partes.join(" · ")
                            }

                            text: resumen
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Accessible.role: Accessible.ListItem
                            Accessible.name: resumen
                        }
                    }
                }
            }
        }
    }
}
