pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import SatCfdiDownloader

// Perfiles SAT SIMULADO: solo lo necesario para recorrer el prototipo (crear
// los perfiles de ejemplo y "registrar" la e.firma). La pantalla real es
// PerfilesSatPage y no cambia en T023.
Item {
    id: pagina

    required property VmPrototipo vm

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        EncabezadoPagina {
            titulo: qsTr("Perfiles SAT")
            mostrarRegresar: true
            textoRegresar: qsTr("Facturas")
            descripcionRegresar: qsTr("Regresar a Facturas (Escape)")
            Layout.fillWidth: true
            onRegresarSolicitado: pagina.vm.irA("facturas")

            BotonAccion {
                visible: !pagina.vm.hayPerfiles
                text: qsTr("Crear perfiles de ejemplo")
                icono: "plus"
                onClicked: pagina.vm.crearPerfilesEjemplo()
            }
            BotonAccion {
                text: qsTr("Nuevo perfil")
                variante: "primario"
                icono: "plus"
                Accessible.description: qsTr("Agregar un perfil SAT")
                onClicked: dialogoNuevo.abrir()
            }
        }

        // Alta simulada: la pantalla real (PerfilesSatPage) ya tiene "Nuevo
        // perfil" con validacion completa y no cambia en T023.
        Dialog {
            id: dialogoNuevo
            title: qsTr("Nuevo perfil")
            modal: true
            anchors.centerIn: Overlay.overlay
            width: Math.min(420, pagina.width - 2 * Theme.espacioL)
            padding: Theme.espacioL
            standardButtons: Dialog.Cancel | Dialog.Ok
            function abrir() {
                campoRfc.text = ""
                campoNombre.text = ""
                open()
                campoRfc.forceActiveFocus()
            }
            onAccepted: pagina.vm.agregarPerfil(campoRfc.text, campoNombre.text)
            Component.onCompleted: standardButton(Dialog.Ok).text = qsTr("Agregar")

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.espacioS
                Label { text: qsTr("RFC"); color: Theme.textoSecundario; font.pixelSize: Theme.etiqueta.size }
                CampoTexto {
                    id: campoRfc
                    placeholderText: "XAXX010101000"
                    Layout.fillWidth: true
                }
                Label { text: qsTr("Nombre"); color: Theme.textoSecundario; font.pixelSize: Theme.etiqueta.size; Layout.topMargin: Theme.espacioS }
                CampoTexto {
                    id: campoNombre
                    placeholderText: qsTr("Contribuyente de ejemplo")
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("La e.firma se registra después, desde la fila del perfil.")
                    color: Theme.textoSecundario
                    font.pixelSize: Theme.leyenda.size
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.espacioS
                }
            }
        }

        EstadoVacio {
            visible: !pagina.vm.hayPerfiles
            variante: "sinPerfiles"
            titulo: qsTr("Sin perfiles SAT")
            descripcion: qsTr("En el prototipo, «Crear perfiles de ejemplo» agrega XAXX010101000 (con e.firma) y XEXX010101000 (sin e.firma).")
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.espacioXxl
        }

        Repeater {
            model: pagina.vm.perfiles
            delegate: Rectangle {
                id: fila
                required property var modelData
                required property int index
                color: Theme.superficie
                radius: Theme.radioTarjeta
                border.width: 1
                border.color: Theme.separador
                Layout.fillWidth: true
                Layout.leftMargin: Theme.espacioL
                Layout.rightMargin: Theme.espacioL
                // Primera tarjeta a espacioL del encabezado (margen de pagina);
                // entre tarjetas espacioM. Alto: contenido + padding espacioM.
                Layout.topMargin: fila.index === 0 ? Theme.espacioL : Theme.espacioM
                implicitHeight: Math.max(Theme.altoFila, filaPerfil.implicitHeight + 2 * Theme.espacioM)
                RowLayout {
                    id: filaPerfil
                    anchors.fill: parent
                    anchors.leftMargin: Theme.espacioL
                    anchors.rightMargin: Theme.espacioL
                    spacing: Theme.espacioM
                    ColumnLayout {
                        spacing: Theme.espacioXs / 2
                        Layout.fillWidth: true
                        Label {
                            text: fila.modelData.rfc
                            color: Theme.texto
                            font.pixelSize: Theme.cuerpoFuerte.size
                            font.weight: Font.DemiBold
                        }
                        Label {
                            text: fila.modelData.nombre
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.leyenda.size
                        }
                    }
                    EstadoBadge {
                        eFirma: true
                        clave: fila.modelData.eFirma
                        texto: fila.modelData.eFirma === "Lista" ? qsTr("e.firma lista") : qsTr("Sin e.firma")
                        contexto: qsTr("e.firma")
                    }
                    BotonAccion {
                        visible: fila.modelData.eFirma !== "Lista"
                        text: qsTr("Registrar e.firma…")
                        compacto: true
                        onClicked: pagina.vm.registrarEFirma(fila.modelData.rfc)
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
