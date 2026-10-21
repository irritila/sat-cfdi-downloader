pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

// Ventana principal del shell. Los view models se inyectan desde C++
// (composition root o pruebas) con setInitialProperties; no hay context
// properties ni singletons. QML solo hace layout, navegacion y estado visual.
//
// La ventana arranca oculta (T004 DA2): AppLifecycleController decide si se
// muestra (arranque manual) o no (Login Item), sin destello. QML no llama APIs
// del sistema operativo.
ApplicationWindow {
    id: ventana
    objectName: "ventanaPrincipal"

    required property AppViewModel appViewModel
    required property SolicitudesListModel solicitudesModel
    required property SolicitudDetailViewModel detalleViewModel
    required property PerfilesSatViewModel perfilesViewModel
    required property EFirmaFormViewModel eFirmaViewModel
    required property ExploradorCfdiViewModel exploradorViewModel
    // T023.4: pantalla principal Facturas (por perfil y mes).
    required property FacturasViewModel facturasViewModel
    // T023.5: historial de descargas del perfil (⌘3).
    required property HistorialViewModel historialViewModel
    // T019 D8: aviso de la carpeta de paquetes (inactivo si no hay aviso).
    required property AvisoRaizPaquetesViewModel avisoRaizPaquetesViewModel

    width: 960
    height: 640
    minimumWidth: 560
    minimumHeight: 420
    visible: false
    title: qsTr("Archivo Fiscal")

    // T013: fondo y paleta de los controles Basic desde Theme (claro/oscuro);
    // los componentes propios usan Theme directamente.
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

    // La pagina se cambia de forma diferida (Qt.callLater) para no destruir la
    // pagina actual dentro de su propio manejador de clic o tecla.
    function componentePara(pagina) {
        switch (pagina) {
        case AppViewModel.Historial:
            return historialComponente
        case AppViewModel.Detalle:
            return detalleComponente
        case AppViewModel.Perfiles:
            return perfilesComponente
        case AppViewModel.Facturas:
            return facturasComponente
        case AppViewModel.FacturasSolicitud:
            return facturasSolicitudComponente
        default:
            return listaComponente // Diagnostico: lista tecnica de descargas
        }
    }

    function actualizarPagina() {
        const componente = componentePara(ventana.appViewModel.pagina)
        if (paginaActual.sourceComponent !== componente)
            paginaActual.sourceComponent = componente
    }

    Connections {
        target: ventana.appViewModel
        function onPaginaChanged() { Qt.callLater(ventana.actualizarPagina) }
    }

    // ---- T014.1 D5: atajos de teclado ----
    // Cada atajo es una Action: el menu de la app (solo cocoa) muestra las
    // mismas acciones, sin registrar el atajo dos veces. Reglas:
    // - con un dialogo abierto no actua ninguno (Escape lo atiende el dialogo);
    // - con un campo editable enfocado no actua ninguno (Escape sigue siendo
    //   de la pagina o del campo);
    // - ⌘R tampoco actua con foco en texto de solo lectura;
    // - ⌘⌫ solo abre la confirmacion de eliminar (nunca elimina).
    readonly property SolicitudesPage paginaLista: paginaActual.item as SolicitudesPage
    readonly property HistorialPage paginaHistorial: paginaActual.item as HistorialPage
    readonly property DetalleSolicitudPage paginaDetalle: paginaActual.item as DetalleSolicitudPage
    readonly property PerfilesSatPage paginaPerfiles: paginaActual.item as PerfilesSatPage
    readonly property ExploradorCfdiPage paginaExplorador: paginaActual.item as ExploradorCfdiPage
    readonly property FacturasPage paginaFacturas: paginaActual.item as FacturasPage

    readonly property bool dialogoAbierto: (ventana.paginaHistorial !== null && ventana.paginaHistorial.dialogoAbierto)
                                           || (ventana.paginaDetalle !== null && ventana.paginaDetalle.dialogoAbierto)
                                           || (ventana.paginaPerfiles !== null && ventana.paginaPerfiles.dialogoAbierto)
                                           || (ventana.paginaExplorador !== null && ventana.paginaExplorador.dialogoAbierto)
                                           || (ventana.paginaFacturas !== null && ventana.paginaFacturas.dialogoAbierto)
    // Campos de texto con foco: TextInput (incluye TextField) o TextEdit.
    readonly property TextInput entradaConFoco: ventana.activeFocusItem as TextInput
    readonly property TextEdit edicionConFoco: ventana.activeFocusItem as TextEdit
    readonly property bool textoConFoco: ventana.entradaConFoco !== null || ventana.edicionConFoco !== null
    readonly property bool campoEditableConFoco: (ventana.entradaConFoco !== null && !ventana.entradaConFoco.readOnly)
                                                 || (ventana.edicionConFoco !== null && !ventana.edicionConFoco.readOnly)
    readonly property bool atajosActivos: !ventana.dialogoAbierto && !ventana.campoEditableConFoco

    // T023 D10: vista tecnica (Diagnostico, su detalle y el explorador por
    // solicitud). T023.5: ninguna ruta lleva a NuevaSolicitudPage (sin ⌘N).
    readonly property bool enDiagnostico: ventana.paginaLista !== null || ventana.paginaDetalle !== null
                                          || ventana.paginaExplorador !== null

    Action {
        id: accionBuscar
        objectName: "atajoBuscar"
        text: qsTr("Buscar")
        shortcut: "Ctrl+F"
        enabled: ventana.atajosActivos
                 && ((ventana.paginaLista !== null && ventana.paginaLista.puedeBuscar)
                     || (ventana.paginaExplorador !== null && ventana.paginaExplorador.puedeBuscar)
                     || (ventana.paginaFacturas !== null && ventana.paginaFacturas.puedeBuscar))
        onTriggered: {
            if (ventana.paginaFacturas !== null)
                ventana.paginaFacturas.enfocarBusqueda()
            else if (ventana.paginaExplorador !== null)
                ventana.paginaExplorador.enfocarBusqueda()
            else
                ventana.paginaLista.enfocarBusqueda()
        }
    }
    Action {
        id: accionPrincipal
        objectName: "atajoAccionPrincipal"
        // T023.4: en Facturas, la accion de la franja; en el detalle, la suya.
        text: ventana.paginaFacturas !== null
              ? (ventana.paginaFacturas.franja.textoAccion.length > 0 ? ventana.paginaFacturas.franja.textoAccion
                                                                      : qsTr("Acción del mes"))
              : ventana.paginaDetalle !== null ? ventana.paginaDetalle.textoAccionPrincipal : qsTr("Acción principal")
        shortcut: "Ctrl+R"
        enabled: !ventana.dialogoAbierto && !ventana.textoConFoco
                 && ((ventana.paginaFacturas !== null && ventana.paginaFacturas.puedeAccion)
                     || (ventana.paginaDetalle !== null && ventana.paginaDetalle.puedeAccionPrincipal))
        onTriggered: {
            if (ventana.paginaFacturas !== null)
                ventana.paginaFacturas.ejecutarAccion()
            else
                ventana.paginaDetalle.accionPrincipal()
        }
    }
    // T023.4: ⌘↑/⌘↓ mes anterior/siguiente en Facturas.
    Action {
        id: accionMesAnterior
        objectName: "atajoMesAnterior"
        text: qsTr("Mes anterior")
        shortcut: "Ctrl+Up"
        enabled: ventana.atajosActivos && ventana.paginaFacturas !== null && ventana.paginaFacturas.puedeMoverMes
                 && ventana.facturasViewModel.puedeMesAnterior
        onTriggered: ventana.paginaFacturas.moverMes(-1)
    }
    Action {
        id: accionMesSiguiente
        objectName: "atajoMesSiguiente"
        text: qsTr("Mes siguiente")
        shortcut: "Ctrl+Down"
        enabled: ventana.atajosActivos && ventana.paginaFacturas !== null && ventana.paginaFacturas.puedeMoverMes
                 && ventana.facturasViewModel.puedeMesSiguiente
        onTriggered: ventana.paginaFacturas.moverMes(1)
    }
    Action {
        id: accionEliminar
        objectName: "atajoEliminar"
        text: qsTr("Eliminar…")
        shortcut: "Ctrl+Backspace"
        enabled: ventana.atajosActivos && ventana.paginaDetalle !== null && ventana.paginaDetalle.puedePedirEliminar
        onTriggered: ventana.paginaDetalle.pedirEliminar()
    }
    Action {
        id: accionPerfiles
        objectName: "atajoPerfiles"
        text: qsTr("Perfiles SAT")
        shortcut: "Ctrl+2"
        enabled: ventana.atajosActivos && ventana.paginaPerfiles === null && paginaActual.item !== null
        onTriggered: ventana.appViewModel.mostrarPerfiles()
    }
    Action {
        id: accionRegresar
        objectName: "atajoRegresar"
        // T023.4: ⌘1 lleva a Facturas (pantalla principal) desde cualquier pagina.
        text: qsTr("Facturas")
        shortcut: "Ctrl+1"
        enabled: ventana.atajosActivos && paginaActual.item !== null && ventana.paginaFacturas === null
        onTriggered: ventana.appViewModel.mostrarFacturas()
    }
    // T023.5: ⌘3 Historial de descargas del perfil.
    Action {
        id: accionHistorial
        objectName: "atajoHistorial"
        text: qsTr("Historial")
        shortcut: "Ctrl+3"
        enabled: ventana.atajosActivos && paginaActual.item !== null && ventana.paginaHistorial === null
        onTriggered: ventana.appViewModel.mostrarHistorial()
    }
    // "Ir > Diagnostico": lista tecnica de descargas; "Atras" vuelve al origen.
    Action {
        id: accionDiagnostico
        objectName: "accionDiagnostico"
        text: qsTr("Diagnóstico")
        enabled: ventana.atajosActivos && paginaActual.item !== null && ventana.paginaLista === null
        onTriggered: ventana.appViewModel.mostrarDiagnostico()
    }

    // Menu de la app con los atajos: solo con el menu nativo de macOS (en
    // otras plataformas o en pruebas offscreen no ocupa espacio en la ventana).
    Component {
        id: barraMenuComponente
        MenuBar {
            Menu {
                title: qsTr("Facturas")
                MenuItem { action: accionBuscar }
                MenuItem { action: accionPrincipal }
                MenuSeparator { }
                MenuItem { action: accionMesAnterior }
                MenuItem { action: accionMesSiguiente }
            }
            Menu {
                title: qsTr("Ir")
                // ⌘1 y ⌘2 (no ⌘[ ni ⌘,: en cocoa, ⌘, choca con el "Preferencias…"
                // oculto de Qt y ⌘[ cambia con el teclado latinoamericano).
                MenuItem { action: accionRegresar }
                MenuItem { action: accionPerfiles }
                MenuItem { action: accionHistorial }
                MenuSeparator { }
                MenuItem { action: accionDiagnostico }
                // "Eliminar…" solo en el detalle de diagnostico.
                MenuItem { action: accionEliminar; visible: ventana.paginaDetalle !== null }
            }
        }
    }
    Component.onCompleted: {
        if (Qt.platform.pluginName === "cocoa")
            ventana.menuBar = barraMenuComponente.createObject(ventana)
    }

    // T019 D8: arriba de todas las paginas mientras haya aviso; no toma foco
    // ni cambia el orden de tabulacion. No muestra la ventana por su cuenta.
    FranjaAvisoRaizPaquetes {
        id: franjaAvisoRaiz
        aviso: ventana.avisoRaizPaquetesViewModel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: implicitHeight
    }

    Loader {
        id: paginaActual
        objectName: "paginaActual"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: franjaAvisoRaiz.bottom
        anchors.bottom: parent.bottom
        focus: true
        Component.onCompleted: ventana.actualizarPagina()
    }

    Component {
        id: listaComponente
        SolicitudesPage {
            app: ventana.appViewModel
            modelo: ventana.solicitudesModel
        }
    }
    Component {
        id: historialComponente
        HistorialPage {
            app: ventana.appViewModel
            historial: ventana.historialViewModel
        }
    }
    Component {
        id: detalleComponente
        DetalleSolicitudPage {
            app: ventana.appViewModel
            detalle: ventana.detalleViewModel
        }
    }
    Component {
        id: facturasComponente
        FacturasPage {
            app: ventana.appViewModel
            facturas: ventana.facturasViewModel
        }
    }
    Component {
        id: facturasSolicitudComponente
        ExploradorCfdiPage {
            app: ventana.appViewModel
            explorador: ventana.exploradorViewModel
            detalle: ventana.detalleViewModel
        }
    }
    Component {
        id: perfilesComponente
        PerfilesSatPage {
            app: ventana.appViewModel
            perfiles: ventana.perfilesViewModel
            eFirma: ventana.eFirmaViewModel
        }
    }
}
