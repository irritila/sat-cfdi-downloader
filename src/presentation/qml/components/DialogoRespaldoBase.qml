import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// T025.6: base visual de los dialogos de Respaldo (ficha EFirmaDialogo):
// contenedor Theme.superficieElevada (radio 12) con velo, ancho 480, padding
// 20, cabecera con icono en tono y titulo; el contenido va debajo
// (propiedad por omision). Modal; Popup es barrera de Tab (el foco no sale
// del dialogo). `bloqueado`: sin cierre con Escape ni clic fuera (operacion
// en curso). Rol Dialog con el titulo como nombre accesible.
Dialog {
    id: dialogo

    property string icono: "lock"
    property string tono: "progreso"
    property string prefijoNombre: "dialogoRespaldo"
    property bool bloqueado: false
    property string descripcionAccesible: ""
    default property alias contenido: columna.data

    readonly property var colores: Theme.tono(dialogo.tono)

    modal: true
    focus: true
    closePolicy: dialogo.bloqueado ? Popup.NoAutoClose : Popup.CloseOnEscape
    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.max(16, Math.min(104, parent.height - height - 16)) : 0
    width: Math.min(Theme.anchoDialogo, (parent ? parent.width : Theme.anchoDialogo) - 32)
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

    contentItem: ColumnLayout {
        id: columna
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
    }
}
