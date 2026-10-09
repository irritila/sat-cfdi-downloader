import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Region sin datos o con error de carga (T013, ficha EstadoVacio). Variantes:
// "vacio" (icono tray), "error" (exclamation-triangle) y "sinPerfiles"
// (person-card). Icono de 48 en Theme.textoSecundario, titulo 15/600 (Heading),
// descripcion de hasta 380 y un BotonAccion opcional (`textoAccion`,
// `accionPrimaria`). Con variante "error" el boton toma el foco al mostrarse.
ColumnLayout {
    id: vacio

    property string variante: "vacio"
    property string titulo: ""
    property string descripcion: ""
    property string textoAccion: ""
    property bool accionPrimaria: false
    // objectName del titulo y del boton (para pruebas, D10).
    property string objectNameTitulo: "tituloVacio"
    property string objectNameAccion: "accionVacio"
    readonly property alias botonAccion: accion

    signal accionSolicitada()

    spacing: Theme.espacioS

    // El error de carga se anuncia; el vacio es informativo.
    Accessible.role: vacio.variante === "error" ? Accessible.AlertMessage : Accessible.Grouping
    Accessible.name: (vacio.variante === "error" ? qsTr("Error: ") : "")
                     + [vacio.titulo, vacio.descripcion].filter(t => t.length > 0).join(". ")

    onVisibleChanged: {
        if (vacio.visible && vacio.variante === "error" && accion.visible)
            Qt.callLater(vacio.enfocarAccion)
    }

    function enfocarAccion() {
        accion.forceActiveFocus(Qt.OtherFocusReason)
    }

    Icono {
        nombre: vacio.variante === "error" ? "exclamation-triangle"
                : vacio.variante === "sinPerfiles" ? "person-card" : "tray"
        color: Theme.textoSecundario
        tamano: 48
        Layout.alignment: Qt.AlignHCenter
    }
    Label {
        objectName: vacio.objectNameTitulo
        text: vacio.titulo
        color: Theme.texto
        font.family: Theme.familia
        font.pixelSize: Theme.subtitulo.size
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Layout.maximumWidth: 380
        Layout.alignment: Qt.AlignHCenter
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }
    Label {
        objectName: "descripcionVacio"
        visible: text.length > 0
        text: vacio.descripcion
        color: Theme.textoSecundario
        font.family: Theme.familia
        font.pixelSize: Theme.cuerpo.size
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Layout.maximumWidth: 380
        Layout.alignment: Qt.AlignHCenter
        Accessible.ignored: true
    }
    BotonAccion {
        id: accion
        objectName: vacio.objectNameAccion
        visible: vacio.textoAccion.length > 0
        text: vacio.textoAccion
        variante: vacio.accionPrimaria ? "primario" : "secundario"
        Layout.topMargin: Theme.espacioM - Theme.espacioS
        Layout.alignment: Qt.AlignHCenter
        onClicked: vacio.accionSolicitada()
    }
}
