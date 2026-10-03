pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Ventana principal del shell. Los view models se inyectan desde C++
// (composition root o pruebas) con setInitialProperties; no hay context
// properties ni singletons. QML solo hace layout, navegacion y estado visual.
ApplicationWindow {
    id: ventana
    objectName: "ventanaPrincipal"

    required property AppViewModel appViewModel
    required property SolicitudesListModel solicitudesModel
    required property NuevaSolicitudViewModel nuevaSolicitudViewModel
    required property SolicitudDetailViewModel detalleViewModel

    width: 960
    height: 640
    minimumWidth: 560
    minimumHeight: 420
    visible: true
    title: qsTr("SAT CFDI Downloader")

    // La pagina se cambia de forma diferida (Qt.callLater) para no destruir la
    // pagina actual dentro de su propio manejador de clic o tecla.
    function componentePara(pagina) {
        switch (pagina) {
        case AppViewModel.Nueva:
            return nuevaComponente
        case AppViewModel.Detalle:
            return detalleComponente
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
}
