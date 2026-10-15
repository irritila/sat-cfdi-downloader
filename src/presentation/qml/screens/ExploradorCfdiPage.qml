pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import "Etiquetas.js" as Etiquetas

// Facturas de una solicitud (T015.3, T015 D1). Se abre desde "Ver facturas
// (N)" del detalle y regresa a el.
// - Encabezado: "‹ Solicitud", titulo "Facturas" con el conteo ("N" o "N de
//   M" con filtros) y subtitulo "RFC · Tipo · periodo" de la solicitud.
// - Barra: busqueda (RFC, nombre, UUID o folio; el view model aplica un
//   retardo corto y la ultima respuesta gana), filtro por tipo de comprobante
//   y "Limpiar filtros".
// - Lineas de estado (ExploradorCfdiViewModel.lineasEstado, en su orden) con
//   "Abrir carpeta de la solicitud" (T009.1) en las de paquetes no
//   disponibles o ilegibles y CFDI ilegibles. No son modales.
// - Lista (FilaCfdi) ordenada por fecha; siempre muestra los CFDI ya
//   indexados. Estados segun `vista`: cargando (tras 300 ms), error (EstadoVacio
//   con "Reintentar" enfocado, reemplaza la lista), sinCoincidencias (con
//   "Limpiar filtros"), indexando (sin estado vacio) y sinFacturas.
// - Visor del PDF (T015.4, VisorPdfCfdi): Enter, doble clic o "Ver PDF" sobre
//   la fila seleccionada lo abren encima de la lista (que sigue viva y
//   conserva la seleccion); Escape o "‹ Facturas" lo cierran y devuelven el
//   foco a la fila.
// Teclado: Tab recorre regreso, busqueda, filtro, Limpiar filtros, Ver PDF,
// lista y acciones de las lineas; flechas mueven la seleccion; Enter/Return
// abre el PDF; ⌘C copia el UUID de la fila; ⌘F (Main.qml) enfoca la busqueda;
// Flecha abajo pasa de la busqueda a la lista; Escape vacia la busqueda,
// cierra el visor o regresa al detalle.
Page {
    id: pagina
    objectName: "paginaExploradorCfdi"

    required property AppViewModel app
    required property ExploradorCfdiViewModel explorador
    required property SolicitudDetailViewModel detalle

    readonly property CfdiListModel modelo: pagina.explorador.modelo
    readonly property string vista: pagina.explorador.vista
    readonly property bool conDatos: pagina.explorador.estado === ExploradorCfdiViewModel.ConDatos
    readonly property bool anchoAmplio: pagina.width >= 760
    // T014.1 D5: el unico dialogo del explorador es el selector de guardado
    // del visor (T015.4).
    readonly property bool dialogoAbierto: visor.dialogoAbierto
    readonly property bool visorAbierto: pagina.explorador.visor.abierto
    readonly property bool puedeBuscar: pagina.explorador.estado !== ExploradorCfdiViewModel.Ninguno
                                        && !pagina.visorAbierto
    readonly property alias campoBusqueda: busqueda
    readonly property alias lista: lista
    readonly property alias visor: visor

    readonly property bool mismaSolicitud: pagina.detalle.cargada
                                           && pagina.detalle.solicitudId === pagina.explorador.solicitudId

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Facturas de la solicitud")

    background: Rectangle {
        color: Theme.superficie
    }

    Component.onCompleted: {
        pagina.vistaAnterior = pagina.vista
        Qt.callLater(pagina.enfocarInicial)
    }

    function enfocarInicial() {
        if (lista.visible && lista.count > 0)
            lista.forceActiveFocus(Qt.TabFocusReason)
        else if (error.visible)
            error.botonAccion.forceActiveFocus(Qt.TabFocusReason)
        else
            busqueda.forceActiveFocus(Qt.TabFocusReason)
    }

    // Si la primera carga termina despues del foco inicial (en la busqueda
    // vacia), el foco pasa a la lista; no se mueve el foco del usuario.
    property string vistaAnterior: ""
    onVistaChanged: {
        if (pagina.vistaAnterior === "cargando" && pagina.vista === "lista"
                && busqueda.activeFocus && busqueda.text.length === 0)
            Qt.callLater(pagina.enfocarListaTrasCarga)
        pagina.vistaAnterior = pagina.vista
    }
    function enfocarListaTrasCarga() {
        if (lista.visible && lista.count > 0 && busqueda.activeFocus && busqueda.text.length === 0)
            lista.forceActiveFocus(Qt.OtherFocusReason)
    }

    function enfocarBusqueda() {
        if (!pagina.puedeBuscar)
            return false
        busqueda.forceActiveFocus(Qt.ShortcutFocusReason)
        busqueda.selectAll()
        return true
    }

    function limpiarFiltros() {
        pagina.explorador.limpiarFiltros()
        busqueda.forceActiveFocus(Qt.OtherFocusReason)
    }

    function regresar() {
        pagina.app.regresarDeFacturas()
    }

    function ajustarFilaActual() {
        if (lista.count > 0 && lista.currentIndex < 0)
            lista.currentIndex = 0
    }

    function previsualizarActual() {
        if (lista.currentIndex >= 0)
            pagina.explorador.previsualizar(lista.currentIndex)
    }

    function cerrarVisor() {
        pagina.explorador.visor.cerrar()
    }

    // Al abrir, el foco entra al visor; al cerrar, vuelve a la fila.
    onVisorAbiertoChanged: Qt.callLater(pagina.enfocarTrasVisor)
    function enfocarTrasVisor() {
        if (pagina.visorAbierto) {
            visor.forceActiveFocus(Qt.OtherFocusReason)
            visor.enfocarInicial()
        } else if (lista.visible && lista.count > 0) {
            lista.forceActiveFocus(Qt.OtherFocusReason)
            lista.positionViewAtIndex(lista.currentIndex, ListView.Contain)
        } else {
            pagina.enfocarInicial()
        }
    }

    function copiarUuidActual() {
        const uuid = pagina.modelo.uuidEn(lista.currentIndex)
        if (uuid.length > 0 && Portapapeles.copiar(uuid)) {
            lista.Accessible.announce(qsTr("UUID copiado al portapapeles."))
            return true
        }
        return false
    }

    Keys.onEscapePressed: {
        if (pagina.visorAbierto)
            pagina.cerrarVisor()
        else
            pagina.regresar()
    }

    header: EncabezadoPagina {
        id: encabezado
        // Con el visor abierto, su propio encabezado ocupa este lugar.
        visible: !pagina.visorAbierto
        titulo: qsTr("Facturas")
        contador: !pagina.conDatos ? ""
                  : pagina.explorador.hayFiltros ? qsTr("%1 de %2").arg(pagina.modelo.count).arg(pagina.explorador.totalCfdi)
                  : String(pagina.modelo.count)
        subtitulo: pagina.mismaSolicitud
                   ? [pagina.detalle.perfilRfc, Etiquetas.tipoDescarga(pagina.detalle.tipoDescarga),
                      FormatoFechas.rango(pagina.detalle.fechaInicial, pagina.detalle.fechaFinal)]
                     .filter(t => t.length > 0).join(" · ")
                   : ""
        mostrarRegresar: true
        textoRegresar: qsTr("Solicitud")
        descripcionRegresar: qsTr("Regresar al detalle de la solicitud (Escape)")
        atajoRegresar: ""
        onRegresarSolicitado: pagina.regresar()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: !pagina.visorAbierto

        // ---- Busqueda y filtro ----
        Rectangle {
            objectName: "barraFiltrosFacturas"
            color: Theme.superficie
            Layout.fillWidth: true
            implicitHeight: filtros.implicitHeight + 2 * Theme.espacioM

            RowLayout {
                id: filtros
                anchors.fill: parent
                anchors.leftMargin: Theme.espacioL
                anchors.rightMargin: Theme.espacioL
                spacing: Theme.espacioS

                CampoTexto {
                    id: busqueda
                    objectName: "campoBusquedaFacturas"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 180
                    Layout.maximumWidth: 360
                    leftPadding: 28
                    rightPadding: limpiarBusqueda.visible ? 28 : 8
                    enabled: pagina.puedeBuscar
                    text: pagina.explorador.texto
                    placeholderText: qsTr("Buscar por RFC, nombre, UUID o folio")
                    onTextEdited: pagina.explorador.texto = text
                    Accessible.name: qsTr("Buscar facturas por RFC, nombre, UUID o folio")
                    Accessible.description: qsTr("Atajo: %1").arg("⌘F")
                    ToolTip.visible: busqueda.hovered && !busqueda.activeFocus
                    ToolTip.text: qsTr("Buscar (%1)").arg("⌘F")
                    ToolTip.delay: 600
                    Keys.onDownPressed: (evento) => {
                        if (lista.visible && lista.count > 0) {
                            lista.forceActiveFocus(Qt.TabFocusReason)
                            evento.accepted = true
                        } else {
                            evento.accepted = false
                        }
                    }
                    Keys.onEscapePressed: (evento) => {
                        if (busqueda.text.length > 0) {
                            pagina.explorador.texto = ""
                            evento.accepted = true
                        } else {
                            evento.accepted = false
                        }
                    }

                    Icono {
                        nombre: "magnifyingglass"
                        color: Theme.textoSecundario
                        tamano: 14
                        anchors.left: parent.left
                        anchors.leftMargin: 9
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    BotonAccion {
                        id: limpiarBusqueda
                        objectName: "botonLimpiarBusquedaFacturas"
                        visible: busqueda.text.length > 0
                        variante: "icono"
                        compacto: true
                        icono: "xmark"
                        nombreAccesible: qsTr("Borrar búsqueda")
                        focusPolicy: Qt.NoFocus
                        anchors.right: parent.right
                        anchors.rightMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: {
                            pagina.explorador.texto = ""
                            busqueda.forceActiveFocus(Qt.OtherFocusReason)
                        }
                    }
                }
                CampoCombo {
                    id: filtroTipo
                    objectName: "filtroTipoComprobante"
                    Layout.preferredWidth: 170
                    focusPolicy: Qt.StrongFocus
                    enabled: pagina.puedeBuscar
                    textRole: "texto"
                    valueRole: "clave"
                    model: [
                        { clave: "", texto: qsTr("Todos los tipos") },
                        { clave: "I", texto: qsTr("Ingreso") },
                        { clave: "E", texto: qsTr("Egreso") },
                        { clave: "T", texto: qsTr("Traslado") },
                        { clave: "N", texto: qsTr("Nómina") },
                        { clave: "P", texto: qsTr("Pago") }
                    ]
                    currentIndex: count > 0 ? indexOfValue(pagina.explorador.tipo) : -1
                    onActivated: (indice) => { pagina.explorador.tipo = valueAt(indice) }
                    Accessible.name: qsTr("Filtrar por tipo de comprobante")
                    Accessible.description: displayText
                }
                BotonAccion {
                    id: botonLimpiarFiltros
                    objectName: "botonLimpiarFiltrosFacturas"
                    visible: pagina.explorador.hayFiltros
                    variante: "secundario"
                    text: qsTr("Limpiar filtros")
                    descripcion: qsTr("Quitar la búsqueda y el filtro de tipo")
                    onClicked: pagina.limpiarFiltros()
                }
                Item { Layout.fillWidth: true }
                // T015.4: PDF de la factura seleccionada (igual que Enter).
                BotonAccion {
                    id: botonVerPdf
                    objectName: "botonVerPdf"
                    visible: pagina.explorador.visor.disponible
                    enabled: lista.visible && lista.currentIndex >= 0
                    variante: "secundario"
                    icono: "doc"
                    text: qsTr("Ver PDF")
                    descripcion: qsTr("Ver el PDF de la factura seleccionada (Enter)")
                    onClicked: pagina.previsualizarActual()
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.separador
            }
        }

        // ---- Lineas de estado del indice y aviso de Finder ----
        Rectangle {
            objectName: "lineasEstadoFacturas"
            // Condiciones directas: la visibilidad efectiva de los hijos depende de esta.
            visible: pagina.vista !== "error" && pagina.vista !== "cargando"
                     && (pagina.explorador.lineasEstado.length > 0 || pagina.explorador.mensajeFinder.length > 0)
            color: Theme.superficie
            Layout.fillWidth: true
            implicitHeight: lineas.implicitHeight + 2 * Theme.espacioM

            ColumnLayout {
                id: lineas
                anchors.fill: parent
                anchors.leftMargin: Theme.espacioL
                anchors.rightMargin: Theme.espacioL
                anchors.topMargin: Theme.espacioM
                anchors.bottomMargin: Theme.espacioM
                spacing: Theme.espacioS

                // T009.1 D7: aviso accesible si no se pudo abrir la carpeta.
                AvisoEnLinea {
                    objectName: "mensajeFinderFacturas"
                    visible: pagina.explorador.mensajeFinder.length > 0
                    variante: "error"
                    icono: "folder"
                    titulo: pagina.explorador.mensajeFinder
                    Layout.fillWidth: true
                }
                Repeater {
                    model: pagina.explorador.lineasEstado
                    delegate: AvisoEnLinea {
                        required property var modelData
                        objectName: "lineaEstado_" + modelData.clave
                        variante: modelData.clave === "indexando" ? "progreso" : "advertencia"
                        icono: modelData.clave === "indexando" ? "clock" : ""
                        anunciar: false
                        titulo: modelData.texto
                        textoAccion: modelData.accionCarpeta && pagina.explorador.puedeAbrirCarpeta
                                     ? qsTr("Abrir carpeta de la solicitud") : ""
                        objectNameAccion: "botonCarpeta_" + modelData.clave
                        Layout.fillWidth: true
                        onAccionSolicitada: pagina.explorador.abrirCarpetaSolicitud()
                    }
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.separador
            }
        }

        // ---- Lista ----
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: lista
                objectName: "listaFacturas"
                anchors.fill: parent
                clip: true
                visible: pagina.vista === "lista"
                model: pagina.modelo
                activeFocusOnTab: true
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                headerPositioning: ListView.OverlayHeader

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Lista de facturas, %n elemento(s)", "", count)

                // Primera fila seleccionada al llegar datos (diferido: la vista
                // aplica los cambios del modelo en su pulido). Una recarga
                // conserva la fila actual.
                onCountChanged: Qt.callLater(pagina.ajustarFilaActual)

                ScrollBar.vertical: ScrollBar { }

                Keys.onReturnPressed: pagina.previsualizarActual()
                Keys.onEnterPressed: pagina.previsualizarActual()
                Keys.onPressed: (evento) => {
                    if (evento.matches(StandardKey.Copy))
                        evento.accepted = pagina.copiarUuidActual()
                }

                header: Rectangle {
                    objectName: "encabezadosFacturas"
                    z: 2
                    width: ListView.view.width
                    height: 30
                    color: Theme.superficie

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.espacioL
                        anchors.rightMargin: Theme.espacioL
                        spacing: Theme.espacioM

                        Repeater {
                            model: [
                                { texto: qsTr("Fecha"), ancho: 120 - Theme.espacioM, estirar: 0 },
                                { texto: qsTr("Emisor"), ancho: 10, estirar: 10 },
                                { texto: qsTr("Receptor"), ancho: 10, estirar: 10, receptor: true },
                                { texto: qsTr("Total"), ancho: 130 - Theme.espacioM, estirar: 0, derecha: true },
                                { texto: qsTr("UUID"), ancho: 140 - Theme.espacioM, estirar: 0 }
                            ]
                            delegate: Label {
                                required property var modelData
                                visible: !modelData.receptor || pagina.anchoAmplio
                                text: modelData.texto
                                color: Theme.textoSecundario
                                font.family: Theme.familia
                                font.pixelSize: Theme.etiqueta.size
                                font.weight: Font.DemiBold
                                horizontalAlignment: modelData.derecha ? Text.AlignRight : Text.AlignLeft
                                elide: Text.ElideRight
                                Layout.preferredWidth: modelData.ancho
                                Layout.fillWidth: modelData.estirar > 0
                                Layout.horizontalStretchFactor: modelData.estirar > 0 ? modelData.estirar : -1
                                Accessible.ignored: true
                            }
                        }
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.separador
                    }
                }

                delegate: FilaCfdi {
                    id: fila
                    required property int index
                    required property var model

                    objectName: "filaCfdi_" + index
                    width: ListView.view.width
                    fecha: model.fecha
                    tipoTexto: model.tipoTexto
                    serieFolio: model.serieFolio
                    emisorRfc: model.emisorRfc
                    emisorNombre: model.emisorNombre
                    receptorRfc: model.receptorRfc
                    receptorNombre: model.receptorNombre
                    total: model.total
                    moneda: model.moneda
                    metodoPago: model.metodoPago
                    uuid: model.uuid
                    uuidCorto: model.uuidCorto
                    estadoParseo: model.estadoParseo
                    estadoParseoTexto: model.estadoParseoTexto
                    nombreAccesible: model.nombreAccesible
                    mostrarReceptor: pagina.anchoAmplio
                    seleccionada: ListView.isCurrentItem
                    conFoco: ListView.isCurrentItem && lista.activeFocus

                    onClicked: {
                        lista.currentIndex = fila.index
                        lista.forceActiveFocus(Qt.MouseFocusReason)
                    }
                    onDoubleClicked: {
                        lista.currentIndex = fila.index
                        pagina.previsualizarActual()
                    }
                }
            }

            // Lista vacia mientras se indexa: sin estado vacio (la linea
            // "Indexando N paquetes" ya lo explica).
            ColumnLayout {
                objectName: "estadoIndexandoFacturas"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                visible: pagina.vista === "indexando"
                spacing: Theme.espacioS
                Accessible.role: Accessible.StaticText
                Accessible.name: textoIndexando.text

                BusyIndicator {
                    running: parent.visible
                    Layout.alignment: Qt.AlignHCenter
                    Accessible.ignored: true
                }
                Label {
                    id: textoIndexando
                    text: qsTr("Las facturas aparecerán aquí conforme se lean los paquetes.")
                    color: Theme.textoSecundario
                    font.family: Theme.familia
                    font.pixelSize: Theme.cuerpo.size
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }
            }

            EstadoVacio {
                id: sinFacturas
                objectName: "estadoSinFacturas"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                visible: pagina.vista === "sinFacturas"
                variante: "vacio"
                titulo: qsTr("Sin facturas")
                descripcion: qsTr("Los paquetes descargados de esta solicitud no tienen facturas que mostrar.")
                objectNameTitulo: "tituloSinFacturas"
            }

            EstadoVacio {
                id: sinCoincidencias
                objectName: "estadoSinCoincidenciasFacturas"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                visible: pagina.vista === "sinCoincidencias"
                variante: "vacio"
                titulo: qsTr("Ninguna factura coincide con la búsqueda")
                descripcion: qsTr("Cambia la búsqueda o el tipo, o límpialos para ver todas las facturas.")
                textoAccion: qsTr("Limpiar filtros")
                objectNameTitulo: "tituloSinCoincidenciasFacturas"
                objectNameAccion: "botonLimpiarFiltrosVacioFacturas"
                onAccionSolicitada: pagina.limpiarFiltros()
            }

            // Estado: cargando (solo si tarda mas de 300 ms)
            ColumnLayout {
                objectName: "estadoCargandoFacturas"
                anchors.centerIn: parent
                visible: pagina.vista === "cargando" && !retraso.running
                spacing: Theme.espacioS
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Cargando facturas")

                Timer {
                    id: retraso
                    interval: 300
                    running: pagina.vista === "cargando"
                }
                BusyIndicator {
                    running: parent.visible
                    Layout.alignment: Qt.AlignHCenter
                    Accessible.ignored: true
                }
            }

            // Estado: error de lectura del indice (reemplaza la lista).
            EstadoVacio {
                id: error
                objectName: "estadoErrorFacturas"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                visible: pagina.vista === "error"
                variante: "error"
                titulo: pagina.explorador.errorMessage
                descripcion: qsTr("Los paquetes descargados siguen en su carpeta. Intenta leer la lista de nuevo.")
                textoAccion: qsTr("Reintentar")
                objectNameTitulo: "errorFacturas"
                objectNameAccion: "botonReintentarFacturas"
                onAccionSolicitada: pagina.explorador.recargar()
            }
        }
    }

    // ---- Visor del PDF (T015.4) ----
    VisorPdfCfdi {
        id: visor
        anchors.fill: parent
        visible: pagina.visorAbierto
        vm: pagina.explorador.visor
        onCerrarSolicitado: pagina.cerrarVisor()
    }
}
