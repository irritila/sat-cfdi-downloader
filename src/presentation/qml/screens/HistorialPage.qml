pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Historial de descargas del perfil (T023.5, ⌘3; T023 D6, D14; prototipo
// ajustado de T023.1). Solo representa HistorialViewModel: resultado, textos,
// conteos y acciones disponibles vienen de C++ (sin "solicitud", "paquete" ni
// "verificacion").
// - Encabezado: "‹ <origen>" (Atras, AppViewModel.regresar), titulo y el
//   perfil como subtitulo.
// - Una fila por descarga, mas reciente primero: mes (o rango) y fecha,
//   EstadoBadge del resultado (icono y texto), conteos y detalle; acciones
//   Ver facturas, Reintentar y Ver detalles tecnicos (con eligeDireccion, un
//   menu pregunta "Emitidas" o "Recibidas").
// - Estados: cargando (tras 300 ms), error con "Reintentar", vacio y sin perfil.
// Teclado: la lista toma el foco (sin anillo hasta la primera tecla); flechas
// mueven la fila; Enter = Ver facturas (o Ver detalles tecnicos si no hay
// facturas que ver); Tab pasa a los botones de la fila actual; Escape regresa.
Page {
    id: pagina
    objectName: "paginaHistorial"

    required property AppViewModel app
    required property HistorialViewModel historial

    readonly property string vista: pagina.historial.vista
    // T014.1 D5: el menu de direccion cuenta como dialogo (los atajos no actuan).
    readonly property bool dialogoAbierto: menuDireccion.visible
    readonly property alias lista: lista
    readonly property alias menuDireccion: menuDireccion

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Historial de descargas")

    background: Rectangle {
        color: Theme.fondo
    }

    Component.onCompleted: Qt.callLater(pagina.enfocarInicial)

    function enfocarInicial() {
        if (lista.mostrar && lista.count > 0)
            lista.enfocarSinAnillo(Qt.OtherFocusReason)
        else if (error.visible)
            error.botonAccion.forceActiveFocus(Qt.TabFocusReason)
        else
            encabezado.botonRegresar.forceActiveFocus(Qt.OtherFocusReason)
    }

    // Si la lista llega despues del foco inicial (en "Atras"), el foco pasa a ella.
    onVistaChanged: {
        if (pagina.vista === "lista" && encabezado.botonRegresar.activeFocus)
            Qt.callLater(pagina.enfocarInicial)
    }

    function regresar() {
        pagina.app.regresar()
    }

    // Ver detalles tecnicos de la fila; con dos direcciones, pregunta.
    function verDetalles(fila, origen) {
        const item = lista.itemAtIndex(fila) as FilaHistorial
        if (item === null || !item.puedeVerDetalles)
            return
        if (item.eligeDireccion) {
            menuDireccion.fila = fila
            menuDireccion.popup(origen !== undefined && origen !== null ? origen : item, 0, item.height)
        } else {
            pagina.historial.verDetallesTecnicos(fila, HistorialViewModel.Ninguna)
        }
    }

    function accionPrincipalDeFila(fila) {
        const item = lista.itemAtIndex(fila) as FilaHistorial
        if (item === null)
            return
        if (item.puedeVerFacturas)
            pagina.historial.verFacturas(fila)
        else if (item.puedeVerDetalles)
            pagina.verDetalles(fila, item)
    }

    // Anuncio una vez por carga (live region).
    Connections {
        target: pagina.historial
        function onAnuncioChanged() {
            if (pagina.historial.anuncio.length > 0)
                lista.Accessible.announce(pagina.historial.anuncio)
        }
    }

    Keys.onEscapePressed: pagina.regresar()

    header: EncabezadoPagina {
        id: encabezado
        titulo: qsTr("Historial")
        subtitulo: pagina.historial.subtitulo
        mostrarRegresar: true
        textoRegresar: pagina.app.textoRegresar.length > 0 ? pagina.app.textoRegresar : qsTr("Facturas")
        descripcionRegresar: qsTr("Regresar a %1 (Escape)").arg(encabezado.textoRegresar)
        atajoRegresar: ""
        onRegresarSolicitado: pagina.regresar()
    }

    // Tipo del delegado (acceso con tipo desde la pagina y las pruebas).
    component FilaHistorial: Rectangle {
        property bool puedeVerFacturas: false
        property bool puedeReintentar: false
        property bool puedeVerDetalles: false
        property bool eligeDireccion: false
        property Item botonDetalles: null
    }

    Rectangle {
        id: tarjeta
        objectName: "tarjetaHistorial"
        anchors.fill: parent
        anchors.margins: Theme.espacioL
        color: Theme.superficie
        radius: Theme.radioTarjeta
        border.width: 1
        border.color: Theme.separador
        clip: true

        ListView {
            id: lista
            objectName: "listaHistorial"
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            model: pagina.historial.modelo
            // Siempre visible (se oculta con opacidad), como la lista de Facturas.
            readonly property bool mostrar: pagina.vista === "lista" || (pagina.historial.actualizando && lista.count > 0)
            opacity: lista.mostrar ? 1 : 0
            enabled: lista.mostrar
            activeFocusOnTab: true
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            currentIndex: -1
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Historial de descargas")
            Accessible.ignored: !lista.mostrar
            ScrollBar.vertical: ScrollBar { }

            // Anillo solo con teclado (el foco inicial y el clic no lo dibujan).
            property bool focoSilencioso: false
            readonly property bool focoVisible: lista.activeFocus && !lista.focoSilencioso
            function enfocarSinAnillo(razon) {
                lista.focoSilencioso = true
                lista.forceActiveFocus(razon)
            }
            onActiveFocusChanged: if (!lista.activeFocus) lista.focoSilencioso = false
            onCountChanged: Qt.callLater(lista.ajustarFila)
            function ajustarFila() {
                if (lista.count > 0 && lista.currentIndex < 0)
                    lista.currentIndex = 0
                else if (lista.currentIndex >= lista.count)
                    lista.currentIndex = lista.count - 1
            }

            Keys.onReturnPressed: pagina.accionPrincipalDeFila(lista.currentIndex)
            Keys.onEnterPressed: pagina.accionPrincipalDeFila(lista.currentIndex)
            Keys.onPressed: (evento) => {
                lista.focoSilencioso = false
                evento.accepted = false
            }

            delegate: FilaHistorial {
                id: fila
                required property int index
                required property string titulo
                required property string textoFecha
                required property string resultadoTexto
                required property string tono
                required property string icono
                required property string textoConteos
                required property string detalle
                required property bool eliminacionParcial
                required property string nombreAccesible
                required puedeVerFacturas
                required puedeReintentar
                required puedeVerDetalles
                required eligeDireccion

                readonly property bool actual: ListView.isCurrentItem
                // Botones de la fila: alcanzables con Tab solo en la fila actual.
                // Bajo 640 de ancho las acciones pasan a una segunda linea.
                readonly property bool angosta: fila.width < 640

                objectName: "filaHistorial_" + fila.index
                botonDetalles: botonDetalles
                width: ListView.view.width
                implicitHeight: Math.max(Theme.altoFila, contenido.implicitHeight + 2 * Theme.espacioM)
                color: fila.actual ? Theme.seleccion : areaFila.containsMouse ? Theme.superficieSeccion : "transparent"
                Accessible.role: Accessible.ListItem
                Accessible.name: fila.nombreAccesible
                Accessible.selected: fila.actual

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.separador
                }
                AnilloFoco {
                    interior: true
                    visible: fila.actual && lista.focoVisible
                }
                MouseArea {
                    id: areaFila
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        lista.currentIndex = fila.index
                        lista.enfocarSinAnillo(Qt.MouseFocusReason)
                    }
                    onDoubleClicked: pagina.accionPrincipalDeFila(fila.index)
                }

                GridLayout {
                    id: contenido
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.espacioL
                    anchors.rightMargin: Theme.espacioL
                    columns: fila.angosta ? 2 : 3
                    columnSpacing: Theme.espacioM
                    rowSpacing: Theme.espacioS

                    // Mes y fecha: columna de ancho fijo para alinear los
                    // resultados (196 cabe "Descarga del 10 may 2026, 13:05";
                    // en la lista angosta baja a 150 y la fecha se elide).
                    ColumnLayout {
                        readonly property int ancho: lista.width < 720 ? 150 : 196
                        spacing: Theme.espacioXs / 2
                        Layout.preferredWidth: ancho
                        Layout.minimumWidth: ancho
                        Layout.maximumWidth: ancho
                        Label {
                            objectName: "tituloFila"
                            text: fila.titulo
                            color: Theme.texto
                            font.family: Theme.familia
                            font.pixelSize: Theme.cuerpoFuerte.size
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                        Label {
                            objectName: "fechaFila"
                            text: fila.textoFecha
                            color: Theme.textoSecundario
                            font.family: Theme.familia
                            font.pixelSize: Theme.leyenda.size
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                    }
                    ColumnLayout {
                        spacing: Theme.espacioXs
                        Layout.fillWidth: true
                        EstadoBadge {
                            objectName: "badgeResultado"
                            texto: fila.resultadoTexto
                            tonoDirecto: fila.tono.length > 0 ? fila.tono : "neutro"
                            iconoDirecto: fila.icono
                            contexto: qsTr("Resultado")
                        }
                        Label {
                            objectName: "conteosFila"
                            text: [fila.textoConteos, fila.detalle].filter(t => t.length > 0).join(" · ")
                            color: Theme.textoSecundario
                            font.family: Theme.familia
                            font.pixelSize: Theme.leyenda.size
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                    }
                    RowLayout {
                        objectName: "accionesFila"
                        spacing: Theme.espacioS
                        Layout.columnSpan: fila.angosta ? 2 : 1
                        Layout.alignment: fila.angosta ? Qt.AlignLeft : Qt.AlignRight
                        BotonAccion {
                            objectName: "botonVerFacturas"
                            visible: fila.puedeVerFacturas
                            text: qsTr("Ver facturas")
                            compacto: true
                            // Nunca pierde la politica mientras tiene el foco.
                            focusPolicy: fila.actual || activeFocus ? Qt.StrongFocus : Qt.NoFocus
                            descripcion: fila.titulo
                            onClicked: pagina.historial.verFacturas(fila.index)
                        }
                        BotonAccion {
                            objectName: "botonReintentar"
                            visible: fila.puedeReintentar
                            text: qsTr("Reintentar")
                            compacto: true
                            // Nunca pierde la politica mientras tiene el foco.
                            focusPolicy: fila.actual || activeFocus ? Qt.StrongFocus : Qt.NoFocus
                            descripcion: fila.titulo
                            onClicked: pagina.historial.reintentar(fila.index)
                        }
                        BotonAccion {
                            id: botonDetalles
                            objectName: "botonDetalles"
                            visible: fila.puedeVerDetalles
                            text: fila.eligeDireccion ? qsTr("Ver detalles técnicos…") : qsTr("Ver detalles técnicos")
                            variante: "plano"
                            compacto: true
                            // Nunca pierde la politica mientras tiene el foco.
                            focusPolicy: fila.actual || activeFocus ? Qt.StrongFocus : Qt.NoFocus
                            descripcion: fila.titulo
                            onClicked: {
                                lista.currentIndex = fila.index
                                pagina.verDetalles(fila.index, botonDetalles)
                            }
                        }
                    }
                }
            }
        }

        // Cargando (solo si tarda mas de 300 ms).
        // Se muestra cuando vence el Timer (no al crear la pagina): sin
        // animaciones que arrancan y paran en el mismo cuadro.
        BusyIndicator {
            id: cargando
            objectName: "cargandoHistorial"
            property bool vencido: false
            anchors.centerIn: parent
            visible: pagina.vista === "cargando" && cargando.vencido
            running: visible
            Accessible.name: qsTr("Cargando historial")
            Timer {
                interval: 300
                running: pagina.vista === "cargando"
                onRunningChanged: if (!running && pagina.vista !== "cargando") cargando.vencido = false
                onTriggered: cargando.vencido = true
            }
        }

        EstadoVacio {
            id: vacio
            objectName: "estadoVacioHistorial"
            anchors.centerIn: parent
            width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
            visible: pagina.vista === "vacio" || pagina.vista === "sinPerfil"
            variante: "vacio"
            titulo: pagina.historial.textoVista.length > 0 ? pagina.historial.textoVista
                                                           : qsTr("Aún no hay descargas de este perfil")
            descripcion: qsTr("Las descargas se inician desde Facturas, por perfil y mes.")
            textoAccion: qsTr("Ir a Facturas")
            objectNameTitulo: "tituloVacioHistorial"
            objectNameAccion: "botonIrAFacturasHistorial"
            onAccionSolicitada: pagina.app.mostrarFacturas()
        }

        EstadoVacio {
            id: error
            objectName: "estadoErrorHistorial"
            anchors.centerIn: parent
            width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
            visible: pagina.vista === "error"
            variante: "error"
            titulo: pagina.historial.textoVista
            textoAccion: qsTr("Reintentar")
            objectNameTitulo: "errorHistorial"
            objectNameAccion: "botonReintentarHistorial"
            onAccionSolicitada: pagina.historial.recargar()
        }
    }

    // Direccion para "Ver detalles tecnicos…" (lotes con emitidas y recibidas).
    Menu {
        id: menuDireccion
        objectName: "menuDireccion"
        property int fila: -1
        MenuItem {
            objectName: "opcionEmitidas"
            text: qsTr("Emitidas")
            onTriggered: pagina.historial.verDetallesTecnicos(menuDireccion.fila, HistorialViewModel.Emitidas)
        }
        MenuItem {
            objectName: "opcionRecibidas"
            text: qsTr("Recibidas")
            onTriggered: pagina.historial.verDetallesTecnicos(menuDireccion.fila, HistorialViewModel.Recibidas)
        }
    }
}
