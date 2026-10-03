pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "Etiquetas.js" as Etiquetas

// Detalle de una solicitud cargada por id. Separa metadata, estado local,
// estado SAT y paquetes. Teclado: foco inicial en "Regresar"; Escape regresa.
Page {
    id: pagina
    objectName: "paginaDetalleSolicitud"

    required property AppViewModel app
    required property SolicitudDetailViewModel detalle

    readonly property bool sinEstadoSat: pagina.detalle.estadoSat === null

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Detalle de solicitud")

    // Foco inicial diferido: la pagina ya esta en la ventana.
    Component.onCompleted: Qt.callLater(pagina.enfocarInicial)

    function enfocarInicial() {
        encabezado.botonRegresar.forceActiveFocus(Qt.TabFocusReason)
    }

    function regresar() {
        pagina.app.mostrarLista()
    }

    Keys.onEscapePressed: regresar()

    header: EncabezadoPagina {
        id: encabezado
        titulo: qsTr("Detalle de solicitud")
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
            spacing: 12

            Item { implicitHeight: 4 }

            BusyIndicator {
                running: pagina.detalle.cargando
                visible: running
                Layout.alignment: Qt.AlignHCenter
                Accessible.name: qsTr("Cargando solicitud")
            }

            Label {
                objectName: "errorDetalle"
                visible: text.length > 0
                text: pagina.detalle.errorMessage
                color: "#b00020"
                font.bold: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Accessible.role: Accessible.AlertMessage
                Accessible.name: qsTr("Error: %1").arg(text)
            }

            // Metadata
            GroupBox {
                objectName: "seccionMetadata"
                visible: pagina.detalle.cargada
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
                    CampoDetalle { etiqueta: qsTr("Fecha inicial"); valor: pagina.detalle.fechaInicial }
                    CampoDetalle { etiqueta: qsTr("Fecha final"); valor: pagina.detalle.fechaFinal }
                    CampoDetalle { etiqueta: qsTr("RFC contraparte"); valor: pagina.detalle.rfcContraparte }
                    CampoDetalle { etiqueta: qsTr("Creada"); valor: Etiquetas.fechaHora(pagina.detalle.creadaEn) }
                }
            }

            // Estados: local y SAT por separado
            GroupBox {
                objectName: "seccionEstados"
                visible: pagina.detalle.cargada
                title: qsTr("Estado")
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Accessible.role: Accessible.Grouping
                Accessible.name: title

                GridLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

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
            }

            // Paquetes
            GroupBox {
                objectName: "seccionPaquetes"
                visible: pagina.detalle.cargada
                title: qsTr("Paquetes (%1)").arg(pagina.detalle.totalPaquetes)
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16
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
                                    text: paquete.modelData.descargadoEn !== null
                                          ? qsTr("Disponible %1 · Descargado %2")
                                                .arg(Etiquetas.fechaHora(paquete.modelData.disponibleEn))
                                                .arg(Etiquetas.fechaHora(paquete.modelData.descargadoEn))
                                          : qsTr("Disponible %1").arg(Etiquetas.fechaHora(paquete.modelData.disponibleEn))
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
        }
    }
}
