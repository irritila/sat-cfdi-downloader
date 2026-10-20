pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

import SatCfdiDownloader

// Ventana del prototipo T023.1. Mismo shell que la app (paleta desde Theme,
// menu nativo solo en cocoa) con el view model falso VmPrototipo. ⌘⇧E abre
// el panel de escenarios. Fuera de Diagnostico no se usan los terminos del
// servicio SAT (T023.1, segundo criterio).
ApplicationWindow {
    id: ventana
    objectName: "ventanaPrototipo"

    width: 960
    height: 640
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: qsTr("Archivo Fiscal (prototipo T023)")

    color: Theme.fondo
    palette.window: Theme.fondo
    palette.windowText: Theme.texto
    palette.base: Theme.controlFondo
    palette.alternateBase: Theme.superficieSeccion
    palette.text: Theme.texto
    palette.button: Theme.controlSecundario
    palette.buttonText: Theme.texto
    palette.highlight: Theme.acento
    palette.highlightedText: Theme.textoSobreAcento
    palette.placeholderText: Theme.textoSecundario
    palette.toolTipBase: Theme.superficieElevada
    palette.toolTipText: Theme.texto
    palette.mid: Theme.borde
    palette.dark: Theme.borde
    palette.light: Theme.superficie
    palette.midlight: Theme.separador

    VmPrototipo { id: vm }

    readonly property bool textoConFoco: ventana.activeFocusItem !== null
                                         && ventana.activeFocusItem instanceof TextInput
    readonly property FacturasPage paginaFacturas: paginaActual.item as FacturasPage

    // ---- Escenarios (T023.1, primer criterio) ----
    readonly property var escenarios: [
        { clave: "sin-perfiles", texto: qsTr("Sin perfiles (guía de primer uso)") },
        { clave: "sin-efirma", texto: qsTr("Perfil sin e.firma") },
        { clave: "sin-descargar", texto: qsTr("Septiembre: Sin descargar") },
        { clave: "preparando", texto: qsTr("Septiembre: Preparando, lista llenándose") },
        { clave: "disponibles", texto: qsTr("Agosto: Disponibles (recibidas)") },
        { clave: "busqueda", texto: qsTr("Agosto: búsqueda con resultados") },
        { clave: "sin-coincidencias", texto: qsTr("Agosto: búsqueda sin coincidencias") },
        { clave: "pdf", texto: qsTr("Agosto: PDF simulado de una recibida") },
        { clave: "parcial", texto: qsTr("Abril: Disponibles parcialmente (solo emitidas)") },
        { clave: "mes-en-curso", texto: qsTr("Octubre: mes en curso, Actualizar mes") },
        { clave: "cerrado-incompleto", texto: qsTr("Mayo: mes cerrado incompleto, Completar mes") },
        { clave: "requiere-atencion", texto: qsTr("Junio: Requiere atención") },
        { clave: "historial", texto: qsTr("Historial") },
        { clave: "diagnostico-mes", texto: qsTr("Diagnóstico de junio") },
        { clave: "diagnostico", texto: qsTr("Ir > Diagnóstico") },
        { clave: "notificacion", texto: qsTr("Notificación que abre un mes") },
        { clave: "cambio-perfil", texto: qsTr("Cambio de perfil y de mes") }
    ]

    function clavesEscenarios() {
        return ventana.escenarios.map(e => e.clave)
    }

    function aplicarEscenario(clave) {
        panel.close()
        vm.reiniciar()
        switch (clave) {
        case "sin-perfiles":
            vm.perfiles = []
            break
        case "sin-efirma":
            vm.seleccionarPerfil(1)
            break
        case "preparando": {
            const m = vm.mesDe("XAXX010101000", 2026, 9)
            vm.iniciarDescarga("descargar")
            vm.temporizador.stop()
            vm.simulacion = null
            m.etapa = 2
            m.visE = 9
            m.visR = 14
            vm.refrescar()
            break
        }
        case "disponibles":
            vm.seleccionarMes(2026, 8)
            vm.direccion = "recibidas"
            break
        case "busqueda":
            vm.seleccionarMes(2026, 8)
            vm.direccion = "recibidas"
            vm.busqueda = "Servicios"
            break
        case "sin-coincidencias":
            vm.seleccionarMes(2026, 8)
            vm.direccion = "recibidas"
            vm.busqueda = "Ferretería Luna"
            break
        case "pdf":
            vm.seleccionarMes(2026, 8)
            vm.direccion = "recibidas"
            vm.pdf = vm.recibidasVisibles[0]
            break
        case "parcial":
            vm.seleccionarMes(2026, 4)
            break
        case "mes-en-curso":
            vm.seleccionarMes(2026, 10)
            break
        case "cerrado-incompleto":
            vm.seleccionarMes(2026, 5)
            break
        case "requiere-atencion":
            vm.seleccionarMes(2026, 6)
            vm.direccion = "recibidas"
            break
        case "historial":
            vm.irA("historial")
            break
        case "diagnostico-mes":
            vm.seleccionarMes(2026, 6)
            vm.abrirDiagnostico({ rfc: "XAXX010101000", anio: 2026, mes: 6 })
            break
        case "diagnostico":
            vm.abrirDiagnostico(null)
            break
        case "notificacion": {
            vm.seleccionarMes(2026, 8)
            const m = vm.mesDe("XAXX010101000", 2026, 9)
            vm.seleccionarMes(2026, 9)
            vm.iniciarDescarga("descargar")
            vm.temporizador.stop()
            m.etapa = 2
            m.visE = m.emitidas.length
            m.visR = m.recibidas.length
            vm.terminarSimulacion(m, vm.simulacion)
            vm.seleccionarMes(2026, 8)
            vm.refrescar()
            break
        }
        case "cambio-perfil":
            vm.seleccionarMes(2026, 7)
            vm.seleccionarPerfil(1)
            vm.seleccionarMes(2026, 7)
            break
        }
    }

    // ---- Acciones y atajos (T023: ⌘1/2/3, ⌘R, ⌘F, ⌘↑/↓) ----
    Action {
        id: accionFacturas
        text: qsTr("Facturas")
        shortcut: "Ctrl+1"
        onTriggered: vm.irA("facturas")
    }
    Action {
        id: accionPerfiles
        text: qsTr("Perfiles SAT")
        shortcut: "Ctrl+2"
        onTriggered: vm.irA("perfiles")
    }
    Action {
        id: accionHistorial
        text: qsTr("Historial")
        shortcut: "Ctrl+3"
        enabled: vm.hayPerfiles
        onTriggered: vm.irA("historial")
    }
    Action {
        id: accionDiagnostico
        text: qsTr("Diagnóstico")
        enabled: vm.hayPerfiles
        onTriggered: vm.abrirDiagnostico(null)
    }
    Action {
        id: accionMes
        text: vm.franja.textoAccion.length > 0 ? vm.franja.textoAccion : qsTr("Descargar facturas")
        shortcut: "Ctrl+R"
        enabled: vm.pagina === "facturas" && !ventana.textoConFoco && vm.franja.accion.length > 0 && vm.franja.habilitada
        onTriggered: vm.accionFranja()
    }
    Action {
        id: accionBuscar
        text: qsTr("Buscar")
        shortcut: "Ctrl+F"
        enabled: vm.pagina === "facturas" && ventana.paginaFacturas !== null
        onTriggered: ventana.paginaFacturas.enfocarBusqueda()
    }
    Action {
        id: accionMesAnterior
        text: qsTr("Mes anterior")
        shortcut: "Ctrl+Up"
        enabled: vm.pagina === "facturas" && vm.hayPerfiles
        onTriggered: vm.moverMes(-1)
    }
    Action {
        id: accionMesSiguiente
        text: qsTr("Mes siguiente")
        shortcut: "Ctrl+Down"
        enabled: vm.pagina === "facturas" && vm.hayPerfiles
        onTriggered: vm.moverMes(1)
    }
    Shortcut {
        sequence: "Ctrl+Shift+E"
        onActivated: panel.opened ? panel.close() : panel.open()
    }
    Shortcut {
        sequence: "Escape"
        enabled: vm.pagina !== "facturas" && !panel.opened && vm.pdf === null
        onActivated: vm.pagina === "diagnostico" ? vm.irA(vm.paginaPrevia) : vm.irA("facturas")
    }

    Component {
        id: barraMenuComponente
        MenuBar {
            Menu {
                title: qsTr("Facturas")
                MenuItem { action: accionMes }
                MenuItem { action: accionBuscar }
                MenuSeparator { }
                MenuItem { action: accionMesAnterior }
                MenuItem { action: accionMesSiguiente }
            }
            Menu {
                title: qsTr("Ir")
                MenuItem { action: accionFacturas }
                MenuItem { action: accionPerfiles }
                MenuItem { action: accionHistorial }
                MenuSeparator { }
                MenuItem { action: accionDiagnostico }
            }
            Menu {
                title: qsTr("Prototipo")
                MenuItem {
                    text: qsTr("Escenarios… (⌘⇧E)")
                    onTriggered: panel.open()
                }
            }
        }
    }
    Component.onCompleted: {
        if (Qt.platform.pluginName === "cocoa")
            ventana.menuBar = barraMenuComponente.createObject(ventana)
    }

    Loader {
        id: paginaActual
        anchors.fill: parent
        focus: true
        // Cambio diferido (como Main.qml de la app): no se destruye la pagina
        // dentro de su propio manejador ni a mitad de un escenario.
        property string mostrada: "facturas"
        sourceComponent: paginaActual.mostrada === "historial" ? historialComponente
                       : paginaActual.mostrada === "perfiles" ? perfilesComponente
                       : paginaActual.mostrada === "diagnostico" ? diagnosticoComponente
                       : facturasComponente
        function actualizar() { paginaActual.mostrada = vm.pagina }
    }
    Connections {
        target: vm
        function onPaginaChanged() { Qt.callLater(paginaActual.actualizar) }
    }

    Component {
        id: facturasComponente
        FacturasPage { vm: vm }
    }
    Component {
        id: historialComponente
        HistorialPage { vm: vm }
    }
    Component {
        id: perfilesComponente
        PerfilesPage { vm: vm }
    }
    Component {
        id: diagnosticoComponente
        DiagnosticoPage { vm: vm }
    }

    NotificacionSimulada {
        vm: vm
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.espacioM
        z: 10
    }

    PanelEscenarios {
        id: panel
        escenarios: ventana.escenarios
        ventana: ventana
        onEscenarioElegido: clave => ventana.aplicarEscenario(clave)
    }
}
