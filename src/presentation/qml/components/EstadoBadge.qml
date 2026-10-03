import QtQuick
import QtQuick.Controls

import "Etiquetas.js" as Etiquetas

// Insignia de estado: siempre muestra texto; el color solo lo refuerza.
Rectangle {
    id: badge

    // Clave estable del estado (p. ej. "EnvioFallido").
    property string clave: ""
    // Texto visible ya resuelto (por defecto, el de estadoResumen).
    property string texto: Etiquetas.estadoResumen(clave)
    // Prefijo para el nombre accesible, p. ej. "Estado SAT".
    property string contexto: qsTr("Estado")

    readonly property string tono: Etiquetas.tono(clave)

    implicitWidth: etiqueta.implicitWidth + 16
    implicitHeight: etiqueta.implicitHeight + 6
    radius: height / 2
    border.width: 1
    border.color: Qt.darker(color, 1.4)
    color: {
        switch (tono) {
        case "exito": return "#d7f0dc"
        case "error": return "#f8d7d7"
        case "advertencia": return "#fbecc8"
        case "progreso": return "#d8e6fa"
        default: return "#e6e6e6"
        }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: badge.contexto + ": " + badge.texto

    Label {
        id: etiqueta
        anchors.centerIn: parent
        width: Math.min(implicitWidth, badge.width - 16)
        elide: Text.ElideRight
        text: badge.texto
        color: "#1d1d1f"
        font.bold: true
        Accessible.ignored: true
    }
}
