pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import SatCfdiDownloader

// Pantalla Facturas (T023.4 en prototipo): encabezado con combo de perfil,
// Historial y Perfiles SAT; lista lateral de meses (combo bajo 760 px, D9);
// franja de estado con una accion; pestanas Emitidas/Recibidas con conteo;
// busqueda, filtro por tipo, lista de CFDI (FilaCfdi real) y PDF simulado.
Item {
    id: pagina

    required property VmPrototipo vm
    readonly property bool angosto: pagina.width < 760

    function enfocarBusqueda() {
        busqueda.forceActiveFocus(Qt.ShortcutFocusReason)
        busqueda.selectAll()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        EncabezadoPagina {
            titulo: qsTr("Facturas")
            Layout.fillWidth: true

            CampoCombo {
                id: comboPerfil
                visible: pagina.vm.hayPerfiles
                model: pagina.vm.perfiles.map(p => p.rfc + " — " + p.nombre)
                readonly property int indiceDeseado: pagina.vm.perfilIndice
                function sincronizar() { comboPerfil.currentIndex = comboPerfil.indiceDeseado }
                currentIndex: comboPerfil.indiceDeseado
                onIndiceDeseadoChanged: comboPerfil.sincronizar()
                onModelChanged: Qt.callLater(comboPerfil.sincronizar)
                Layout.preferredWidth: pagina.angosto ? 200 : 290
                Accessible.name: qsTr("Perfil fiscal")
                onActivated: indice => pagina.vm.seleccionarPerfil(indice)
            }
            BotonAccion {
                text: qsTr("Historial")
                icono: "clock"
                atajo: "⌘3"
                enabled: pagina.vm.hayPerfiles
                onClicked: pagina.vm.irA("historial")
            }
            BotonAccion {
                text: qsTr("Perfiles SAT")
                icono: "person-card"
                atajo: "⌘2"
                onClicked: pagina.vm.irA("perfiles")
            }
        }

        GuiaPrimerUso {
            visible: !pagina.vm.hayPerfiles
            vm: pagina.vm
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        RowLayout {
            visible: pagina.vm.hayPerfiles
            spacing: 0
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListaMeses {
                visible: !pagina.angosto
                vm: pagina.vm
                Layout.preferredWidth: 212
                Layout.fillHeight: true
            }

            ColumnLayout {
                spacing: Theme.espacioM
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Theme.espacioL

                // D9: bajo ~760 px los meses son un combo con anio.
                RowLayout {
                    visible: pagina.angosto
                    spacing: Theme.espacioS
                    Layout.fillWidth: true
                    BotonAccion {
                        variante: "icono"
                        compacto: true
                        icono: "chevron-left"
                        nombreAccesible: qsTr("Año anterior")
                        enabled: pagina.vm.anio > 2025
                        onClicked: pagina.vm.cambiarAnio(-1)
                    }
                    Label {
                        text: pagina.vm.anio
                        color: Theme.texto
                        font.pixelSize: Theme.cuerpoFuerte.size
                        font.weight: Font.DemiBold
                    }
                    BotonAccion {
                        variante: "icono"
                        compacto: true
                        icono: "chevron-right"
                        nombreAccesible: qsTr("Año siguiente")
                        enabled: pagina.vm.anio < 2026
                        onClicked: pagina.vm.cambiarAnio(1)
                    }
                    CampoCombo {
                        model: pagina.vm.mesesDelAnio.filter(m => !m.futuro)
                                   .map(m => m.nombre + " · " + m.estadoTexto + (m.total > 0 ? " · " + m.total : ""))
                        id: comboMes
                        // El modelo cambia con cada refresco y ComboBox reinicia
                        // currentIndex: se vuelve a sincronizar con el mes.
                        readonly property int indiceDeseado: pagina.vm.mes - 1
                        function sincronizar() { comboMes.currentIndex = comboMes.indiceDeseado }
                        currentIndex: comboMes.indiceDeseado
                        onIndiceDeseadoChanged: comboMes.sincronizar()
                        onModelChanged: Qt.callLater(comboMes.sincronizar)
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Mes")
                        onActivated: indice => pagina.vm.seleccionarMes(pagina.vm.anio, indice + 1)
                    }
                }

                AvisoEnLinea {
                    visible: pagina.vm.hayPerfiles && !pagina.vm.eFirmaLista
                    variante: "advertencia"
                    icono: "key-slash"
                    titulo: qsTr("Este perfil no tiene e.firma")
                    descripcion: qsTr("Puedes ver las facturas que ya están en el equipo. Para descargar, registra la e.firma del perfil.")
                    textoAccion: qsTr("Registrar e.firma…")
                    Layout.fillWidth: true
                    onAccionSolicitada: pagina.vm.irA("perfiles")
                }

                FranjaMes {
                    vm: pagina.vm
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: Theme.espacioS
                    Layout.fillWidth: true
                    visible: pagina.vm.estadoMes.length > 0 && pagina.vm.estadoMes !== "Futuro"

                    SelectorSegmentado {
                        variante: "pestanas"
                        nombreAccesible: qsTr("Dirección")
                        opciones: [
                            { clave: "emitidas", texto: qsTr("Emitidas"), contador: pagina.vm.emitidasVisibles.length },
                            { clave: "recibidas", texto: qsTr("Recibidas"), contador: pagina.vm.recibidasVisibles.length }
                        ]
                        valor: pagina.vm.direccion
                        onActivado: clave => { pagina.vm.direccion = clave; pagina.vm.pdf = null }
                    }
                    Item { Layout.fillWidth: true }
                    CampoTexto {
                        id: busqueda
                        placeholderText: qsTr("Buscar RFC, nombre, folio o UUID")
                        text: pagina.vm.busqueda
                        Layout.preferredWidth: pagina.angosto ? 170 : 250
                        Accessible.name: qsTr("Buscar facturas (⌘F)")
                        onTextEdited: pagina.vm.busqueda = text
                        Keys.onEscapePressed: { pagina.vm.busqueda = ""; lista.forceActiveFocus() }
                        Keys.onDownPressed: lista.forceActiveFocus()
                    }
                    CampoCombo {
                        id: comboTipo
                        readonly property var claves: ["", "I", "E", "P", "N"]
                        model: [qsTr("Todos los tipos"), qsTr("Ingreso"), qsTr("Egreso"), qsTr("Pago"), qsTr("Nómina")]
                        currentIndex: Math.max(0, comboTipo.claves.indexOf(pagina.vm.filtroTipo))
                        Layout.preferredWidth: 140
                        Accessible.name: qsTr("Tipo de comprobante")
                        onActivated: indice => pagina.vm.filtroTipo = comboTipo.claves[indice]
                    }
                }

                Rectangle {
                    id: contenedorLista
                    visible: pagina.vm.estadoMes.length > 0 && pagina.vm.estadoMes !== "Futuro"
                    color: Theme.superficie
                    radius: Theme.radioTarjeta
                    border.width: 1
                    border.color: Theme.separador
                    clip: true
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    readonly property bool sinDatos: pagina.vm.listaDireccion.length === 0
                    readonly property bool sinCoincidencias: !contenedorLista.sinDatos && pagina.vm.cfdisFiltrados.length === 0

                    ListView {
                        id: lista
                        anchors.fill: parent
                        anchors.margins: 1
                        visible: pagina.vm.cfdisFiltrados.length > 0
                        model: pagina.vm.cfdisFiltrados
                        clip: true
                        focus: true
                        keyNavigationEnabled: true
                        boundsBehavior: Flickable.StopAtBounds
                        currentIndex: -1
                        Accessible.role: Accessible.List
                        Accessible.name: pagina.vm.direccion === "emitidas" ? qsTr("Facturas emitidas") : qsTr("Facturas recibidas")
                        ScrollBar.vertical: ScrollBar { }
                        Keys.onReturnPressed: if (lista.currentIndex >= 0) pagina.vm.pdf = pagina.vm.cfdisFiltrados[lista.currentIndex]
                        Keys.onSpacePressed: if (lista.currentIndex >= 0) pagina.vm.pdf = pagina.vm.cfdisFiltrados[lista.currentIndex]

                        // Cabecera de columnas: alto de control (altoControl), esquinas
                        // superiores con el radio interior de la tarjeta y linea
                        // inferior separador, como las filas.
                        header: Rectangle {
                            id: cabecera
                            width: lista.width
                            height: Theme.altoControl
                            color: Theme.superficieSeccion
                            radius: Theme.radioTarjeta - 1
                            z: 2
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: cabecera.radius
                                color: Theme.superficieSeccion
                            }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: Theme.separador
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.espacioL
                                anchors.rightMargin: Theme.espacioL
                                spacing: Theme.espacioM
                                component Columna: Label {
                                    color: Theme.textoSecundario
                                    font.pixelSize: Theme.etiqueta.size
                                    font.weight: Font.DemiBold
                                }
                                Columna { text: qsTr("FECHA"); Layout.preferredWidth: 120 - Theme.espacioM }
                                // Solo la contraparte: Receptor en Emitidas, Emisor en Recibidas
                                // (aprobado por el usuario en T023.1; FilaCfdi productiva se ajusta en T023.4).
                                Columna { text: pagina.vm.direccion === "emitidas" ? qsTr("RECEPTOR") : qsTr("EMISOR"); Layout.fillWidth: true; Layout.preferredWidth: 10 }
                                Columna { text: qsTr("TOTAL"); horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 130 - Theme.espacioM }
                                Columna { text: qsTr("UUID"); Layout.preferredWidth: 140 - Theme.espacioM }
                            }
                        }
                        headerPositioning: ListView.OverlayHeader

                        delegate: FilaCfdi {
                            id: fila
                            required property var modelData
                            required property int index
                            width: lista.width
                            fecha: fila.modelData.fecha
                            tipoTexto: fila.modelData.tipoTexto
                            serieFolio: fila.modelData.serieFolio
                            readonly property bool emitida: pagina.vm.direccion === "emitidas"
                            emisorRfc: fila.emitida ? fila.modelData.receptorRfc : fila.modelData.emisorRfc
                            emisorNombre: fila.emitida ? fila.modelData.receptorNombre : fila.modelData.emisorNombre
                            receptorRfc: fila.modelData.receptorRfc
                            receptorNombre: fila.modelData.receptorNombre
                            total: fila.modelData.total
                            moneda: fila.modelData.moneda
                            metodoPago: fila.modelData.metodoPago
                            uuid: fila.modelData.uuid
                            uuidCorto: fila.modelData.uuidCorto
                            estadoParseoTexto: qsTr("Completo")
                            mostrarReceptor: false
                            nombreAccesible: [fila.modelData.tipoTexto, fila.modelData.fecha,
                                              pagina.vm.direccion === "emitidas" ? fila.modelData.receptorNombre : fila.modelData.emisorNombre,
                                              fila.modelData.total].join(", ")
                            seleccionada: pagina.vm.pdf !== null && pagina.vm.pdf.uuid === fila.modelData.uuid
                            conFoco: lista.activeFocus && lista.currentIndex === fila.index
                            onClicked: {
                                lista.currentIndex = fila.index
                                pagina.vm.pdf = fila.modelData
                            }
                        }
                    }

                    EstadoVacio {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 2 * Theme.espacioL, 420)
                        visible: contenedorLista.sinDatos || contenedorLista.sinCoincidencias
                        variante: "vacio"
                        titulo: contenedorLista.sinCoincidencias ? qsTr("Sin coincidencias")
                              : pagina.vm.estadoMes === "Preparando"
                                ? qsTr("Las facturas aparecerán aquí en cuanto lleguen")
                              : pagina.vm.estadoMes === "Atencion"
                                ? qsTr("Las recibidas de este mes no están en el equipo")
                              : qsTr("Aún no hay facturas de este mes en el equipo")
                        descripcion: contenedorLista.sinCoincidencias
                                     ? qsTr("Ninguna factura de esta pestaña coincide con «%1».").arg(pagina.vm.busqueda)
                                     : pagina.vm.estadoMes === "Preparando"
                                       ? qsTr("Puedes cambiar de mes o cerrar la ventana; la descarga sigue.")
                                       : qsTr("Usa la acción de arriba para obtenerlas del SAT.")
                        textoAccion: contenedorLista.sinCoincidencias ? qsTr("Limpiar búsqueda") : ""
                        onAccionSolicitada: { pagina.vm.busqueda = ""; pagina.vm.filtroTipo = "" }
                    }
                }

                Item {
                    visible: pagina.vm.estadoMes === "Futuro"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }
    }

    PdfSimulado {
        vm: pagina.vm
    }
}
