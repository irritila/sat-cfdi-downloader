pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import SatCfdiDownloader

import "DatosSinteticos.js" as Datos

// Historial de descargas del perfil (D6, D14): una fila por descarga del
// usuario (emitidas + recibidas), mas reciente primero. Acciones: Ver
// facturas, Reintentar y Ver detalles tecnicos. "Descarga anterior" (previas a
// T023) solo lleva diagnostico.
Item {
    id: pagina

    required property VmPrototipo vm

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        EncabezadoPagina {
            titulo: qsTr("Historial")
            subtitulo: pagina.vm.perfil !== null ? pagina.vm.perfil.rfc + " — " + pagina.vm.perfil.nombre : ""
            mostrarRegresar: true
            textoRegresar: qsTr("Facturas")
            descripcionRegresar: qsTr("Regresar a Facturas (Escape)")
            Layout.fillWidth: true
            onRegresarSolicitado: pagina.vm.irA("facturas")
        }

        Rectangle {
            color: Theme.superficie
            radius: Theme.radioTarjeta
            border.width: 1
            border.color: Theme.separador
            clip: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.espacioL

            ListView {
                id: lista
                anchors.fill: parent
                anchors.margins: 1
                model: pagina.vm.historialPerfil
                clip: true
                focus: true
                boundsBehavior: Flickable.StopAtBounds
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Historial de descargas")
                ScrollBar.vertical: ScrollBar { }

                delegate: Rectangle {
                    id: fila
                    required property var modelData
                    required property int index
                    readonly property var r: Datos.resultadoHistorial(fila.modelData.resultado)
                    readonly property bool anterior: fila.modelData.resultado === "Anterior"
                    // Alto: contenido + padding vertical espacioM (minimo altoFila).
                    // Fondo transparente: la tarjeta ya es superficie y asi la
                    // primera fila no tapa las esquinas redondeadas.
                    width: lista.width
                    implicitHeight: Math.max(Theme.altoFila, contenido.implicitHeight + 2 * Theme.espacioM)
                    color: "transparent"
                    Accessible.role: Accessible.ListItem
                    Accessible.name: [Datos.tituloMesAnio(fila.modelData.anio, fila.modelData.mes), fila.r.texto,
                                      fila.modelData.fecha].join(", ")

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.separador
                    }

                    RowLayout {
                        id: contenido
                        anchors.fill: parent
                        anchors.leftMargin: Theme.espacioL
                        anchors.rightMargin: Theme.espacioL
                        spacing: Theme.espacioM

                        // Columna de mes de ancho fijo (como las columnas de
                        // FilaCfdi): los badges de resultado quedan alineados.
                        // 196 cabe "Descarga del 10 may 2026, 13:05"; en la lista
                        // angosta (ventana de 720) baja a 150 y la fecha se elide
                        // para que la fila con Reintentar no desborde.
                        ColumnLayout {
                            readonly property int ancho: lista.width < 720 ? 150 : 196
                            spacing: Theme.espacioXs / 2
                            Layout.preferredWidth: ancho
                            Layout.minimumWidth: ancho
                            Layout.maximumWidth: ancho
                            Label {
                                text: Datos.tituloMesAnio(fila.modelData.anio, fila.modelData.mes)
                                color: Theme.texto
                                font.pixelSize: Theme.cuerpoFuerte.size
                                font.weight: Font.DemiBold
                            }
                            Label {
                                text: qsTr("Descarga del %1").arg(fila.modelData.fecha)
                                color: Theme.textoSecundario
                                font.pixelSize: Theme.leyenda.size
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        ColumnLayout {
                            spacing: Theme.espacioXs
                            Layout.fillWidth: true
                            EstadoBadge {
                                texto: fila.r.texto
                                tonoDirecto: fila.r.tono
                                iconoDirecto: fila.r.icono
                                contexto: qsTr("Resultado")
                            }
                            Label {
                                text: [fila.modelData.resultado === "SinFacturas" ? qsTr("El SAT no tenía facturas de este mes")
                                       : qsTr("%1 emitidas, %2 recibidas").arg(fila.modelData.emitidas).arg(fila.modelData.recibidas),
                                       fila.modelData.detalle].filter(t => t.length > 0).join(" · ")
                                color: Theme.textoSecundario
                                font.pixelSize: Theme.leyenda.size
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        BotonAccion {
                            visible: !fila.anterior
                            text: qsTr("Ver facturas")
                            compacto: true
                            onClicked: pagina.vm.abrirMes(pagina.vm.perfil.rfc, fila.modelData.anio, fila.modelData.mes)
                        }
                        BotonAccion {
                            visible: fila.modelData.resultado === "Atencion"
                            text: qsTr("Reintentar")
                            compacto: true
                            onClicked: {
                                pagina.vm.abrirMes(pagina.vm.perfil.rfc, fila.modelData.anio, fila.modelData.mes)
                                pagina.vm.iniciarDescarga("reintentar")
                            }
                        }
                        BotonAccion {
                            text: qsTr("Ver detalles técnicos")
                            variante: "plano"
                            compacto: true
                            onClicked: pagina.vm.abrirDiagnostico({ rfc: pagina.vm.perfil.rfc, anio: fila.modelData.anio,
                                                                    mes: fila.modelData.mes })
                        }
                    }
                }
            }
        }
    }
}
