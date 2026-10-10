import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import "Etiquetas.js" as Etiquetas

// Perfil en la lista de Perfiles SAT (T013, ficha FilaPerfil):
// 1) RFC en cuerpoFuerte y EstadoBadge de e.firma; 2) nombre; 3) icono de 13 y
// la disponibilidad en leyenda (con "Reintentar" sm si el estado no se pudo
// consultar); 4) opcional, calendar y "Vigente hasta AAAA-MM-DD". Estados:
// normal, `seleccionada`, `conFoco` (anillo interior) e inactivo.
// T014.3 D2: `diasParaVencer` >= 0 agrega el badge de advertencia
// "Vence en N días" (calendar-exclamation) junto a la vigencia.
// Se usa como delegado (no toma foco: el foco es de la lista; R reintenta).
ItemDelegate {
    id: fila

    property string rfc: ""
    property string nombre: ""
    property string preparacion: ""
    property string estadoTexto: ""
    property string disponibilidad: ""
    property bool activo: true
    property bool listo: false
    property var vigenteHasta: null
    // -1 = sin aviso (el view model solo lo da con credencial Lista y <= 30 dias).
    property int diasParaVencer: -1
    property bool seleccionada: false
    property bool conFoco: false
    property string objectNameReintentar: ""
    property string objectNameBadge: "badgePerfil"
    property string objectNameDisponibilidad: "disponibilidadPerfil"
    readonly property alias botonReintentar: reintentar

    signal reintentarSolicitado()

    readonly property string textoVigencia: fila.vigenteHasta
        ? qsTr("Vigente hasta %1").arg(Qt.formatDate(fila.vigenteHasta, "yyyy-MM-dd")) : ""
    readonly property string textoVencimiento: Etiquetas.venceEn(fila.diasParaVencer)

    focusPolicy: Qt.NoFocus
    leftPadding: Theme.espacioL
    rightPadding: Theme.espacioL
    topPadding: 10
    bottomPadding: 10

    Accessible.role: Accessible.ListItem
    Accessible.name: [fila.rfc, fila.nombre, fila.estadoTexto, fila.disponibilidad, fila.textoVigencia,
                      fila.textoVencimiento]
                     .filter(t => t.length > 0).join(", ")

    background: Rectangle {
        color: fila.seleccionada ? Theme.seleccion : fila.hovered ? Theme.superficieSeccion : Theme.superficie
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.separador
        }
        AnilloFoco {
            interior: true
            visible: fila.conFoco
        }
    }

    contentItem: ColumnLayout {
        spacing: 2

        RowLayout {
            spacing: Theme.espacioS
            Layout.fillWidth: true
            Label {
                objectName: "rfcPerfil"
                text: fila.rfc
                color: Theme.texto
                font.family: Theme.familia
                font.pixelSize: Theme.cuerpoFuerte.size
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
                Accessible.ignored: true
            }
            EstadoBadge {
                objectName: fila.objectNameBadge
                eFirma: true
                clave: fila.preparacion
                texto: fila.estadoTexto
                contexto: qsTr("e.firma")
            }
        }
        Label {
            objectName: "nombrePerfil"
            visible: text.length > 0
            text: fila.nombre
            color: Theme.texto
            font.family: Theme.familia
            font.pixelSize: Theme.cuerpo.size
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }
        RowLayout {
            spacing: Theme.espacioXs
            Layout.fillWidth: true
            Icono {
                nombre: fila.activo && fila.listo ? "check-circle" : "nosign"
                color: Theme.textoSecundario
                tamano: 13
            }
            Label {
                objectName: fila.objectNameDisponibilidad
                text: fila.disponibilidad
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                font.italic: !fila.activo
                elide: Text.ElideRight
                Layout.fillWidth: true
                Accessible.ignored: true
            }
            BotonAccion {
                id: reintentar
                objectName: fila.objectNameReintentar
                visible: fila.preparacion === "EstadoNoDisponible"
                compacto: true
                text: qsTr("Reintentar")
                activeFocusOnTab: true
                Accessible.name: qsTr("Reintentar estado de %1").arg(fila.rfc)
                onClicked: fila.reintentarSolicitado()
            }
        }
        RowLayout {
            visible: fila.textoVigencia.length > 0 || fila.textoVencimiento.length > 0
            spacing: Theme.espacioXs
            Icono {
                nombre: "calendar"
                color: Theme.textoSecundario
                tamano: 13
            }
            Label {
                objectName: "vigenciaPerfil"
                text: fila.textoVigencia
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                Accessible.ignored: true
            }
            EstadoBadge {
                objectName: "badgeVencimientoPerfil"
                visible: fila.textoVencimiento.length > 0
                tonoDirecto: "advertencia"
                iconoDirecto: "calendar-exclamation"
                texto: fila.textoVencimiento
                contexto: qsTr("e.firma")
                Accessible.ignored: true
            }
        }
    }
}
