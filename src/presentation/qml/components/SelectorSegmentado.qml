pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

// Control segmentado (T013, ficha SelectorSegmentado): carril
// Theme.controlSecundario (radio 7, padding 2) con segmentos de alto 24 y
// radio 5. El activo lleva Theme.segmentoActivo y peso 600 (no solo color);
// el inactivo, peso 500 y hover con 50 % de segmentoActivo.
// - opciones: [{ clave, texto, icono?, contador? }]
// - valor: clave activa. Al elegir se actualiza y se emite activado(clave).
// - variante: "opciones" (rol RadioButton) o "pestanas" (rol PageTab).
// Teclado: Tab entra en el segmento activo; flechas izquierda/derecha cambian
// de segmento; Espacio/Enter activan el segmento con foco.
Rectangle {
    id: selector

    property var opciones: []
    property string valor: ""
    property string variante: "opciones"
    property string nombreAccesible: ""

    signal activado(string clave)

    implicitWidth: fila.implicitWidth + 4
    implicitHeight: 28
    radius: 7
    color: Theme.controlSecundario
    opacity: selector.enabled ? 1 : 0.5

    Accessible.role: selector.variante === "pestanas" ? Accessible.PageTabList : Accessible.Grouping
    Accessible.name: selector.nombreAccesible

    function elegir(indice) {
        if (indice < 0 || indice >= selector.opciones.length)
            return
        const clave = selector.opciones[indice].clave
        // Primero el foco: el segmento anterior deja de ser alcanzable con Tab
        // al cambiar `valor`, y no puede perder esa politica mientras tiene foco.
        const segmento = repetidor.itemAt(indice)
        let conFoco = false
        for (let i = 0; i < repetidor.count; ++i) {
            const s = repetidor.itemAt(i)
            if (s && s.activeFocus)
                conFoco = true
        }
        if (segmento && conFoco)
            segmento.forceActiveFocus(Qt.TabFocusReason)
        selector.valor = clave
        selector.activado(clave)
    }

    function indiceActivo() {
        for (let i = 0; i < selector.opciones.length; ++i) {
            if (selector.opciones[i].clave === selector.valor)
                return i
        }
        return -1
    }

    Row {
        id: fila
        x: 2
        y: 2
        spacing: 2

        Repeater {
            id: repetidor
            model: selector.opciones

            delegate: AbstractButton {
                id: segmento
                required property var modelData
                required property int index

                readonly property bool activo: segmento.modelData.clave === selector.valor
                readonly property string textoAccesible: segmento.modelData.contador !== undefined
                    ? segmento.modelData.texto + " (" + segmento.modelData.contador + ")" : segmento.modelData.texto

                objectName: "segmento_" + segmento.modelData.clave
                checkable: false
                focusPolicy: segmento.activo ? Qt.StrongFocus : Qt.ClickFocus
                height: 24
                leftPadding: 14
                rightPadding: 14
                implicitWidth: contenido.implicitWidth + leftPadding + rightPadding

                Accessible.role: selector.variante === "pestanas" ? Accessible.PageTab : Accessible.RadioButton
                Accessible.name: segmento.textoAccesible
                Accessible.checkable: true
                Accessible.checked: segmento.activo

                onClicked: selector.elegir(segmento.index)
                Keys.onLeftPressed: selector.elegir(Math.max(0, segmento.index - 1))
                Keys.onRightPressed: selector.elegir(Math.min(selector.opciones.length - 1, segmento.index + 1))
                Keys.onReturnPressed: selector.elegir(segmento.index)
                Keys.onEnterPressed: selector.elegir(segmento.index)

                contentItem: Item {
                    implicitWidth: contenido.implicitWidth
                    Row {
                        id: contenido
                        anchors.centerIn: parent
                        spacing: Theme.espacioXs
                        Icono {
                            visible: !!segmento.modelData.icono
                            nombre: segmento.modelData.icono ? segmento.modelData.icono : ""
                            color: Theme.texto
                            tamano: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: segmento.modelData.texto
                            color: Theme.texto
                            font.family: Theme.familia
                            font.pixelSize: Theme.cuerpo.size
                            font.weight: segmento.activo ? Font.DemiBold : Font.Medium
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.ignored: true
                        }
                        Text {
                            visible: segmento.modelData.contador !== undefined
                            text: segmento.modelData.contador !== undefined ? String(segmento.modelData.contador) : ""
                            color: Theme.textoSecundario
                            font.family: Theme.familia
                            font.pixelSize: Theme.etiqueta.size
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                            Accessible.ignored: true
                        }
                    }
                }
                background: Rectangle {
                    id: fondoSegmento
                    radius: 5
                    color: segmento.activo ? Theme.segmentoActivo
                           : segmento.hovered ? Qt.alpha(Theme.segmentoActivo, 0.5) : "transparent"
                    AnilloFoco {
                        radioBase: fondoSegmento.radius
                        visible: segmento.visualFocus
                    }
                }
            }
        }
    }
}
