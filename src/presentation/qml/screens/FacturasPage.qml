pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Pantalla principal Facturas (T023.4; prototipo ajustado de T023.1). Solo
// representa FacturasViewModel: estados, textos, conteos, navegacion de meses
// y acciones vienen de C++ (QML no calcula reglas de negocio).
// - Encabezado: titulo, combo de perfil y "Perfiles SAT" (⌘2).
// - Sin perfiles: guia de primer uso (T014.3) con el paso 3 "Descarga las
//   facturas de un mes".
// - Lista lateral de meses (ListaMesesFacturas); bajo 760 px, selector de anio
//   y combo de mes.
// - Aviso de e.firma no lista (la franja ofrece "Registrar e.firma…").
// - FranjaMes con su unica accion (⌘R) y el enlace "Ver detalles tecnicos".
// - Pestanas Emitidas/Recibidas con conteo, busqueda (⌘F), filtro por tipo,
//   "Limpiar filtros" y la lista (FilaCfdi con solo la contraparte, D20).
// - Visor PDF (T015.4) y confirmacion por direccion (DialogoConfirmacion).
// Teclado: Tab recorre perfil, meses, franja, pestanas, busqueda, filtro y
// lista; flechas en la lista; Enter abre el PDF; ⌘C copia el UUID; Escape
// vacia la busqueda o cierra el visor. ⌘1/⌘2/⌘R/⌘F/⌘↑/⌘↓ en Main.qml.
// Foco inicial sin anillo (OtherFocusReason); el anillo de la fila aparece al
// navegar con teclado o al entrar con Tab.
// T026.3: "Exportar este mes" y "Elegir facturas" en la barra de filtros (Tab:
// filtro, [Limpiar filtros], Exportar, Elegir, lista). En modo de seleccion,
// casilla por fila (enlazada a revisionSeleccion), Espacio alterna la fila
// actual, barra con el conteo, "Exportar elegidas…", "Quitar selección" y
// "Listo"; Escape sale del modo. DialogoExportarFacturas lo abre el view model.
Page {
    id: pagina
    objectName: "paginaFacturas"

    required property AppViewModel app
    required property FacturasViewModel facturas
    required property ExportacionFacturasViewModel exportacion

    readonly property FranjaMesViewModel franja: pagina.facturas.franja
    readonly property string vista: pagina.facturas.vista
    readonly property bool sinPerfil: !pagina.facturas.hayPerfiles || pagina.vista === "sinPerfil"
    readonly property bool angosto: pagina.width < 760
    readonly property bool visorAbierto: pagina.facturas.visor.abierto
    // T014.1 D5: dialogos propios (confirmacion y guardado del visor).
    readonly property bool dialogoAbierto: confirmacion.visible || visor.dialogoAbierto || dialogoExportar.visible
    readonly property bool seleccionando: pagina.exportacion.seleccionando
    readonly property bool conLista: !pagina.sinPerfil && pagina.vista !== "futuro"
    readonly property bool puedeBuscar: pagina.conLista && !pagina.visorAbierto
    readonly property bool puedeAccion: !pagina.sinPerfil && !pagina.visorAbierto && franjaMes.conAccion
                                        && pagina.franja.accionHabilitada && !pagina.franja.ejecutando
    readonly property bool puedeMoverMes: !pagina.sinPerfil && !pagina.visorAbierto

    readonly property alias campoBusqueda: busqueda
    readonly property alias lista: lista
    readonly property alias listaMeses: listaMeses
    readonly property alias franjaMes: franjaMes
    readonly property alias visor: visor
    readonly property alias dialogoConfirmacion: confirmacion
    readonly property alias dialogoExportar: dialogoExportar

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Facturas")

    background: Rectangle {
        color: Theme.fondo
    }

    // activar()/desactivar() los llama AppViewModel al entrar y salir.
    Component.onCompleted: Qt.callLater(pagina.enfocarInicial)

    // Foco inicial sin anillo: en la lista si tiene filas; si no, en los meses.
    function enfocarInicial() {
        if (lista.mostrar && lista.count > 0)
            lista.enfocarSinAnillo(Qt.OtherFocusReason)
        else if (listaMeses.visible)
            listaMeses.enfocarSinAnillo()
        else if (comboPerfil.visible)
            comboPerfil.forceActiveFocus(Qt.OtherFocusReason)
    }

    function enfocarBusqueda() {
        if (!pagina.puedeBuscar)
            return false
        busqueda.forceActiveFocus(Qt.ShortcutFocusReason)
        busqueda.selectAll()
        return true
    }

    function ejecutarAccion() {
        if (pagina.puedeAccion)
            pagina.facturas.ejecutarAccion()
    }

    function moverMes(delta) {
        return pagina.puedeMoverMes && pagina.facturas.moverMes(delta)
    }

    function limpiarFiltros() {
        pagina.facturas.limpiarFiltros()
        busqueda.forceActiveFocus(Qt.OtherFocusReason)
    }

    function previsualizarActual() {
        if (lista.currentIndex >= 0)
            pagina.facturas.previsualizar(lista.currentIndex)
    }

    function copiarUuidActual() {
        const uuid = pagina.facturas.modelo.uuidEn(lista.currentIndex)
        if (uuid.length > 0 && Portapapeles.copiar(uuid)) {
            lista.Accessible.announce(qsTr("UUID copiado al portapapeles."))
            return true
        }
        return false
    }

    // Espacio en modo de seleccion: alterna la fila actual.
    function alternarFilaActual() {
        const m = pagina.facturas.modelo
        if (!pagina.seleccionando || lista.currentIndex < 0)
            return false
        const indice = m.index(lista.currentIndex, 0)
        return pagina.exportacion.alternarSeleccion(m.data(indice, FacturasListModel.PaqueteIdRole),
                                                    m.data(indice, FacturasListModel.PosicionRole))
    }

    // El view model abre y cierra el dialogo de exportacion.
    Connections {
        target: pagina.exportacion
        function onEstadoChanged() {
            if (pagina.exportacion.dialogoAbierto && !dialogoExportar.visible)
                dialogoExportar.open()
            else if (!pagina.exportacion.dialogoAbierto && dialogoExportar.visible)
                dialogoExportar.close()
        }
    }

    function ajustarFilaActual() {
        if (lista.count > 0 && lista.currentIndex < 0)
            lista.currentIndex = 0
        else if (lista.count === 0)
            lista.currentIndex = -1
    }

    // Al abrir el visor el foco entra en el; al cerrar vuelve a la fila.
    onVisorAbiertoChanged: Qt.callLater(pagina.enfocarTrasVisor)
    function enfocarTrasVisor() {
        if (pagina.visorAbierto) {
            visor.forceActiveFocus(Qt.OtherFocusReason)
            visor.enfocarInicial()
        } else if (lista.mostrar && lista.count > 0) {
            lista.enfocarSinAnillo(Qt.OtherFocusReason)
            lista.positionViewAtIndex(lista.currentIndex, ListView.Contain)
        } else {
            pagina.enfocarInicial()
        }
    }

    // Confirmacion por direccion: una a la vez, la abre y cierra el view model.
    Connections {
        target: pagina.facturas
        function onConfirmacionChanged() {
            if (pagina.facturas.confirmacionPendiente && !confirmacion.visible)
                confirmacion.open()
            else if (!pagina.facturas.confirmacionPendiente && confirmacion.visible)
                confirmacion.close()
        }
        // Anuncio una vez por cambio de estado del mes (live region).
        function onAnuncioChanged() {
            if (pagina.facturas.anuncio.length > 0)
                franjaMes.Accessible.announce(pagina.facturas.anuncio)
        }
    }

    Keys.onEscapePressed: (evento) => {
        if (pagina.visorAbierto) {
            pagina.facturas.visor.cerrar()
            evento.accepted = true
        } else if (pagina.seleccionando) {
            pagina.exportacion.terminarSeleccion()
            evento.accepted = true
        } else {
            evento.accepted = false
        }
    }

    header: EncabezadoPagina {
        id: encabezado
        visible: !pagina.visorAbierto
        titulo: qsTr("Facturas")

        CampoCombo {
            id: comboPerfil
            objectName: "comboPerfilFacturas"
            visible: pagina.facturas.hayPerfiles
            model: pagina.facturas.perfiles
            textRole: "etiqueta"
            focusPolicy: Qt.StrongFocus
            currentIndex: pagina.facturas.perfilIndice
            Layout.preferredWidth: pagina.angosto ? 200 : 290
            Accessible.name: qsTr("Perfil fiscal")
            Accessible.description: displayText
            onActivated: (indice) => pagina.facturas.seleccionarPerfil(indice)
        }
        // T023.5: Historial de descargas del perfil (⌘3).
        BotonAccion {
            objectName: "botonHistorialFacturas"
            visible: pagina.facturas.hayPerfiles
            text: qsTr("Historial")
            icono: "clock"
            atajo: "⌘3"
            onClicked: pagina.app.mostrarHistorial()
        }
        BotonAccion {
            objectName: "botonPerfilesFacturas"
            text: qsTr("Perfiles SAT")
            icono: "person-card"
            atajo: "⌘2"
            onClicked: pagina.app.mostrarPerfiles()
        }
    }

    // ---- Sin perfiles: guia de primer uso (T014.3, paso 3 de T023) ----
    ColumnLayout {
        id: guia
        objectName: "guiaPrimerUsoFacturas"
        visible: pagina.sinPerfil && !pagina.visorAbierto
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.espacioXl, 480)
        spacing: Theme.espacioL

        readonly property var pasos: [
            { nombre: "pasoPerfil", texto: qsTr("Crea un perfil SAT con el RFC del contribuyente"),
              accion: qsTr("Perfiles SAT"), hecho: pagina.app.primerUsoPasos[0] === true },
            { nombre: "pasoEFirma", texto: qsTr("Registra la e.firma del perfil"),
              accion: qsTr("Registrar e.firma…"), hecho: pagina.app.primerUsoPasos[1] === true },
            { nombre: "pasoFacturas", texto: qsTr("Descarga las facturas de un mes"),
              accion: qsTr("Ver facturas"), hecho: false }
        ]
        readonly property int siguiente: guia.pasos.findIndex(p => !p.hecho)

        EstadoVacio {
            variante: "sinPerfiles"
            titulo: qsTr("Empieza con un perfil fiscal")
            descripcion: qsTr("Archivo Fiscal guarda en este equipo las facturas emitidas y recibidas de cada RFC, mes por mes.")
            objectNameTitulo: "tituloGuiaFacturas"
            Layout.fillWidth: true
        }

        Rectangle {
            color: Theme.superficie
            radius: Theme.radioTarjeta
            border.width: 1
            border.color: Theme.separador
            Layout.fillWidth: true
            implicitHeight: columnaPasos.implicitHeight + 2 * Theme.espacioM

            ColumnLayout {
                id: columnaPasos
                anchors.fill: parent
                anchors.topMargin: Theme.espacioM
                anchors.bottomMargin: Theme.espacioM
                anchors.leftMargin: Theme.espacioL
                anchors.rightMargin: Theme.espacioL
                spacing: Theme.espacioS

                Repeater {
                    model: guia.pasos
                    delegate: RowLayout {
                        id: paso
                        required property var modelData
                        required property int index
                        readonly property bool esSiguiente: paso.index === guia.siguiente
                        objectName: paso.modelData.nombre
                        spacing: Theme.espacioM
                        Layout.fillWidth: true
                        Accessible.role: Accessible.ListItem
                        Accessible.name: qsTr("Paso %1 de 3: %2, %3").arg(paso.index + 1).arg(paso.modelData.texto)
                                         .arg(paso.modelData.hecho ? qsTr("hecho") : qsTr("pendiente"))

                        Icono {
                            nombre: paso.modelData.hecho ? "check-circle" : "circle-dashed"
                            color: paso.modelData.hecho ? Theme.exito : Theme.textoSecundario
                            tamano: 18
                        }
                        Label {
                            text: paso.modelData.texto
                            color: paso.modelData.hecho ? Theme.textoSecundario : Theme.texto
                            font.family: Theme.familia
                            font.pixelSize: Theme.cuerpo.size
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                        BotonAccion {
                            objectName: "boton_" + paso.modelData.nombre
                            text: paso.modelData.accion
                            compacto: true
                            variante: paso.esSiguiente ? "primario" : "secundario"
                            // Los pasos 1 y 2 se resuelven en Perfiles SAT; el 3,
                            // en esta misma pantalla al haber perfil.
                            enabled: paso.index < 2 && (paso.esSiguiente || paso.modelData.hecho)
                            onClicked: pagina.app.mostrarPerfiles()
                        }
                    }
                }
            }
        }
    }

    // ---- Con perfil ----
    RowLayout {
        anchors.fill: parent
        spacing: 0
        visible: !pagina.sinPerfil && !pagina.visorAbierto

        ListaMesesFacturas {
            id: listaMeses
            visible: !pagina.angosto
            meses: pagina.facturas.meses
            anio: pagina.facturas.anio
            puedeAnioAnterior: pagina.facturas.puedeAnioAnterior
            puedeAnioSiguiente: pagina.facturas.puedeAnioSiguiente
            Layout.preferredWidth: 212
            Layout.fillHeight: true
            onMesElegido: (anio, mes) => pagina.facturas.seleccionarMes(anio, mes)
            onAnioCambiado: (delta) => pagina.facturas.cambiarAnio(delta)
            onMoverSolicitado: (delta) => pagina.facturas.moverMes(delta)
        }

        ColumnLayout {
            spacing: Theme.espacioM
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.espacioL

            // D9: bajo ~760 px los meses son un combo con anio.
            RowLayout {
                objectName: "selectorMesAngosto"
                visible: pagina.angosto
                spacing: Theme.espacioS
                Layout.fillWidth: true
                BotonAccion {
                    variante: "icono"
                    compacto: true
                    icono: "chevron-left"
                    nombreAccesible: qsTr("Año anterior")
                    enabled: pagina.facturas.puedeAnioAnterior
                    onClicked: pagina.facturas.cambiarAnio(-1)
                }
                Label {
                    text: pagina.facturas.anio > 0 ? String(pagina.facturas.anio) : ""
                    color: Theme.texto
                    font.family: Theme.familia
                    font.pixelSize: Theme.cuerpoFuerte.size
                    font.weight: Font.DemiBold
                    Accessible.role: Accessible.Heading
                    Accessible.name: qsTr("Año %1").arg(text)
                }
                BotonAccion {
                    variante: "icono"
                    compacto: true
                    icono: "chevron-right"
                    nombreAccesible: qsTr("Año siguiente")
                    enabled: pagina.facturas.puedeAnioSiguiente
                    onClicked: pagina.facturas.cambiarAnio(1)
                }
                CampoCombo {
                    id: comboMes
                    objectName: "comboMesFacturas"
                    model: pagina.facturas.meses
                    textRole: "nombreAccesible"
                    valueRole: "mes"
                    focusPolicy: Qt.StrongFocus
                    currentIndex: pagina.facturas.meses.filaDe(pagina.facturas.mes)
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Mes")
                    Accessible.description: displayText
                    onActivated: (indice) => pagina.facturas.seleccionarMes(pagina.facturas.anio, comboMes.valueAt(indice))
                }
            }

            AvisoEnLinea {
                objectName: "avisoEFirmaFacturas"
                visible: pagina.facturas.hayPerfiles && !pagina.facturas.eFirmaLista
                         && pagina.facturas.motivoEFirma.length > 0
                variante: "advertencia"
                icono: "key-slash"
                anunciar: false
                titulo: pagina.facturas.motivoEFirma
                Layout.fillWidth: true
            }

            FranjaMes {
                id: franjaMes
                vm: pagina.franja
                Layout.fillWidth: true
                onAccionSolicitada: pagina.ejecutarAccion()
                onEnlaceSolicitado: pagina.facturas.verDetallesTecnicos()
            }

            // ---- Pestanas, busqueda y filtro ----
            // Una fila a 960; bajo 760 px las pestanas van arriba y la
            // busqueda y el filtro debajo (sin desborde en la ventana minima).
            GridLayout {
                objectName: "barraFiltrosFacturas"
                visible: pagina.conLista
                columns: pagina.angosto ? 2 : 5
                columnSpacing: Theme.espacioS
                rowSpacing: Theme.espacioS
                Layout.fillWidth: true

                SelectorSegmentado {
                    id: pestanas
                    objectName: "pestanasFacturas"
                    variante: "pestanas"
                    nombreAccesible: qsTr("Dirección")
                    opciones: [
                        { clave: "emitidas", texto: pagina.facturas.textoPestanaEmitidas },
                        { clave: "recibidas", texto: pagina.facturas.textoPestanaRecibidas }
                    ]
                    Layout.columnSpan: pagina.angosto ? 2 : 1
                    valor: pagina.facturas.direccion === FacturasViewModel.Emitidas ? "emitidas" : "recibidas"
                    onActivado: (clave) => {
                        pagina.facturas.direccion = clave === "emitidas" ? FacturasViewModel.Emitidas
                                                                         : FacturasViewModel.Recibidas
                    }
                }
                Item { visible: !pagina.angosto; Layout.fillWidth: true }
                CampoTexto {
                    id: busqueda
                    objectName: "campoBusquedaFacturas"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 140
                    Layout.maximumWidth: pagina.angosto ? -1 : 280
                    leftPadding: 28
                    rightPadding: limpiarBusqueda.visible ? 28 : Theme.espacioS
                    enabled: pagina.puedeBuscar
                    text: pagina.facturas.texto
                    placeholderText: pagina.facturas.placeholderBusqueda
                    onTextEdited: pagina.facturas.texto = text
                    Accessible.name: pagina.facturas.placeholderBusqueda
                    Accessible.description: qsTr("Atajo: %1").arg("⌘F")
                    Keys.onDownPressed: (evento) => {
                        if (lista.mostrar && lista.count > 0) {
                            lista.forceActiveFocus(Qt.TabFocusReason)
                            evento.accepted = true
                        } else {
                            evento.accepted = false
                        }
                    }
                    Keys.onEscapePressed: (evento) => {
                        if (busqueda.text.length > 0) {
                            pagina.facturas.texto = ""
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
                        anchors.leftMargin: Theme.espacioS
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    BotonAccion {
                        id: limpiarBusqueda
                        visible: busqueda.text.length > 0
                        variante: "icono"
                        compacto: true
                        icono: "xmark"
                        nombreAccesible: qsTr("Borrar búsqueda")
                        focusPolicy: Qt.NoFocus
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.espacioXs / 2
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: {
                            pagina.facturas.texto = ""
                            busqueda.forceActiveFocus(Qt.OtherFocusReason)
                        }
                    }
                }
                CampoCombo {
                    id: filtroTipo
                    objectName: "filtroTipoFacturas"
                    Layout.preferredWidth: 150
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
                    currentIndex: count > 0 ? indexOfValue(pagina.facturas.tipo) : -1
                    onActivated: (indice) => { pagina.facturas.tipo = valueAt(indice) }
                    Accessible.name: qsTr("Filtrar por tipo de comprobante")
                    Accessible.description: displayText
                }
                BotonAccion {
                    objectName: "botonLimpiarFiltrosFacturas"
                    visible: pagina.facturas.hayFiltros && !pagina.angosto
                    text: qsTr("Limpiar filtros")
                    descripcion: qsTr("Quitar la búsqueda y el filtro de tipo")
                    onClicked: pagina.limpiarFiltros()
                }
            }

            // ---- Modo de seleccion (T026.3) ----
            // Fuera de la seleccion: exportar el mes o empezar a elegir.
            RowLayout {
                objectName: "barraExportarFacturas"
                visible: pagina.conLista && !pagina.seleccionando
                spacing: Theme.espacioS
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                BotonAccion {
                    objectName: "botonExportarMes"
                    text: qsTr("Exportar este mes")
                    icono: "arrow-down-circle"
                    enabled: pagina.exportacion.puedeExportar
                    descripcion: qsTr("Guardar las facturas de %1 en un archivo ZIP").arg(pagina.facturas.tituloMes)
                    onClicked: pagina.exportacion.abrirMesActual()
                }
                BotonAccion {
                    objectName: "botonElegirFacturas"
                    text: qsTr("Elegir facturas")
                    icono: "checkmark"
                    enabled: pagina.exportacion.disponible
                    descripcion: qsTr("Marcar facturas para exportarlas")
                    onClicked: {
                        pagina.exportacion.iniciarSeleccion()
                        lista.enfocarSinAnillo(Qt.OtherFocusReason)
                    }
                }
            }
            RowLayout {
                objectName: "barraSeleccionFacturas"
                visible: pagina.conLista && pagina.seleccionando
                spacing: Theme.espacioS
                Layout.fillWidth: true
                Label {
                    objectName: "conteoSeleccionFacturas"
                    text: pagina.exportacion.conteoSeleccionadas === 1 ? qsTr("1 factura elegida")
                          : qsTr("%1 facturas elegidas").arg(pagina.exportacion.conteoSeleccionadas)
                    color: Theme.texto
                    font.family: Theme.familia
                    font.pixelSize: Theme.cuerpo.size
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                BotonAccion {
                    objectName: "botonExportarElegidas"
                    text: qsTr("Exportar elegidas…")
                    variante: "primario"
                    enabled: pagina.exportacion.puedeExportar && pagina.exportacion.conteoSeleccionadas > 0
                    onClicked: pagina.exportacion.abrirSeleccion()
                }
                BotonAccion {
                    objectName: "botonQuitarSeleccion"
                    text: qsTr("Quitar selección")
                    enabled: pagina.exportacion.conteoSeleccionadas > 0
                    onClicked: pagina.exportacion.limpiarSeleccion()
                }
                BotonAccion {
                    objectName: "botonTerminarSeleccion"
                    text: qsTr("Listo")
                    descripcion: qsTr("Salir del modo de selección (Escape)")
                    onClicked: pagina.exportacion.terminarSeleccion()
                }
            }

            // ---- Lista ----
            Rectangle {
                id: contenedorLista
                objectName: "contenedorListaFacturas"
                visible: pagina.conLista
                color: Theme.superficie
                radius: Theme.radioTarjeta
                border.width: 1
                border.color: Theme.separador
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: lista
                    objectName: "listaFacturas"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    // Siempre visible (se oculta con opacidad): una vista
                    // invisible no aplica las bajas del modelo y al destruirse
                    // cancela delegados con indices viejos (aviso de DelegateModel).
                    readonly property bool mostrar: pagina.vista === "lista" || (pagina.vista === "llenandose" && lista.count > 0)
                    opacity: lista.mostrar ? 1 : 0
                    enabled: lista.mostrar
                    Accessible.ignored: !lista.mostrar
                    model: pagina.facturas.modelo
                    activeFocusOnTab: true
                    keyNavigationEnabled: true
                    boundsBehavior: Flickable.StopAtBounds
                    headerPositioning: ListView.OverlayHeader
                    currentIndex: -1

                    // Anillo solo al navegar con teclado o al entrar con Tab: el
                    // foco que pone la pagina (inicial, tras el visor) o el clic
                    // no lo dibujan hasta la siguiente tecla.
                    property bool focoSilencioso: false
                    readonly property bool focoVisible: lista.activeFocus && !lista.focoSilencioso
                    function enfocarSinAnillo(razon) {
                        lista.focoSilencioso = true
                        lista.forceActiveFocus(razon)
                    }
                    onActiveFocusChanged: if (!lista.activeFocus) lista.focoSilencioso = false

                    Accessible.role: Accessible.List
                    Accessible.name: pagina.facturas.direccion === FacturasViewModel.Emitidas
                                     ? qsTr("Facturas emitidas") : qsTr("Facturas recibidas")

                    onCountChanged: Qt.callLater(pagina.ajustarFilaActual)

                    ScrollBar.vertical: ScrollBar { }

                    Keys.onReturnPressed: pagina.previsualizarActual()
                    Keys.onEnterPressed: pagina.previsualizarActual()
                    Keys.onPressed: (evento) => {
                        if (evento.key === Qt.Key_Space && pagina.seleccionando) {
                            evento.accepted = pagina.alternarFilaActual()
                            lista.focoSilencioso = false
                            return
                        }
                        if (evento.matches(StandardKey.Copy)) {
                            evento.accepted = pagina.copiarUuidActual()
                            return
                        }
                        lista.focoSilencioso = false
                        evento.accepted = false
                    }

                    // Cabecera: alto altoControl, esquinas superiores con el radio
                    // interior de la tarjeta y linea separador; columnas como FilaCfdi.
                    header: Rectangle {
                        id: cabecera
                        objectName: "encabezadosFacturas"
                        z: 2
                        width: ListView.view.width
                        height: Theme.altoControl
                        color: Theme.superficieSeccion
                        radius: Theme.radioTarjeta - 1
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
                            // Hueco de la casilla en modo de seleccion.
                            Item {
                                visible: pagina.seleccionando
                                Layout.preferredWidth: 28 + Theme.espacioS - Theme.espacioM
                            }
                            component Columna: Label {
                                color: Theme.textoSecundario
                                font.family: Theme.familia
                                font.pixelSize: Theme.etiqueta.size
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }
                            Columna { text: qsTr("Fecha"); Layout.preferredWidth: 120 - Theme.espacioM }
                            Columna {
                                objectName: "encabezadoContraparte"
                                text: pagina.facturas.encabezadoContraparte
                                Layout.fillWidth: true
                                Layout.preferredWidth: 10
                            }
                            Columna { text: qsTr("Total"); horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 130 - Theme.espacioM }
                            Columna { text: qsTr("UUID"); Layout.preferredWidth: 140 - Theme.espacioM }
                        }
                    }

                    // Solo la contraparte (D20): FilaCfdi muestra la columna
                    // "emisor" con la contraparte y oculta Receptor.
                    delegate: FilaCfdi {
                        id: fila
                        required property int index
                        required property var model

                        objectName: "filaFactura_" + index
                        width: ListView.view.width
                        // La fila actual recibe el foco de la lista: Espacio
                        // en modo de seleccion alterna la fila, no la pulsa.
                        Keys.onSpacePressed: (evento) => {
                            evento.accepted = pagina.seleccionando && pagina.alternarFilaActual()
                        }
                        fecha: fila.model.fecha
                        tipoTexto: fila.model.tipoTexto
                        serieFolio: fila.model.serieFolio
                        emisorRfc: fila.model.contraparteRfc
                        emisorNombre: fila.model.contraparteNombre
                        total: fila.model.total
                        moneda: fila.model.moneda
                        // Segunda linea del total: "MXN · PUE" (FilaCfdi la compone).
                        metodoPago: fila.model.metodoPago
                        uuid: fila.model.uuid
                        uuidCorto: fila.model.uuidCorto
                        estadoParseo: fila.model.estadoParseo
                        estadoParseoTexto: fila.model.estadoParseoTexto
                        nombreAccesible: fila.model.nombreAccesible
                        mostrarReceptor: false
                        seleccionada: ListView.isCurrentItem
                        conFoco: ListView.isCurrentItem && lista.focoVisible
                        // T026.3: casilla por fila; la seleccion es del view
                        // model por identidad (revisionSeleccion reevalua).
                        readonly property bool elegida: pagina.exportacion.revisionSeleccion >= 0
                                                        && pagina.exportacion.estaSeleccionada(fila.model.paqueteId, fila.model.posicion)
                        leftPadding: pagina.seleccionando ? Theme.espacioL + 28 + Theme.espacioS : Theme.espacioL
                        Accessible.name: pagina.seleccionando
                                         ? qsTr("%1, %2").arg(fila.model.nombreAccesible).arg(fila.elegida ? qsTr("elegida") : qsTr("no elegida"))
                                         : fila.model.nombreAccesible
                        Accessible.checkable: pagina.seleccionando
                        Accessible.checked: fila.elegida

                        CheckBox {
                            objectName: "casillaFactura_" + fila.index
                            visible: pagina.seleccionando
                            focusPolicy: Qt.NoFocus
                            padding: 0
                            checked: fila.elegida
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.espacioL - 4
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.name: qsTr("Elegir factura: %1").arg(fila.model.nombreAccesible)
                            onToggled: {
                                pagina.exportacion.alternarSeleccion(fila.model.paqueteId, fila.model.posicion)
                                checked = Qt.binding(() => fila.elegida)
                            }
                        }

                        onClicked: {
                            lista.currentIndex = fila.index
                            lista.enfocarSinAnillo(Qt.MouseFocusReason)
                        }
                        onDoubleClicked: {
                            lista.currentIndex = fila.index
                            pagina.previsualizarActual()
                        }
                    }
                }

                // Llenandose: lista vacia mientras se prepara o indexa, sin
                // estado vacio ni error modal (la franja explica la etapa).
                ColumnLayout {
                    objectName: "estadoLlenandoseFacturas"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                    visible: pagina.vista === "llenandose" && lista.count === 0
                    spacing: Theme.espacioS
                    Accessible.role: Accessible.StaticText
                    Accessible.name: textoLlenandose.text
                    BusyIndicator {
                        running: parent.visible
                        Layout.alignment: Qt.AlignHCenter
                        Accessible.ignored: true
                    }
                    Label {
                        id: textoLlenandose
                        visible: text.length > 0
                        text: pagina.facturas.textoVista
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
                    objectName: "estadoSinFacturas"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                    visible: pagina.vista === "sinFacturas"
                    variante: "vacio"
                    titulo: pagina.facturas.textoVista
                    objectNameTitulo: "tituloSinFacturas"
                }

                EstadoVacio {
                    objectName: "estadoSinCoincidenciasFacturas"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                    visible: pagina.vista === "sinCoincidencias"
                    variante: "vacio"
                    titulo: pagina.facturas.textoVista
                    textoAccion: qsTr("Limpiar filtros")
                    objectNameTitulo: "tituloSinCoincidenciasFacturas"
                    objectNameAccion: "botonLimpiarFiltrosVacioFacturas"
                    onAccionSolicitada: pagina.limpiarFiltros()
                }

                // Cargando: solo si tarda mas de 300 ms.
                // El indicador se muestra cuando vence el Timer (no al crear la
                // pagina): sin animaciones que arrancan y paran en el mismo
                // cuadro si la pagina se cierra enseguida.
                ColumnLayout {
                    id: cargando
                    objectName: "estadoCargandoFacturas"
                    property bool vencido: false
                    anchors.centerIn: parent
                    visible: pagina.vista === "cargando" && cargando.vencido
                    spacing: Theme.espacioS
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("Cargando facturas")
                    Timer {
                        interval: 300
                        running: pagina.vista === "cargando"
                        onRunningChanged: if (!running && pagina.vista !== "cargando") cargando.vencido = false
                        onTriggered: cargando.vencido = true
                    }
                    BusyIndicator {
                        running: parent.visible
                        Layout.alignment: Qt.AlignHCenter
                        Accessible.ignored: true
                    }
                }

                EstadoVacio {
                    id: error
                    objectName: "estadoErrorFacturas"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                    visible: pagina.vista === "error"
                    variante: "error"
                    titulo: pagina.facturas.textoVista
                    textoAccion: qsTr("Reintentar")
                    objectNameTitulo: "errorFacturas"
                    objectNameAccion: "botonReintentarFacturas"
                    onAccionSolicitada: pagina.facturas.recargar()
                }
            }

            // Mes futuro: sin pestanas ni lista (la franja lo explica).
            Item {
                visible: pagina.vista === "futuro"
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }

    // ---- Visor del PDF (T015.4) ----
    VisorPdfCfdi {
        id: visor
        anchors.fill: parent
        visible: pagina.visorAbierto
        vm: pagina.facturas.visor
        onCerrarSolicitado: pagina.facturas.visor.cerrar()
    }

    // ---- Exportacion (T026.3) ----
    DialogoExportarFacturas {
        id: dialogoExportar
        exportacion: pagina.exportacion
    }

    // ---- Confirmacion por direccion (RequiereConfirmacion) ----
    DialogoConfirmacion {
        id: confirmacion
        prefijoNombre: "confirmacionFacturas"
        variante: "advertencia"
        mensaje: pagina.facturas.confirmacionTitulo
        consecuencia: pagina.facturas.confirmacionTexto
        textoConfirmar: pagina.facturas.textoConfirmar
        textoCancelar: pagina.facturas.textoCancelar
        onConfirmado: pagina.facturas.confirmar()
        onCancelado: pagina.facturas.cancelarConfirmacion()
    }
}
