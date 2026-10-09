import QtQuick
import QtQuick.Controls.Basic

// Campo de texto del diseno (T013, control de CampoFormulario): alto 28,
// fondo Theme.controlFondo, borde Theme.borde y radio de control. Foco: borde
// y anillo Theme.foco. `conError`: borde de 2 px Theme.error. `soloLectura`:
// fondo Theme.superficieSeccion, borde punteado e icono de candado (UX-32).
// Deshabilitado: 0.5.
TextField {
    id: campo

    property bool conError: false
    property bool soloLectura: false

    readOnly: campo.soloLectura
    implicitHeight: Theme.altoControl
    leftPadding: campo.soloLectura ? 30 : 8
    rightPadding: 8
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    font.family: Theme.familia
    font.pixelSize: Theme.cuerpo.size
    color: Theme.texto
    placeholderTextColor: Theme.textoSecundario
    selectionColor: Theme.seleccion
    selectedTextColor: Theme.texto
    opacity: campo.enabled ? 1 : 0.5

    background: Rectangle {
        id: fondo
        implicitWidth: 160
        implicitHeight: Theme.altoControl
        radius: Theme.radioControl
        color: campo.soloLectura ? Theme.superficieSeccion : Theme.controlFondo
        border.width: campo.soloLectura && !campo.conError ? 0 : campo.conError ? 2 : 1
        border.color: campo.conError ? Theme.error : campo.activeFocus ? Theme.foco : Theme.borde

        // Borde punteado del campo de solo lectura.
        Canvas {
            id: punteado
            anchors.fill: parent
            visible: campo.soloLectura && !campo.conError
            readonly property color colorBorde: Theme.borde
            onColorBordeChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onVisibleChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = punteado.colorBorde
                ctx.lineWidth = 1
                ctx.setLineDash([3, 2])
                const r = fondo.radius
                const w = punteado.width - 1
                const h = punteado.height - 1
                ctx.beginPath()
                ctx.moveTo(0.5 + r, 0.5)
                ctx.arcTo(0.5 + w, 0.5, 0.5 + w, 0.5 + h, r)
                ctx.arcTo(0.5 + w, 0.5 + h, 0.5, 0.5 + h, r)
                ctx.arcTo(0.5, 0.5 + h, 0.5, 0.5, r)
                ctx.arcTo(0.5, 0.5, 0.5 + w, 0.5, r)
                ctx.closePath()
                ctx.stroke()
            }
        }

        AnilloFoco {
            radioBase: fondo.radius
            visible: campo.activeFocus && !campo.soloLectura
        }
        Icono {
            visible: campo.soloLectura
            nombre: "lock"
            color: Theme.textoSecundario
            tamano: 14
            anchors.left: parent.left
            anchors.leftMargin: 9
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
