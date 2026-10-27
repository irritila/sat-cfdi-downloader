import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// T025.6: base visual de los dialogos de Respaldo (ficha EFirmaDialogo):
// contenedor Theme.superficieElevada (radio 12) con velo, ancho 480, padding
// 20, cabecera con icono en tono y titulo; el contenido va debajo
// (propiedad por omision). Modal; Popup es barrera de Tab (el foco no sale
// del dialogo). `bloqueado`: sin cierre con Escape ni clic fuera (operacion
// en curso). Rol Dialog con el titulo como nombre accesible.
// T024.4: el alto se limita a la ventana (16 px arriba y abajo). Cabecera y
// `acciones` (fila de botones) quedan fijas; si el contenido no cabe, se
// desplaza con barra visible y el foco de teclado lo lleva al control
// enfocado. Si cabe, el aspecto es el de siempre (sin recorte ni barra).
Dialog {
    id: dialogo

    property string icono: "lock"
    property string tono: "progreso"
    property string prefijoNombre: "dialogoRespaldo"
    property bool bloqueado: false
    property string descripcionAccesible: ""
    default property alias contenido: columna.data
    // Botones de accion, fijos al pie (fila con espacio S).
    property alias acciones: pie.data
    property bool accionesVisibles: true
    readonly property bool desbordado: desplazable.desbordado
    readonly property alias desplazable: desplazable

    readonly property var colores: Theme.tono(dialogo.tono)

    modal: true
    focus: true
    closePolicy: dialogo.bloqueado ? Popup.NoAutoClose : Popup.CloseOnEscape
    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.max(16, Math.min(104, parent.height - height - 16)) : 0
    width: Math.min(Theme.anchoDialogo, (parent ? parent.width : Theme.anchoDialogo) - 32)
    height: parent ? Math.min(implicitHeight, parent.height - 32) : implicitHeight
    padding: 20
    header: null
    footer: null

    Overlay.modal: Rectangle {
        color: Theme.velo
    }

    background: Rectangle {
        radius: 12
        color: Theme.superficieElevada
        border.width: 1
        border.color: Theme.separador
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 6
            anchors.bottomMargin: -10
            anchors.leftMargin: -4
            anchors.rightMargin: -4
            radius: 16
            color: Theme.sombra
        }
    }

    onAboutToShow: desplazable.contentY = 0

    // Lleva al area visible el control que recibe el foco (Tab o
    // forceActiveFocus) si esta dentro del contenido desplazable.
    function asegurarVisible(item) {
        if (!item || !desplazable.desbordado)
            return
        let p = item
        while (p && p !== columna)
            p = p.parent
        if (!p)
            return
        const r = item.mapToItem(columna, 0, 0, item.width, item.height)
        const m = desplazable.margen
        // Coordenadas del contenido: la columna empieza en y = margen.
        const arriba = r.y
        const abajo = r.y + r.height + 2 * m
        const alto = desplazable.height
        let y = desplazable.contentY
        if (arriba < y || abajo - arriba > alto)
            y = arriba
        else if (abajo > y + alto)
            y = abajo - alto
        desplazable.contentY = Math.max(0, Math.min(y, desplazable.contentHeight - alto))
    }

    Connections {
        target: marco.Window.window
        enabled: dialogo.opened
        function onActiveFocusItemChanged() {
            const item = marco.Window.window ? marco.Window.window.activeFocusItem : null
            Qt.callLater(dialogo.asegurarVisible, item)
        }
    }

    contentItem: ColumnLayout {
        id: marco
        objectName: dialogo.prefijoNombre + "Contenido"
        spacing: Theme.espacioM
        Accessible.role: Accessible.Dialog
        Accessible.name: dialogo.title
        Accessible.description: dialogo.descripcionAccesible

        RowLayout {
            spacing: Theme.espacioM
            Layout.fillWidth: true
            Rectangle {
                implicitWidth: 40
                implicitHeight: 40
                radius: 10
                color: dialogo.colores.fondo
                border.width: 1
                border.color: dialogo.colores.borde
                Icono {
                    anchors.centerIn: parent
                    nombre: dialogo.icono
                    color: dialogo.colores.texto
                    tamano: 20
                }
            }
            Label {
                objectName: dialogo.prefijoNombre + "Titulo"
                text: dialogo.title
                color: Theme.texto
                font.family: Theme.familia
                font.pixelSize: Theme.subtitulo.size
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
        }

        // Contenido: mide lo mismo que la columna; si el dialogo se limita,
        // se encoge y desplaza. El Flickable sobresale `margen` px para no
        // recortar anillos de foco (4 px) cuando recorta.
        Item {
            id: zona
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: columna.implicitHeight
            Layout.minimumHeight: Math.min(columna.implicitHeight, 48)
            implicitWidth: columna.implicitWidth

            Flickable {
                id: desplazable
                objectName: dialogo.prefijoNombre + "Desplazable"
                readonly property int margen: 6
                readonly property bool desbordado: columna.implicitHeight > zona.height + 0.5
                anchors.fill: parent
                anchors.margins: -desplazable.margen
                contentWidth: width
                contentHeight: columna.implicitHeight + 2 * desplazable.margen
                clip: desplazable.desbordado
                interactive: desplazable.desbordado
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                ScrollBar.vertical: barra
                onContentHeightChanged: desplazable.returnToBounds()
                onHeightChanged: desplazable.returnToBounds()

                ColumnLayout {
                    id: columna
                    x: desplazable.margen
                    y: desplazable.margen
                    width: zona.width
                    spacing: Theme.espacioM
                }
            }

            // En el padding derecho del dialogo, fuera del contenido.
            ScrollBar {
                id: barra
                objectName: dialogo.prefijoNombre + "Barra"
                x: zona.width + 4
                y: 0
                height: zona.height
                policy: desplazable.desbordado ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                visible: desplazable.desbordado
                Accessible.name: qsTr("Desplazar el contenido")
            }
        }

        RowLayout {
            id: pie
            objectName: dialogo.prefijoNombre + "Acciones"
            visible: dialogo.accionesVisibles
            spacing: Theme.espacioS
            Layout.fillWidth: true
            Layout.topMargin: Theme.espacioXs
        }
    }
}
