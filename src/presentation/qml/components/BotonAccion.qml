import QtQuick
import QtQuick.Controls.Basic

// Boton estandar (T013, ficha BotonAccion). Variantes en `variante`:
// - "primario": relleno Theme.acento (uno por vista). `highlighted: true`
//   equivale a primario (compatibilidad con las pantallas).
// - "secundario" (por omision): relleno Theme.controlSecundario.
// - "destructivo": relleno controlSecundario con texto e icono Theme.error.
// - "navegacion" / "plano": sin relleno, texto Theme.acentoTexto
//   (navegacion lleva chevron-left si no se indica otro icono).
// - "icono": cuadrado de 28 sin texto; `nombreAccesible` es obligatorio.
// `compacto` = tamano sm (alto 24, texto 12). `icono` = nombre del SVG (16 pt).
// `cargando` sustituye el icono por un BusyIndicator; el llamador deshabilita.
// Estados: hover (4 % mas oscuro en claro, 15 % mas claro en oscuro),
// presionado (acentoPresionado o 10 % mas oscuro), foco (anillo 2/2) y
// deshabilitado (opacidad 0.45). Espacio, Enter y Return lo activan.
Button {
    id: boton

    property string descripcion: ""
    property string variante: boton.highlighted ? "primario" : "secundario"
    property string icono: boton.variante === "navegacion" ? "chevron-left" : ""
    property bool compacto: false
    property bool cargando: false
    property string nombreAccesible: ""
    // T014.1 D5: atajo de teclado visible ("⌘N"); se muestra en el ToolTip.
    property string atajo: ""
    readonly property string textoAyuda: (boton.esIcono ? boton.nombreAccesible : boton.text)
                                         + (boton.atajo.length > 0 ? " (" + boton.atajo + ")" : "")

    readonly property bool esPrimario: boton.variante === "primario"
    readonly property bool esIcono: boton.variante === "icono"
    readonly property bool sinRelleno: boton.variante === "navegacion" || boton.variante === "plano"
    readonly property color colorTexto: boton.esPrimario ? Theme.textoSobreAcento
                                        : boton.variante === "destructivo" ? Theme.error
                                        : boton.sinRelleno ? Theme.acentoTexto
                                        : Theme.texto
    readonly property color colorBase: boton.esPrimario ? Theme.acento
                                       : boton.sinRelleno ? Theme.fondo
                                       : Theme.controlSecundario

    focusPolicy: Qt.StrongFocus
    implicitHeight: boton.compacto ? 24 : Theme.altoControl
    implicitWidth: boton.esIcono ? boton.implicitHeight
                                 : Math.max(boton.implicitHeight, contenido.implicitWidth + boton.leftPadding + boton.rightPadding)
    leftPadding: boton.esIcono ? 0 : (boton.sinRelleno ? Theme.espacioS : Theme.espacioM)
    rightPadding: boton.leftPadding
    topPadding: 0
    bottomPadding: 0
    font.family: Theme.familia
    font.pixelSize: boton.compacto ? 12 : Theme.cuerpo.size
    font.weight: Font.Medium
    opacity: boton.enabled ? 1 : 0.45

    Accessible.role: Accessible.Button
    Accessible.name: boton.esIcono ? boton.nombreAccesible : boton.text
    Accessible.description: boton.atajo.length > 0
                            ? (boton.descripcion.length > 0 ? boton.descripcion + ". " : "") + qsTr("Atajo: %1").arg(boton.atajo)
                            : boton.descripcion

    ToolTip.visible: boton.hovered && ((boton.esIcono && boton.nombreAccesible.length > 0) || boton.atajo.length > 0)
    ToolTip.text: boton.textoAyuda
    ToolTip.delay: 600

    Keys.onReturnPressed: (evento) => { if (boton.enabled) boton.click(); evento.accepted = true }
    Keys.onEnterPressed: (evento) => { if (boton.enabled) boton.click(); evento.accepted = true }

    contentItem: Item {
        implicitWidth: contenido.implicitWidth
        implicitHeight: contenido.implicitHeight

        Row {
            id: contenido
            anchors.centerIn: parent
            spacing: 6

            BusyIndicator {
                visible: boton.cargando
                running: boton.cargando
                width: 14
                height: 14
                anchors.verticalCenter: parent.verticalCenter
                Accessible.ignored: true
            }
            Icono {
                visible: !boton.cargando && boton.icono.length > 0
                nombre: boton.icono
                color: boton.colorTexto
                tamano: 16
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: !boton.esIcono && boton.text.length > 0
                text: boton.text
                font: boton.font
                color: boton.colorTexto
                elide: Text.ElideRight
                anchors.verticalCenter: parent.verticalCenter
                Accessible.ignored: true
            }
        }
    }

    background: Rectangle {
        id: fondo
        implicitHeight: boton.implicitHeight
        radius: Theme.radioControl
        color: {
            if (boton.sinRelleno)
                return boton.down ? Theme.controlSecundario : boton.hovered ? Theme.superficieSeccion : "transparent"
            if (boton.down)
                return boton.esPrimario ? Theme.acentoPresionado : Qt.darker(boton.colorBase, 1.1)
            if (boton.hovered)
                return Theme.oscuro ? Qt.lighter(boton.colorBase, 1.15) : Qt.darker(boton.colorBase, 1.04)
            return boton.colorBase
        }

        AnilloFoco {
            radioBase: fondo.radius
            visible: boton.visualFocus
        }
    }
}
