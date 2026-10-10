import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Par etiqueta-valor de solo lectura (T013, ficha CampoDetalle): rejilla de
// 200 + resto, etiqueta en Theme.textoSecundario, valor en Theme.texto (o
// tipografia mono con `mono`, para identificadores). Linea superior
// Theme.separador salvo con `primero`. Valor vacio: "—" atenuado. El valor es
// seleccionable para copiarlo; en `mono` se alcanza con Tab.
Item {
    id: campo

    property string etiqueta: ""
    property string valor: ""
    property bool mono: false
    property bool primero: false
    // T014.1 D6: boton "Copiar" junto al valor (con `descripcionCopia` para
    // su nombre accesible, p. ej. "Id de solicitud SAT").
    property bool copiable: false
    property string descripcionCopia: campo.etiqueta
    readonly property alias botonCopiar: copiar

    readonly property bool vacio: campo.valor.length === 0

    Layout.fillWidth: true
    implicitWidth: 200 + valorTexto.implicitWidth + 2 * Theme.espacioL
    implicitHeight: Math.max(etiquetaTexto.implicitHeight, valorTexto.implicitHeight) + 14

    Accessible.role: Accessible.StaticText
    Accessible.name: campo.etiqueta + ": " + (campo.vacio ? "—" : campo.valor)

    Rectangle {
        visible: !campo.primero
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.separador
    }

    Label {
        id: etiquetaTexto
        x: Theme.espacioL
        y: 7
        width: 200 - Theme.espacioL
        text: campo.etiqueta
        color: Theme.textoSecundario
        font.family: Theme.familia
        font.pixelSize: Theme.cuerpo.size
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }
    TextEdit {
        id: valorTexto
        x: 200
        y: 7
        width: campo.width - 200 - Theme.espacioL - (copiar.visible ? copiar.width + Theme.espacioS : 0)
        text: campo.vacio ? "—" : campo.valor
        readOnly: true
        selectByMouse: true
        activeFocusOnTab: campo.mono && !campo.vacio
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.WrapAnywhere
        color: campo.vacio ? Theme.textoSecundario : Theme.texto
        selectionColor: Theme.seleccion
        selectedTextColor: Theme.texto
        font.family: campo.mono ? Theme.familiaMono : Theme.familia
        font.pixelSize: campo.mono ? Theme.mono.size : Theme.cuerpo.size
        Accessible.ignored: true
    }
    BotonCopiar {
        id: copiar
        objectName: "botonCopiar"
        visible: campo.copiable && !campo.vacio
        valor: campo.valor
        descripcionValor: campo.descripcionCopia
        anchors.right: parent.right
        anchors.rightMargin: Theme.espacioL
        y: 4
    }
}
