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
    required property NuevaSolicitudViewModel nuevaSolicitudViewModel
    required property SolicitudDetailViewModel detalleViewModel
    required property PerfilesSatViewModel perfilesViewModel
    required property EFirmaFormViewModel eFirmaViewModel

    width: 960
    height: 640
    minimumWidth: 560
    minimumHeight: 420
    visible: false
    title: qsTr("SAT CFDI Downloader")

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
        case AppViewModel.Nueva:
            return nuevaComponente
        case AppViewModel.Detalle:
            return detalleComponente
        case AppViewModel.Perfiles:
            return perfilesComponente
        default:
            return listaComponente
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
    readonly property NuevaSolicitudPage paginaNueva: paginaActual.item as NuevaSolicitudPage
    readonly property DetalleSolicitudPage paginaDetalle: paginaActual.item as DetalleSolicitudPage
    readonly property PerfilesSatPage paginaPerfiles: paginaActual.item as PerfilesSatPage

    readonly property bool dialogoAbierto: (ventana.paginaNueva !== null && ventana.paginaNueva.dialogoAbierto)
                                           || (ventana.paginaDetalle !== null && ventana.paginaDetalle.dialogoAbierto)
                                           || (ventana.paginaPerfiles !== null && ventana.paginaPerfiles.dialogoAbierto)
    // Campos de texto con foco: TextInput (incluye TextField) o TextEdit.
    readonly property TextInput entradaConFoco: ventana.activeFocusItem as TextInput
    readonly property TextEdit edicionConFoco: ventana.activeFocusItem as TextEdit
    readonly property bool textoConFoco: ventana.entradaConFoco !== null || ventana.edicionConFoco !== null
    readonly property bool campoEditableConFoco: (ventana.entradaConFoco !== null && !ventana.entradaConFoco.readOnly)
                                                 || (ventana.edicionConFoco !== null && !ventana.edicionConFoco.readOnly)
    readonly property bool atajosActivos: !ventana.dialogoAbierto && !ventana.campoEditableConFoco

    function regresarDePagina() {
        if (ventana.paginaNueva !== null)
            ventana.paginaNueva.regresar()
        else if (ventana.paginaDetalle !== null)
            ventana.paginaDetalle.regresar()
        else if (ventana.paginaPerfiles !== null)
            ventana.paginaPerfiles.regresar()
    }

    Action {
        id: accionNueva
        objectName: "atajoNuevaSolicitud"
        text: qsTr("Nueva solicitud")
        shortcut: "Ctrl+N"
        enabled: ventana.atajosActivos && ventana.paginaNueva === null && paginaActual.item !== null
        onTriggered: ventana.appViewModel.mostrarNueva()
    }
    Action {
        id: accionBuscar
        objectName: "atajoBuscar"
        text: qsTr("Buscar")
        shortcut: "Ctrl+F"
        enabled: ventana.atajosActivos && ventana.paginaLista !== null && ventana.paginaLista.puedeBuscar
        onTriggered: ventana.paginaLista.enfocarBusqueda()
    }
    Action {
        id: accionPrincipal
        objectName: "atajoAccionPrincipal"
        text: ventana.paginaDetalle !== null ? ventana.paginaDetalle.textoAccionPrincipal : qsTr("Verificar ahora")
        shortcut: "Ctrl+R"
        enabled: !ventana.dialogoAbierto && !ventana.textoConFoco && ventana.paginaDetalle !== null
                 && ventana.paginaDetalle.puedeAccionPrincipal
        onTriggered: ventana.paginaDetalle.accionPrincipal()
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
        text: qsTr("Solicitudes")
        shortcut: "Ctrl+1"
        enabled: ventana.atajosActivos && paginaActual.item !== null && ventana.paginaLista === null
        onTriggered: ventana.regresarDePagina()
    }

    // Menu de la app con los atajos: solo con el menu nativo de macOS (en
    // otras plataformas o en pruebas offscreen no ocupa espacio en la ventana).
    Component {
        id: barraMenuComponente
        MenuBar {
            Menu {
                title: qsTr("Solicitud")
                MenuItem { action: accionNueva }
                MenuItem { action: accionBuscar }
                MenuSeparator { }
                MenuItem { action: accionPrincipal }
                MenuItem { action: accionEliminar }
            }
            Menu {
                title: qsTr("Ir")
                // ⌘1 y ⌘2 (no ⌘[ ni ⌘,: en cocoa, ⌘, choca con el "Preferencias…"
                // oculto de Qt y ⌘[ cambia con el teclado latinoamericano).
                MenuItem { action: accionRegresar }
                MenuItem { action: accionPerfiles }
            }
        }
    }
    Component.onCompleted: {
        if (Qt.platform.pluginName === "cocoa")
            ventana.menuBar = barraMenuComponente.createObject(ventana)
    }

    Loader {
        id: paginaActual
        objectName: "paginaActual"
        anchors.fill: parent
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
        id: nuevaComponente
        NuevaSolicitudPage {
            app: ventana.appViewModel
            formulario: ventana.nuevaSolicitudViewModel
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
        id: perfilesComponente
        PerfilesSatPage {
            app: ventana.appViewModel
            perfiles: ventana.perfilesViewModel
            eFirma: ventana.eFirmaViewModel
        }
    }
}
