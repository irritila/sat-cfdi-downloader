pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import SatCfdiDownloader

import "DatosSinteticos.js" as Datos

// Diagnostico SIMULADO (D10): el unico lugar del prototipo con los terminos
// del servicio web del SAT (solicitud, paquete, verificacion). Con foco en un
// mes muestra sus solicitudes; sin foco ("Ir > Diagnóstico") lista todas las
// del perfil. Usa EstadoBadge con las claves reales de solicitud y paquete.
Item {
    id: pagina

    required property VmPrototipo vm
    readonly property var foco: pagina.vm.diagnosticoFoco

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        EncabezadoPagina {
            titulo: qsTr("Diagnóstico")
            subtitulo: pagina.foco !== null
                       ? qsTr("Solicitudes de descarga masiva de %1 · %2").arg(Datos.nombreMesAnio(pagina.foco.anio, pagina.foco.mes)).arg(pagina.foco.rfc)
                       : qsTr("Todas las solicitudes de descarga masiva del perfil")
            mostrarRegresar: true
            textoRegresar: pagina.vm.paginaPrevia === "historial" ? qsTr("Historial") : qsTr("Facturas")
            descripcionRegresar: qsTr("Regresar (Escape)")
            Layout.fillWidth: true
            onRegresarSolicitado: pagina.vm.irA(pagina.vm.paginaPrevia)

            BotonAccion {
                visible: pagina.foco !== null
                text: qsTr("Todas las solicitudes")
                compacto: true
                onClicked: pagina.vm.diagnosticoFoco = null
            }
        }

        AvisoEnLinea {
            variante: "neutro"
            anunciar: false
            titulo: qsTr("Vista técnica")
            descripcion: qsTr("Muestra las solicitudes, paquetes y verificaciones del servicio web del SAT que hay detrás de cada descarga.")
            Layout.fillWidth: true
            // Mismo ritmo que Facturas: espacioL desde el encabezado y
            // espacioM entre bloques.
            Layout.leftMargin: Theme.espacioL
            Layout.rightMargin: Theme.espacioL
            Layout.topMargin: Theme.espacioL
        }

        Rectangle {
            color: Theme.superficie
            radius: Theme.radioTarjeta
            border.width: 1
            border.color: Theme.separador
            clip: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.espacioL
            Layout.rightMargin: Theme.espacioL
            Layout.topMargin: Theme.espacioM
            Layout.bottomMargin: Theme.espacioL

            EstadoVacio {
                anchors.centerIn: parent
                visible: pagina.vm.diagnostico.length === 0
                titulo: qsTr("Sin solicitudes para este mes")
                descripcion: qsTr("Todavía no se ha creado ninguna solicitud de descarga masiva.")
            }

            ListView {
                id: lista
                anchors.fill: parent
                anchors.margins: 1
                model: pagina.vm.diagnostico
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Solicitudes")
                ScrollBar.vertical: ScrollBar { }

                delegate: Rectangle {
                    id: fila
                    required property var modelData
                    // Padding vertical espacioM; fondo transparente sobre la
                    // tarjeta superficie para no tapar sus esquinas.
                    width: lista.width
                    implicitHeight: contenido.implicitHeight + 2 * Theme.espacioM
                    color: "transparent"
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.separador
                    }
                    ColumnLayout {
                        id: contenido
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Theme.espacioL
                        anchors.rightMargin: Theme.espacioL
                        spacing: Theme.espacioXs
                        RowLayout {
                            spacing: Theme.espacioM
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("Solicitud %1 · %2").arg(fila.modelData.direccion).arg(fila.modelData.rango)
                                color: Theme.texto
                                font.pixelSize: Theme.cuerpoFuerte.size
                                font.weight: Font.DemiBold
                                Layout.fillWidth: true
                            }
                            EstadoBadge {
                                clave: fila.modelData.estado
                                contexto: qsTr("Estado SAT")
                            }
                        }
                        RowLayout {
                            spacing: Theme.espacioS
                            Label {
                                text: qsTr("Id de solicitud SAT")
                                color: Theme.textoSecundario
                                font.pixelSize: Theme.leyenda.size
                            }
                            Label {
                                text: fila.modelData.id
                                color: Theme.texto
                                font.family: Theme.familiaMono
                                font.pixelSize: Theme.mono.size
                            }
                            BotonCopiar {
                                valor: fila.modelData.id
                                descripcionValor: qsTr("Id de solicitud SAT")
                            }
                        }
                        RowLayout {
                            visible: fila.modelData.paquete.length > 0
                            spacing: Theme.espacioS
                            Label {
                                text: qsTr("Paquete")
                                color: Theme.textoSecundario
                                font.pixelSize: Theme.leyenda.size
                            }
                            Label {
                                text: fila.modelData.paquete
                                color: Theme.texto
                                font.family: Theme.familiaMono
                                font.pixelSize: Theme.mono.size
                            }
                            EstadoBadge {
                                clave: fila.modelData.estadoPaquete
                                texto: fila.modelData.estadoPaquete === "Error" ? qsTr("Error de descarga") : qsTr("Descargado")
                                contexto: qsTr("Paquete")
                            }
                        }
                        Label {
                            visible: text.length > 0
                            text: fila.modelData.estado === "Terminada"
                                  ? [qsTr("Última verificación: 5000 Solicitud recibida con éxito."), fila.modelData.nota].filter(t => t.length > 0).join(" ")
                                  : qsTr("Última verificación: la solicitud sigue en proceso en el SAT.")
                            color: fila.modelData.nota.length > 0 ? Theme.error : Theme.textoSecundario
                            font.pixelSize: Theme.leyenda.size
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            visible: fila.modelData.estadoPaquete === "Error"
                            spacing: Theme.espacioS
                            BotonAccion {
                                text: qsTr("Reintentar paquete")
                                compacto: true
                                onClicked: {
                                    pagina.vm.abrirMes(fila.modelData.rfc, fila.modelData.anio, fila.modelData.mes)
                                    pagina.vm.iniciarDescarga("reintentar")
                                }
                            }
                            BotonAccion {
                                text: qsTr("Mostrar en Finder")
                                variante: "plano"
                                compacto: true
                                enabled: false
                            }
                        }
                    }
                }
            }
        }
    }
}
