import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Par etiqueta/valor de solo lectura. El valor hace wrap para no cortarse al
// redimensionar.
RowLayout {
    id: campo

    property string etiqueta: ""
    property string valor: ""

    spacing: 12
    Layout.fillWidth: true

    Accessible.role: Accessible.StaticText
    Accessible.name: campo.etiqueta + ": " + campo.valor

    Label {
        text: campo.etiqueta
        opacity: 0.75
        Layout.preferredWidth: 160
        Layout.alignment: Qt.AlignTop
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }
    Label {
        text: campo.valor.length > 0 ? campo.valor : "-"
        Layout.fillWidth: true
        wrapMode: Text.WrapAnywhere
        textFormat: Text.PlainText
        Accessible.ignored: true
    }
}
