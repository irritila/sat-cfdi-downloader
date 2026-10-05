pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "Etiquetas.js" as Etiquetas

// Lista de solicitudes con estados Cargando, Vacia, Error y ConDatos
// (SolicitudesListModel.estado). Teclado: foco inicial en la lista (o en "Nueva
// solicitud" si esta vacia); flechas para moverse; Enter/Return/Espacio abre
// el detalle por id; Tab alterna entre lista y boton.
Page {
    id: pagina
    objectName: "paginaSolicitudes"

    required property AppViewModel app
    required property SolicitudesListModel modelo

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Solicitudes de descarga masiva")

    // Foco inicial diferido: la pagina ya esta en la ventana.
    Component.onCompleted: Qt.callLater(pagina.enfocarInicial)

    function enfocarInicial() {
        if (lista.visible && lista.count > 0)
            lista.forceActiveFocus(Qt.TabFocusReason)
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

        BotonAccion {
            id: botonNueva
            objectName: "botonNuevaSolicitud"
            text: qsTr("Nueva solicitud")
            descripcion: qsTr("Abrir el formulario de nueva solicitud")
            highlighted: true
            KeyNavigation.tab: botonPerfiles
            onClicked: pagina.app.mostrarNueva()
        }
        BotonAccion {
            id: botonPerfiles
            objectName: "botonPerfilesSat"
            text: qsTr("Perfiles SAT")
            descripcion: qsTr("Administrar perfiles SAT y su e.firma")
            KeyNavigation.tab: lista.visible ? lista : (botonReintentar.visible ? botonReintentar : botonNueva)
            onClicked: pagina.app.mostrarPerfiles()
        }
    }

    // T009 D9: aviso cuando macOS no permite notificaciones (no afecta el flujo).
    footer: Label {
        objectName: "avisoNotificaciones"
        visible: pagina.app.notificacionesDeshabilitadas
        text: qsTr("Las notificaciones estan deshabilitadas. Activalas en Ajustes del Sistema para recibir avisos.")
        wrapMode: Text.WordWrap
        padding: 8
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    ListView {
        id: lista
        objectName: "listaSolicitudes"
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        spacing: 4
        visible: pagina.modelo.estado === SolicitudesListModel.ConDatos
        model: pagina.modelo
        currentIndex: count > 0 ? 0 : -1
        activeFocusOnTab: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        KeyNavigation.tab: botonNueva

        Accessible.role: Accessible.List
        Accessible.name: qsTr("Lista de solicitudes, %n elemento(s)", "", count)

        ScrollBar.vertical: ScrollBar { }

        Keys.onReturnPressed: pagina.abrir(pagina.modelo.idEn(lista.currentIndex))
        Keys.onEnterPressed: pagina.abrir(pagina.modelo.idEn(lista.currentIndex))
        Keys.onSpacePressed: pagina.abrir(pagina.modelo.idEn(lista.currentIndex))

        delegate: ItemDelegate {
            id: fila
            objectName: "filaSolicitud_" + index

            required property int index
            required property var model

            readonly property string solicitudId: model.id
            readonly property string textoEstado: Etiquetas.estadoResumen(model.estadoResumen)
            readonly property string textoTipo: Etiquetas.tipoDescarga(model.tipoDescarga)
            readonly property string textoPaquetes: qsTr("%n paquete(s)", "", model.totalPaquetes)

            width: ListView.view.width
            focusPolicy: Qt.NoFocus
            highlighted: ListView.isCurrentItem

            Accessible.role: Accessible.ListItem
            Accessible.name: qsTr("Solicitud %1, %2, del %3 al %4, estado %5, %6")
                .arg(model.perfilRfc).arg(textoTipo).arg(model.fechaInicial)
                .arg(model.fechaFinal).arg(textoEstado).arg(textoPaquetes)

            onClicked: {
                lista.currentIndex = index
                pagina.abrir(solicitudId)
            }

            contentItem: ColumnLayout {
                spacing: 4

                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true

                    Label {
                        text: fila.model.perfilRfc
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Accessible.ignored: true
                    }
                    EstadoBadge {
                        clave: fila.model.estadoResumen
                    }
                }
                Label {
                    text: qsTr("%1 · %2 a %3 · %4%5")
                        .arg(fila.textoTipo).arg(fila.model.fechaInicial).arg(fila.model.fechaFinal)
                        .arg(fila.textoPaquetes)
                        .arg(fila.model.rfcContraparte ? qsTr(" · Contraparte %1").arg(fila.model.rfcContraparte) : "")
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }
                Label {
                    text: qsTr("Creada %1").arg(Etiquetas.fechaHora(fila.model.creadaEn))
                    opacity: 0.75
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }
            }
        }
    }

    // Estado: vacia
    ColumnLayout {
        objectName: "estadoVacio"
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420)
        visible: pagina.modelo.estado === SolicitudesListModel.Vacia
        spacing: 12

        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("No hay solicitudes. Crea una nueva solicitud para comenzar.")

        Label {
            text: qsTr("No hay solicitudes")
            font.pixelSize: 18
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Accessible.ignored: true
        }
        Label {
            text: qsTr("Crea una nueva solicitud para comenzar.")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }

    // Estado: cargando
    ColumnLayout {
        objectName: "estadoCargando"
        anchors.centerIn: parent
        visible: pagina.modelo.estado === SolicitudesListModel.Cargando
        spacing: 8
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("Cargando solicitudes")

        BusyIndicator {
            running: parent.visible
            Layout.alignment: Qt.AlignHCenter
            Accessible.ignored: true
        }
        Label {
            text: qsTr("Cargando solicitudes...")
            Layout.alignment: Qt.AlignHCenter
            Accessible.ignored: true
        }
    }

    // Estado: error
    ColumnLayout {
        objectName: "estadoError"
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420)
        visible: pagina.modelo.estado === SolicitudesListModel.Error
        spacing: 12

        Label {
            objectName: "errorLista"
            text: pagina.modelo.errorMessage
            color: "#b00020"
            font.bold: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Accessible.role: Accessible.AlertMessage
            Accessible.name: qsTr("Error: %1").arg(text)
        }
        BotonAccion {
            id: botonReintentar
            objectName: "botonReintentarLista"
            text: qsTr("Reintentar")
            descripcion: qsTr("Volver a cargar la lista de solicitudes")
            Layout.alignment: Qt.AlignHCenter
            KeyNavigation.tab: botonNueva
            onClicked: pagina.modelo.refrescar()
        }
    }
}
