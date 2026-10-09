import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Mensaje persistente dentro de una pagina o dialogo (T013, ficha
// AvisoEnLinea). `variante`: "progreso", "exito", "advertencia", "error" o
// "neutro" (colores tono-*). Titulo en cuerpo 600 y descripcion en cuerpo, en
// el texto del tono, con icono de 18. `textoAccion` agrega un BotonAccion sm.
// No se puede cerrar: desaparece cuando su causa se resuelve. Con `anunciar`
// (por omision) su rol es AlertMessage.
Rectangle {
    id: aviso

    property string variante: "neutro"
    property string titulo: ""
    property string descripcion: ""
    property string textoAccion: ""
    property bool anunciar: true
    // Icono propio (p. ej. "bell-slash"); vacio = el de la variante.
    property string icono: ""
    // objectName del boton (para pruebas, D10).
    property string objectNameAccion: "accionAviso"
    readonly property alias botonAccion: accion

    signal accionSolicitada()

    readonly property var colores: Theme.tono(aviso.variante)
    readonly property string iconoVariante: {
        switch (aviso.variante) {
        case "exito": return "check-circle"
        case "advertencia": return "exclamation-triangle"
        case "error": return "exclamation-octagon"
        case "progreso": return "clock"
        default: return "question-circle"
        }
    }

    implicitWidth: fila.implicitWidth + 24
    implicitHeight: fila.implicitHeight + 20
    radius: 8
    color: aviso.colores.fondo
    border.width: 1
    border.color: aviso.colores.borde

    Accessible.role: aviso.anunciar ? Accessible.AlertMessage : Accessible.StaticText
    Accessible.name: [aviso.titulo, aviso.descripcion].filter(t => t.length > 0).join(". ")

    RowLayout {
        id: fila
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 10

        Icono {
            nombre: aviso.icono.length > 0 ? aviso.icono : aviso.iconoVariante
            color: aviso.colores.texto
            tamano: 18
            Layout.alignment: Qt.AlignTop
        }
        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Label {
                objectName: "tituloAviso"
                visible: text.length > 0
                text: aviso.titulo
                color: aviso.colores.texto
                font.family: Theme.familia
                font.pixelSize: Theme.cuerpoFuerte.size
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Accessible.ignored: true
            }
            Label {
                objectName: "descripcionAviso"
                visible: text.length > 0
                text: aviso.descripcion
                color: aviso.colores.texto
                font.family: Theme.familia
                font.pixelSize: Theme.cuerpo.size
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Accessible.ignored: true
            }
        }
        BotonAccion {
            id: accion
            objectName: aviso.objectNameAccion
            visible: aviso.textoAccion.length > 0
            text: aviso.textoAccion
            compacto: true
            Layout.alignment: Qt.AlignVCenter
            onClicked: aviso.accionSolicitada()
        }
    }
}
