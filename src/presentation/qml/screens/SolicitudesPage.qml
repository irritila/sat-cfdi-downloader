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
// Teclado: orden de foco por las acciones del encabezado y la lista; foco
// inicial en la lista (primera fila); flechas para moverse; Enter/Return/
// Espacio abre el detalle por id.
Page {
    id: pagina
    objectName: "paginaSolicitudes"

    required property AppViewModel app
    required property SolicitudesListModel modelo

    readonly property bool anchoAmplio: pagina.width >= 960

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Solicitudes de descarga masiva")

    background: Rectangle {
        color: Theme.superficie
    }

    // Foco inicial diferido: la pagina ya esta en la ventana.
    Component.onCompleted: Qt.callLater(pagina.enfocarInicial)

    function enfocarInicial() {
        if (lista.visible && lista.count > 0)
            lista.forceActiveFocus(Qt.TabFocusReason)
        else if (error.visible)
            error.botonAccion.forceActiveFocus(Qt.TabFocusReason)
        else
            botonNueva.forceActiveFocus(Qt.TabFocusReason)
    }

    // Si la primera carga termina despues del foco inicial, el foco pasa a la
    // lista (solo desde el estado Cargando, para no mover el foco del usuario).
    property int estadoAnterior: pagina.modelo.estado
    Connections {
        target: pagina.modelo
        function onEstadoChanged() {
            if (pagina.estadoAnterior === SolicitudesListModel.Cargando
                    && pagina.modelo.estado === SolicitudesListModel.ConDatos
                    && botonNueva.activeFocus)
                lista.forceActiveFocus(Qt.OtherFocusReason)
            pagina.estadoAnterior = pagina.modelo.estado
        }
    }

    function abrir(id) {
        if (id)
            pagina.app.abrirDetalle(id)
    }

    header: EncabezadoPagina {
        titulo: qsTr("Solicitudes")
        contador: pagina.modelo.estado === SolicitudesListModel.ConDatos ? String(pagina.modelo.count) : ""

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
            onClicked: pagina.app.mostrarPerfiles()
        }
        BotonAccion {
            id: botonNueva
            objectName: "botonNuevaSolicitud"
            variante: "primario"
            icono: "plus"
            text: qsTr("Nueva solicitud")
            descripcion: qsTr("Abrir el formulario de nueva solicitud")
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

        // ---- Tabla ----
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: lista
                objectName: "listaSolicitudes"
                anchors.fill: parent
                clip: true
                visible: pagina.modelo.estado === SolicitudesListModel.ConDatos
                model: pagina.modelo
                currentIndex: count > 0 ? 0 : -1
                activeFocusOnTab: true
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                headerPositioning: ListView.OverlayHeader

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Lista de solicitudes, %n elemento(s)", "", count)

                ScrollBar.vertical: ScrollBar { }

                Keys.onReturnPressed: pagina.abrir(pagina.modelo.idEn(lista.currentIndex))
                Keys.onEnterPressed: pagina.abrir(pagina.modelo.idEn(lista.currentIndex))
                Keys.onSpacePressed: pagina.abrir(pagina.modelo.idEn(lista.currentIndex))

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
                visible: pagina.modelo.estado === SolicitudesListModel.Vacia
                variante: "vacio"
                titulo: qsTr("No hay solicitudes")
                descripcion: qsTr("Crea una solicitud para descargar del SAT los CFDI emitidos o recibidos de un contribuyente.")
                textoAccion: qsTr("Nueva solicitud")
                accionPrimaria: true
                objectNameAccion: "botonNuevaSolicitudVacia"
                onAccionSolicitada: pagina.app.mostrarNueva()
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
