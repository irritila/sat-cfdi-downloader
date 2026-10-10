import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import "Etiquetas.js" as Etiquetas

// Paquete del SAT en la pestana Paquetes (T013, ficha FilaPaquete): columna de
// 150 con el EstadoBadge, bloque central con el nombre del paquete (mono,
// seleccionable), metadatos en leyenda y un mensaje opcional con icono de 14 en
// el tono `tonoMensaje` ("exito", "advertencia", "error" o "neutro"). En
// Descargando se muestra una barra indeterminada en lugar del mensaje. A la
// derecha, "Mostrar en Finder" (BotonAccion sm) solo en Descargado; se habilita
// con `finderHabilitado`.
// T014.2 D1: "Reintentar" (BotonAccion sm) solo con `puedeReintentar` (el view
// model lo niega con 5008); con `reintentoPendiente` (monitoreo en pausa, D2)
// se muestra "Reintento pendiente" en lugar del boton.
Item {
    id: fila

    property string idPaquete: ""
    property string estado: ""
    property string metadatos: ""
    property string mensaje: ""
    property string tonoMensaje: "neutro"
    property bool finderHabilitado: false
    property string objectNameFinder: ""
    property bool puedeReintentar: false
    property bool reintentoPendiente: false
    property string objectNameReintentar: "botonReintentarPaquete"
    property string objectNamePendiente: "reintentoPendientePaquete"
    // objectName del texto del mensaje (para pruebas, D10).
    property string objectNameMensaje: "mensajePaquete"
    // T014.1 D6: objectName del boton "Copiar" del nombre del paquete.
    property string objectNameCopiar: "botonCopiarPaquete"
    readonly property alias botonCopiar: copiar
    readonly property alias botonFinder: finder
    readonly property alias botonReintentar: reintentar

    signal mostrarEnFinder()
    signal reintentarSolicitado()

    readonly property string textoEstado: Etiquetas.estadoDescarga(fila.estado)
    readonly property string sufijo: {
        const i = fila.idPaquete.lastIndexOf("_")
        return i >= 0 ? fila.idPaquete.substring(i) : fila.idPaquete
    }
    readonly property color colorMensaje: {
        switch (fila.tonoMensaje) {
        case "exito": return Theme.exito
        case "advertencia": return Theme.advertencia
        case "error": return Theme.error
        default: return Theme.textoSecundario
        }
    }

    Layout.fillWidth: true
    implicitWidth: 150 + centro.implicitWidth + Math.max(finder.implicitWidth, reintentar.implicitWidth)
                   + 2 * Theme.espacioL + 2 * Theme.espacioM
    implicitHeight: Math.max(centro.implicitHeight, Theme.altoBadge) + 24

    Accessible.role: Accessible.ListItem
    Accessible.name: [qsTr("Paquete %1").arg(fila.sufijo), fila.textoEstado, fila.mensaje,
                      fila.reintentoPendiente ? qsTr("Reintento pendiente") : ""]
                     .filter(t => t.length > 0).join(", ")

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.espacioL
        anchors.rightMargin: Theme.espacioL
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        spacing: Theme.espacioM

        Item {
            Layout.preferredWidth: 150 - Theme.espacioM
            Layout.preferredHeight: badge.implicitHeight
            Layout.alignment: Qt.AlignTop
            EstadoBadge {
                id: badge
                objectName: "badgePaquete"
                clave: fila.estado
                texto: fila.textoEstado
                contexto: qsTr("Estado de descarga")
            }
        }
        ColumnLayout {
            id: centro
            spacing: 2
            Layout.fillWidth: true

            RowLayout {
                spacing: Theme.espacioXs
                Layout.fillWidth: true
                TextEdit {
                    objectName: "nombrePaquete"
                    text: fila.idPaquete
                    readOnly: true
                    selectByMouse: true
                    activeFocusOnTab: false
                    color: Theme.texto
                    selectionColor: Theme.seleccion
                    selectedTextColor: Theme.texto
                    font.family: Theme.familiaMono
                    font.pixelSize: Theme.mono.size
                    font.weight: Font.Medium
                    wrapMode: TextEdit.WrapAnywhere
                    Layout.fillWidth: true
                    Layout.maximumWidth: implicitWidth
                    Accessible.ignored: true
                }
                BotonCopiar {
                    id: copiar
                    objectName: fila.objectNameCopiar
                    valor: fila.idPaquete
                    descripcionValor: qsTr("Nombre del paquete")
                }
                Item { Layout.fillWidth: true }
            }
            Label {
                objectName: "metadatosPaquete"
                visible: text.length > 0
                text: fila.metadatos
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Accessible.ignored: true
            }
            ProgressBar {
                objectName: "progresoPaquete"
                visible: fila.estado === "Descargando"
                indeterminate: true
                Layout.fillWidth: true
                Layout.maximumWidth: 240
                Accessible.name: qsTr("Descargando")
            }
            // T014.2 D2: reintento pedido con el monitoreo en pausa.
            RowLayout {
                objectName: fila.objectNamePendiente
                visible: fila.reintentoPendiente
                spacing: Theme.espacioXs
                Layout.fillWidth: true
                Icono {
                    nombre: "clock"
                    color: Theme.textoSecundario
                    tamano: 14
                    Layout.alignment: Qt.AlignTop
                }
                Label {
                    text: qsTr("Reintento pendiente: se descargará al reanudar el monitoreo.")
                    color: Theme.textoSecundario
                    font.family: Theme.familia
                    font.pixelSize: Theme.leyenda.size
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }
            }
            RowLayout {
                visible: fila.estado !== "Descargando" && fila.mensaje.length > 0
                spacing: Theme.espacioXs
                Layout.fillWidth: true
                Icono {
                    nombre: fila.tonoMensaje === "exito" ? "check-circle"
                            : fila.tonoMensaje === "error" ? "exclamation-octagon"
                            : fila.tonoMensaje === "advertencia" ? "exclamation-triangle" : "clock"
                    color: fila.colorMensaje
                    tamano: 14
                    Layout.alignment: Qt.AlignTop
                }
                Label {
                    objectName: fila.objectNameMensaje
                    text: fila.mensaje
                    Accessible.name: fila.mensaje
                    color: fila.colorMensaje
                    font.family: Theme.familia
                    font.pixelSize: Theme.leyenda.size
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
        BotonAccion {
            id: finder
            objectName: fila.objectNameFinder
            visible: fila.estado === "Descargado"
            enabled: fila.finderHabilitado
            compacto: true
            icono: "folder"
            text: qsTr("Mostrar en Finder")
            descripcion: qsTr("Mostrar el archivo del paquete %1 en Finder").arg(fila.idPaquete)
            Layout.alignment: Qt.AlignTop
            onClicked: fila.mostrarEnFinder()
        }
        BotonAccion {
            id: reintentar
            objectName: fila.objectNameReintentar
            visible: fila.puedeReintentar && !fila.reintentoPendiente
            compacto: true
            icono: "arrow-down-circle"
            text: qsTr("Reintentar")
            descripcion: qsTr("Descargar de nuevo solo este paquete; si el monitoreo está pausado, queda pendiente")
            Accessible.name: qsTr("Reintentar paquete %1").arg(fila.idPaquete)
            Layout.alignment: Qt.AlignTop
            onClicked: fila.reintentarSolicitado()
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
