import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import "Etiquetas.js" as Etiquetas

// Linea del historial (T013, ficha EventoHistorial): fecha y hora (150) en
// Theme.textoSecundario, descripcion en cuerpo y origen con icono de 13
// (person para Usuario, gear para Automatico/Recuperacion). Separador inferior.
// - fechaHora: instante (QDateTime/Date), se formatea con FormatoFechas.
// - origen: clave estable del log ("usuario", "worker", "recuperacion").
Item {
    id: evento

    property var fechaHora: null
    property string descripcion: ""
    property string origen: ""

    readonly property string textoFecha: FormatoFechas.fechaHora(evento.fechaHora)
    readonly property string textoOrigen: Etiquetas.origenLog(evento.origen)
    // Resumen de una linea (fecha, descripcion y origen).
    readonly property string text: [evento.textoFecha, evento.descripcion,
                                    evento.textoOrigen.length > 0 ? qsTr("origen %1").arg(evento.textoOrigen) : ""]
                                   .filter(t => t.length > 0).join(" · ")

    Layout.fillWidth: true
    implicitWidth: 150 + textoDescripcion.implicitWidth + filaOrigen.implicitWidth + 2 * Theme.espacioL + 24
    implicitHeight: Math.max(textoDescripcion.implicitHeight, 18) + 16

    Accessible.role: Accessible.ListItem
    Accessible.name: [evento.textoFecha.replace(",", ""), evento.descripcion,
                      evento.textoOrigen.length > 0 ? qsTr("origen %1").arg(evento.textoOrigen) : ""]
                     .filter(t => t.length > 0).join(", ")

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.espacioL
        anchors.rightMargin: Theme.espacioL
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: Theme.espacioM

        Label {
            objectName: "fechaEvento"
            text: evento.textoFecha
            color: Theme.textoSecundario
            font.family: Theme.familia
            font.pixelSize: Theme.cuerpo.size
            Layout.preferredWidth: 150 - Theme.espacioM
            Layout.alignment: Qt.AlignTop
            Accessible.ignored: true
        }
        Label {
            id: textoDescripcion
            objectName: "descripcionEvento"
            text: evento.descripcion
            color: Theme.texto
            font.family: Theme.familia
            font.pixelSize: Theme.cuerpo.size
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Accessible.ignored: true
        }
        RowLayout {
            id: filaOrigen
            visible: evento.textoOrigen.length > 0
            spacing: Theme.espacioXs
            Layout.alignment: Qt.AlignTop
            Icono {
                nombre: evento.origen === "usuario" ? "person" : "gear"
                color: Theme.textoSecundario
                tamano: 13
            }
            Label {
                objectName: "origenEvento"
                text: evento.textoOrigen
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                Accessible.ignored: true
            }
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
