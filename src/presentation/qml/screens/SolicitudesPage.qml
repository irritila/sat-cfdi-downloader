pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Solicitudes (T013, traspaso "SolicitudesPage"; UX-06..UX-13).
// - Encabezado raiz: titulo con contador y acciones Abrir carpeta de paquetes,
//   Perfiles SAT y Nueva solicitud (primaria, al extremo).
// - Avisos sobre la tabla: notificaciones desactivadas (AvisoEnLinea con
//   bell-slash) y el resultado fallido de abrir la carpeta de paquetes.
// - Tabla: encabezados de columna fijos y FilaSolicitud por solicitud (Estado,
//   Contribuyente, Tipo, Periodo, Paquetes, Creada); bajo 960 se oculta Creada.
// - Estados: cargando (BusyIndicator tras 300 ms), vacia y error (EstadoVacio,
//   este ultimo con Reintentar enfocado).
// - T014.1 D1: busqueda (RFC o nombre) y filtros Estado, Tipo y Mes sobre un
//   SolicitudesFiltroModel local (no cambia la consulta ni el orden). Los
//   filtros viven en AppViewModel (se conservan durante la sesion). Sin
//   coincidencias: EstadoVacio con "Limpiar filtros".
// - T014.3 D1: sin solicitudes y con la consulta de primer uso resuelta, el
//   estado vacio es una guia de tres pasos con check (perfil, e.firma, primera
//   solicitud); cada paso abre su pantalla. Mientras carga o si fallo se ve el
//   estado vacio normal.
// Teclado: orden de foco por las acciones del encabezado, la busqueda, los
// filtros y la lista; foco inicial en la lista (primera fila); flechas para
// moverse; Enter/Return/Espacio abre el detalle por id. ⌘F (Main.qml) enfoca
// la busqueda; Flecha abajo pasa de la busqueda a la lista y Escape la vacia.
Page {
    id: pagina
    objectName: "paginaSolicitudes"

    required property AppViewModel app
    required property SolicitudesListModel modelo

    readonly property bool anchoAmplio: pagina.width >= 960
    readonly property bool conDatos: pagina.modelo.estado === SolicitudesListModel.ConDatos
    // T014.1 D5: la lista no abre dialogos propios.
    readonly property bool dialogoAbierto: false
    readonly property bool puedeBuscar: pagina.conDatos
    readonly property alias campoBusqueda: busqueda
    readonly property bool mostrarGuia: pagina.modelo.estado === SolicitudesListModel.Vacia
                                        && pagina.app.mostrarGuiaPrimerUso
    // T014.3 D1: pasos de la guia; `hecho` viene de AppViewModel.primerUsoPasos.
    readonly property var pasosGuia: [
        { nombre: "pasoPerfil", texto: qsTr("Crea un perfil SAT con el RFC del contribuyente"),
          accion: qsTr("Perfiles SAT"), destino: "perfiles", hecho: pagina.app.primerUsoPasos[0] === true },
        { nombre: "pasoEFirma", texto: qsTr("Registra la e.firma del perfil"),
          accion: qsTr("Registrar e.firma"), destino: "perfiles", hecho: pagina.app.primerUsoPasos[1] === true },
        { nombre: "pasoSolicitud", texto: qsTr("Crea tu primera solicitud de descarga"),
          accion: qsTr("Nueva solicitud"), destino: "nueva", hecho: pagina.app.primerUsoPasos[2] === true }
    ]
    readonly property int pasoSiguiente: pagina.pasosGuia.findIndex(p => !p.hecho)
    // Tipo del delegado de la guia (para acceder a su boton con tipo).
    component PasoGuia: Item {
        property Item boton: null
    }

    // T014.1 D1: filtro local enlazado al estado de sesion de AppViewModel.
    SolicitudesFiltroModel {
        id: filtro
        fuente: pagina.modelo
        texto: pagina.app.filtroTexto
        estado: pagina.app.filtroEstado
        tipo: pagina.app.filtroTipo
        mes: pagina.app.filtroMes
    }
    readonly property SolicitudesFiltroModel filtroModelo: filtro

    // Meses del selector: los de los periodos y, si ya no aparece, el elegido.
    readonly property var opcionesMes: {
        const meses = filtro.mesesDisponibles.slice()
        if (pagina.app.filtroMes.length > 0 && meses.indexOf(pagina.app.filtroMes) < 0)
            meses.unshift(pagina.app.filtroMes)
        return [{ clave: "", texto: qsTr("Todos los meses") }].concat(
                    meses.map(m => ({ clave: m, texto: FormatoFechas.mesAnio(m) })))
    }

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Solicitudes de descarga masiva")

    background: Rectangle {
        color: Theme.superficie
    }

    // Foco inicial diferido: la pagina ya esta en la ventana.
    Component.onCompleted: {
        pagina.estadoAnterior = pagina.modelo.estado
        Qt.callLater(pagina.enfocarInicial)
    }

    function enfocarInicial() {
        if (lista.visible && lista.count > 0)
            lista.forceActiveFocus(Qt.TabFocusReason)
        else if (error.visible)
            error.botonAccion.forceActiveFocus(Qt.TabFocusReason)
        else if (pagina.mostrarGuia && pagina.pasoGuia(pagina.pasoSiguiente) !== null)
            pagina.pasoGuia(pagina.pasoSiguiente).boton.forceActiveFocus(Qt.TabFocusReason)
        else
            botonNueva.forceActiveFocus(Qt.TabFocusReason)
    }

    // Si la primera carga termina despues del foco inicial, el foco pasa a la
    // lista (solo desde el estado Cargando, para no mover el foco del usuario).
    // Valor previo sin binding (un binding se actualizaria antes del manejador).
    property int estadoAnterior: -1
    Connections {
        target: pagina.modelo
        function onEstadoChanged() {
            if (pagina.estadoAnterior === SolicitudesListModel.Cargando
                    && pagina.modelo.estado === SolicitudesListModel.ConDatos
                    && botonNueva.activeFocus)
                Qt.callLater(pagina.enfocarListaTrasCarga)
            pagina.estadoAnterior = pagina.modelo.estado
        }
    }

    // T014.3 D1: si la guia aparece despues del foco inicial (en "Nueva
    // solicitud"), el foco pasa al siguiente paso; no se mueve el foco del usuario.
    Connections {
        target: pagina.app
        function onPrimerUsoChanged() { Qt.callLater(pagina.enfocarGuiaTrasCarga) }
    }
    function enfocarGuiaTrasCarga() {
        const paso = pagina.pasoGuia(pagina.pasoSiguiente)
        if (pagina.mostrarGuia && paso !== null && botonNueva.activeFocus)
            paso.boton.forceActiveFocus(Qt.OtherFocusReason)
    }

    // Diferido: la lista filtrada se hace visible cuando el proxy ya tiene filas.
    function enfocarListaTrasCarga() {
        if (lista.visible && botonNueva.activeFocus)
            lista.forceActiveFocus(Qt.OtherFocusReason)
    }

    function pasoGuia(indice) {
        return repetidorPasos.itemAt(Math.max(0, indice)) as PasoGuia
    }

    function enfocarBusqueda() {
        if (!pagina.puedeBuscar)
            return false
        busqueda.forceActiveFocus(Qt.ShortcutFocusReason)
        busqueda.selectAll()
        return true
    }

    function limpiarFiltros() {
        pagina.app.limpiarFiltros()
        busqueda.forceActiveFocus(Qt.OtherFocusReason)
    }

    function abrir(id) {
        if (id)
            pagina.app.abrirDetalle(id)
    }

    header: EncabezadoPagina {
        titulo: qsTr("Solicitudes")
        contador: !pagina.conDatos ? ""
                  : filtro.hayFiltros ? qsTr("%1 de %2").arg(filtro.count).arg(pagina.modelo.count)
                  : String(pagina.modelo.count)

        // T009.1 D1: abre la carpeta de paquetes en Finder (no la crea).
        BotonAccion {
            id: botonCarpetaPaquetes
            objectName: "botonAbrirCarpetaPaquetes"
            visible: pagina.app.puedeAbrirCarpetaPaquetes
            icono: "folder"
            text: qsTr("Abrir carpeta de paquetes")
            descripcion: qsTr("Mostrar en Finder la carpeta donde se guardan los paquetes descargados")
            onClicked: pagina.app.abrirCarpetaPaquetes()
        }
        BotonAccion {
            id: botonPerfiles
            objectName: "botonPerfilesSat"
            icono: "person-card"
            text: qsTr("Perfiles SAT")
            descripcion: qsTr("Administrar perfiles SAT y su e.firma")
            atajo: "⌘2"
            onClicked: pagina.app.mostrarPerfiles()
        }
        BotonAccion {
            id: botonNueva
            objectName: "botonNuevaSolicitud"
            variante: "primario"
            icono: "plus"
            text: qsTr("Nueva solicitud")
            descripcion: qsTr("Abrir el formulario de nueva solicitud")
            atajo: "⌘N"
            onClicked: pagina.app.mostrarNueva()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Avisos sobre la tabla ----
        Rectangle {
            // Condiciones directas: la visibilidad efectiva de los hijos depende de esta.
            visible: pagina.app.notificacionesDeshabilitadas || pagina.app.mensajeFinder.length > 0
            color: Theme.superficie
            Layout.fillWidth: true
            implicitHeight: avisos.implicitHeight + 24

            ColumnLayout {
                id: avisos
                anchors.fill: parent
                anchors.leftMargin: Theme.espacioL
                anchors.rightMargin: Theme.espacioL
                anchors.topMargin: 12
                anchors.bottomMargin: 12
                spacing: Theme.espacioS

                // T009.1 D7: aviso accesible si no se pudo abrir la carpeta de paquetes.
                AvisoEnLinea {
                    id: mensajeFinder
                    objectName: "mensajeFinderLista"
                    visible: pagina.app.mensajeFinder.length > 0
                    variante: "error"
                    icono: "folder"
                    titulo: pagina.app.mensajeFinder
                    Layout.fillWidth: true
                }
                // T009 D9: notificaciones deshabilitadas en macOS (no afecta el flujo).
                AvisoEnLinea {
                    id: avisoNotificaciones
                    objectName: "avisoNotificaciones"
                    visible: pagina.app.notificacionesDeshabilitadas
                    variante: "advertencia"
                    icono: "bell-slash"
                    anunciar: false
                    titulo: qsTr("Las notificaciones están desactivadas.")
                    descripcion: qsTr("Actívalas en Ajustes del Sistema para recibir avisos cuando una solicitud termine o falle.")
                    Layout.fillWidth: true
                }
            }
        }

        // ---- Busqueda y filtros (T014.1 D1) ----
        Rectangle {
            objectName: "barraFiltros"
            visible: pagina.conDatos
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
                    objectName: "campoBusqueda"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 160
                    Layout.maximumWidth: 320
                    leftPadding: 28
                    rightPadding: limpiarBusqueda.visible ? 28 : 8
                    text: pagina.app.filtroTexto
                    placeholderText: qsTr("Buscar por RFC o nombre")
                    onTextEdited: pagina.app.filtroTexto = text
                    Accessible.name: qsTr("Buscar solicitudes por RFC o nombre del perfil")
                    Accessible.description: qsTr("Atajo: %1").arg("⌘F")
                    ToolTip.visible: busqueda.hovered && !busqueda.activeFocus
                    ToolTip.text: qsTr("Buscar (%1)").arg("⌘F")
                    ToolTip.delay: 600
                    Keys.onDownPressed: (evento) => {
                        if (lista.visible) {
                            lista.forceActiveFocus(Qt.TabFocusReason)
                            evento.accepted = true
                        } else {
                            evento.accepted = false
                        }
                    }
                    Keys.onEscapePressed: (evento) => {
                        if (busqueda.text.length > 0) {
                            pagina.app.filtroTexto = ""
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
                        objectName: "botonLimpiarBusqueda"
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
                            pagina.app.filtroTexto = ""
                            busqueda.forceActiveFocus(Qt.OtherFocusReason)
                        }
                    }
                }
                CampoCombo {
                    id: filtroEstado
                    objectName: "filtroEstado"
                    Layout.preferredWidth: 176
                    focusPolicy: Qt.StrongFocus
                    textRole: "texto"
                    valueRole: "clave"
                    model: [
                        { clave: "", texto: qsTr("Todos los estados") },
                        { clave: "Creada", texto: qsTr("Creada") },
                        { clave: "Enviando", texto: qsTr("Enviando") },
                        { clave: "Enviada", texto: qsTr("Enviada") },
                        { clave: "EnvioFallido", texto: qsTr("Envío fallido") },
                        { clave: "EnvioIncierto", texto: qsTr("Envío incierto") },
                        { clave: "Aceptada", texto: qsTr("Aceptada por SAT") },
                        { clave: "EnProceso", texto: qsTr("En proceso SAT") },
                        { clave: "Terminada", texto: qsTr("Terminada") },
                        { clave: "ErrorSat", texto: qsTr("Error SAT") },
                        { clave: "Rechazada", texto: qsTr("Rechazada por SAT") },
                        { clave: "Vencida", texto: qsTr("Vencida") }
                    ]
                    currentIndex: count > 0 ? indexOfValue(pagina.app.filtroEstado) : -1
                    onActivated: (indice) => { pagina.app.filtroEstado = valueAt(indice) }
                    Accessible.name: qsTr("Filtrar por estado")
                    Accessible.description: displayText
                }
                CampoCombo {
                    id: filtroTipo
                    objectName: "filtroTipo"
                    Layout.preferredWidth: 140
                    focusPolicy: Qt.StrongFocus
                    textRole: "texto"
                    valueRole: "clave"
                    model: [
                        { clave: "", texto: qsTr("Todos los tipos") },
                        { clave: "Emitidos", texto: qsTr("Emitidos") },
                        { clave: "Recibidos", texto: qsTr("Recibidos") }
                    ]
                    currentIndex: count > 0 ? indexOfValue(pagina.app.filtroTipo) : -1
                    onActivated: (indice) => { pagina.app.filtroTipo = valueAt(indice) }
                    Accessible.name: qsTr("Filtrar por tipo de descarga")
                    Accessible.description: displayText
                }
                CampoCombo {
                    id: filtroMes
                    objectName: "filtroMes"
                    Layout.preferredWidth: 150
                    focusPolicy: Qt.StrongFocus
                    textRole: "texto"
                    valueRole: "clave"
                    model: pagina.opcionesMes
                    currentIndex: count > 0 ? indexOfValue(pagina.app.filtroMes) : -1
                    onActivated: (indice) => { pagina.app.filtroMes = valueAt(indice) }
                    Accessible.name: qsTr("Filtrar por mes del periodo")
                    Accessible.description: displayText
                }
                BotonAccion {
                    id: botonLimpiarFiltros
                    objectName: "botonLimpiarFiltros"
                    visible: filtro.hayFiltros
                    variante: "secundario"
                    text: qsTr("Limpiar filtros")
                    descripcion: qsTr("Quitar la búsqueda y los filtros")
                    onClicked: pagina.limpiarFiltros()
                }
                Item { Layout.fillWidth: true }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.separador
            }
        }

        // ---- Tabla ----
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: lista
                objectName: "listaSolicitudes"
                anchors.fill: parent
                clip: true
                visible: pagina.conDatos && filtro.count > 0
                model: filtro
                currentIndex: count > 0 ? 0 : -1
                activeFocusOnTab: true
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                headerPositioning: ListView.OverlayHeader
                // T014.1 D1: sin delegados incubados fuera de pantalla; el filtro
                // local quita filas mientras se crean (evita solicitudes de
                // indices que ya no existen en el modelo filtrado).
                cacheBuffer: 0

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Lista de solicitudes, %n elemento(s)", "", count)

                ScrollBar.vertical: ScrollBar { }

                Keys.onReturnPressed: pagina.abrir(filtro.idEn(lista.currentIndex))
                Keys.onEnterPressed: pagina.abrir(filtro.idEn(lista.currentIndex))
                Keys.onSpacePressed: pagina.abrir(filtro.idEn(lista.currentIndex))

                header: Rectangle {
                    objectName: "encabezadosColumnas"
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
                                { texto: qsTr("Estado"), ancho: 176 - Theme.espacioM, estirar: 0 },
                                { texto: qsTr("Contribuyente"), ancho: 15, estirar: 15 },
                                { texto: qsTr("Tipo"), ancho: 112 - Theme.espacioM, estirar: 0 },
                                { texto: qsTr("Periodo"), ancho: 11, estirar: 11 },
                                { texto: qsTr("Paquetes"), ancho: 72 - Theme.espacioM, estirar: 0, derecha: true },
                                { texto: qsTr("Creada"), ancho: 150 - Theme.espacioM, estirar: 0, creada: true }
                            ]
                            delegate: Label {
                                required property var modelData
                                visible: !modelData.creada || pagina.anchoAmplio
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
                        // Columna del chevron.
                        Item { Layout.preferredWidth: 16 }
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.separador
                    }
                }

                delegate: FilaSolicitud {
                    id: fila
                    required property int index
                    required property var model

                    objectName: "filaSolicitud_" + index
                    width: ListView.view.width
                    estado: model.estadoResumen
                    rfc: model.perfilRfc
                    nombrePerfil: model.perfilNombre
                    tipoDescarga: model.tipoDescarga
                    fechaInicial: model.fechaInicial
                    fechaFinal: model.fechaFinal
                    totalPaquetes: model.totalPaquetes
                    paquetesDescargados: model.paquetesDescargados
                    paquetesPendientes: model.paquetesPendientesDescarga
                    creadaEn: model.creadaEn
                    mostrarCreada: pagina.anchoAmplio
                    seleccionada: ListView.isCurrentItem
                    conFoco: ListView.isCurrentItem && lista.activeFocus

                    onClicked: {
                        lista.currentIndex = index
                        pagina.abrir(model.id)
                    }
                }
            }

            // Estado: vacia
            EstadoVacio {
                id: vacio
                objectName: "estadoVacio"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                visible: pagina.modelo.estado === SolicitudesListModel.Vacia && !pagina.mostrarGuia
                variante: "vacio"
                titulo: qsTr("No hay solicitudes")
                descripcion: qsTr("Crea una solicitud para descargar del SAT los CFDI emitidos o recibidos de un contribuyente.")
                textoAccion: qsTr("Nueva solicitud")
                accionPrimaria: true
                objectNameAccion: "botonNuevaSolicitudVacia"
                onAccionSolicitada: pagina.app.mostrarNueva()
            }

            // T014.1 D1: hay solicitudes, pero ninguna coincide con los filtros.
            EstadoVacio {
                id: sinCoincidencias
                objectName: "estadoSinCoincidencias"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                visible: pagina.conDatos && filtro.count === 0
                variante: "vacio"
                titulo: qsTr("Ninguna solicitud coincide con la búsqueda")
                descripcion: qsTr("Cambia la búsqueda o los filtros, o límpialos para ver todas las solicitudes.")
                textoAccion: qsTr("Limpiar filtros")
                objectNameTitulo: "tituloSinCoincidencias"
                objectNameAccion: "botonLimpiarFiltrosVacio"
                onAccionSolicitada: pagina.limpiarFiltros()
            }

            // T014.3 D1: guia de primer uso (lista vacia y consulta resuelta).
            EstadoVacio {
                id: guia
                objectName: "guiaPrimerUso"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.espacioXxl, 480)
                visible: pagina.mostrarGuia
                variante: "vacio"
                titulo: qsTr("Empieza en tres pasos")
                descripcion: qsTr("Para descargar CFDI del SAT necesitas un perfil con su e.firma registrada.")
                objectNameTitulo: "tituloGuiaPrimerUso"

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.espacioM
                    implicitHeight: listaPasos.implicitHeight
                    radius: Theme.radioTarjeta
                    color: Theme.superficie
                    border.width: 1
                    border.color: Theme.separador

                    ColumnLayout {
                        id: listaPasos
                        width: parent.width
                        spacing: 0

                        Repeater {
                            id: repetidorPasos
                            model: pagina.pasosGuia
                            delegate: PasoGuia {
                                id: paso
                                required property var modelData
                                required property int index
                                boton: botonPaso
                                readonly property bool siguiente: paso.index === pagina.pasoSiguiente

                                objectName: paso.modelData.nombre
                                Layout.fillWidth: true
                                implicitHeight: Math.max(textosPaso.implicitHeight, botonPaso.implicitHeight) + 2 * Theme.espacioM
                                Accessible.role: Accessible.ListItem
                                Accessible.name: qsTr("Paso %1 de 3: %2, %3").arg(paso.index + 1).arg(paso.modelData.texto)
                                                 .arg(paso.modelData.hecho ? qsTr("hecho") : qsTr("pendiente"))

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.espacioM
                                    anchors.rightMargin: Theme.espacioM
                                    spacing: Theme.espacioM

                                    Icono {
                                        objectName: "iconoPaso"
                                        nombre: paso.modelData.hecho ? "check-circle" : "circle-dashed"
                                        color: paso.modelData.hecho ? Theme.exito : Theme.textoSecundario
                                        tamano: 18
                                    }
                                    ColumnLayout {
                                        id: textosPaso
                                        spacing: 0
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("Paso %1").arg(paso.index + 1)
                                            color: Theme.textoSecundario
                                            font.family: Theme.familia
                                            font.pixelSize: Theme.etiqueta.size
                                            font.weight: Font.DemiBold
                                            Accessible.ignored: true
                                        }
                                        Label {
                                            objectName: "textoPaso"
                                            text: paso.modelData.texto
                                            color: paso.modelData.hecho ? Theme.textoSecundario : Theme.texto
                                            font.family: Theme.familia
                                            font.pixelSize: Theme.cuerpo.size
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                            Accessible.ignored: true
                                        }
                                    }
                                    BotonAccion {
                                        id: botonPaso
                                        objectName: "boton_" + paso.modelData.nombre
                                        compacto: true
                                        variante: paso.siguiente ? "primario" : "secundario"
                                        text: paso.modelData.accion
                                        descripcion: paso.modelData.texto
                                        onClicked: {
                                            if (paso.modelData.destino === "nueva")
                                                pagina.app.mostrarNueva()
                                            else
                                                pagina.app.mostrarPerfiles()
                                        }
                                    }
                                }
                                Rectangle {
                                    visible: paso.index < 2
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 1
                                    color: Theme.separador
                                }
                            }
                        }
                    }
                }
            }

            // Estado: cargando (solo si tarda mas de 300 ms)
            ColumnLayout {
                id: cargando
                objectName: "estadoCargando"
                anchors.centerIn: parent
                // Visible solo si la carga sigue tras 300 ms (el retraso ya disparo).
                visible: pagina.modelo.estado === SolicitudesListModel.Cargando && !retraso.running
                spacing: Theme.espacioS
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Cargando solicitudes")

                Timer {
                    id: retraso
                    interval: 300
                    running: pagina.modelo.estado === SolicitudesListModel.Cargando
                }
                BusyIndicator {
                    running: parent.visible
                    Layout.alignment: Qt.AlignHCenter
                    Accessible.ignored: true
                }
            }

            // Estado: error
            EstadoVacio {
                id: error
                objectName: "estadoError"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                visible: pagina.modelo.estado === SolicitudesListModel.Error
                variante: "error"
                titulo: pagina.modelo.errorMessage
                descripcion: qsTr("Tus solicitudes siguen guardadas en este equipo. Intenta cargarlas de nuevo.")
                textoAccion: qsTr("Reintentar")
                objectNameTitulo: "errorLista"
                objectNameAccion: "botonReintentarLista"
                onAccionSolicitada: pagina.modelo.refrescar()
            }
        }
    }
}
