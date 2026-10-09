import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import "Etiquetas.js" as Etiquetas

// Solicitud en la tabla de Solicitudes (T013, ficha FilaSolicitud). Columnas:
// Estado (EstadoBadge, 176) | Contribuyente (RFC en cuerpoFuerte y nombre en
// leyenda, 1.5fr) | Tipo (icono arrow-up-right/arrow-down-left y texto, 112) |
// Periodo (rango legible, 1.1fr) | Paquetes (derecha, 72) | Creada (150) |
// chevron-right. Fondo: superficie, hover superficieSeccion, `seleccionada`
// Theme.seleccion; `conFoco` dibuja el anillo interior. Alto minimo 56.
// Se usa como delegado (no toma foco: el foco es de la lista).
ItemDelegate {
    id: fila

    property string estado: ""
    property string rfc: ""
    property string nombrePerfil: ""
    property string tipoDescarga: ""
    property string fechaInicial: ""
    property string fechaFinal: ""
    property int totalPaquetes: 0
    property var creadaEn: null
    property bool seleccionada: false
    property bool conFoco: false
    // Por debajo de 960 de ancho la pagina oculta la columna Creada.
    property bool mostrarCreada: true

    readonly property string textoEstado: Etiquetas.estadoResumen(fila.estado)
    readonly property string textoTipo: Etiquetas.tipoDescarga(fila.tipoDescarga)
    readonly property string textoPeriodo: FormatoFechas.rango(fila.fechaInicial, fila.fechaFinal)
    readonly property string textoCreada: FormatoFechas.fechaHora(fila.creadaEn)
    readonly property string textoPaquetes: fila.totalPaquetes === 1 ? qsTr("1 paquete")
                                                                     : qsTr("%1 paquetes").arg(fila.totalPaquetes)
    // Columna Paquetes: "—" si aun no hay paquetes y la solicitud no termino.
    readonly property string columnaPaquetes: fila.totalPaquetes === 0 && fila.estado !== "Terminada"
                                              ? "—" : String(fila.totalPaquetes)

    focusPolicy: Qt.NoFocus
    implicitHeight: Math.max(Theme.altoFila, contenido.implicitHeight + 16)
    leftPadding: Theme.espacioL
    rightPadding: Theme.espacioL
    topPadding: 8
    bottomPadding: 8

    Accessible.role: Accessible.ListItem
    Accessible.name: qsTr("%1, %2 %3, %4, %5, %6, creada %7")
                         .arg(fila.textoEstado)
                         .arg(fila.rfc).arg(fila.nombrePerfil)
                         .arg(fila.textoTipo)
                         .arg(fila.textoPeriodo)
                         .arg(fila.textoPaquetes)
                         .arg(fila.textoCreada.replace(",", ""))

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

    contentItem: RowLayout {
        id: contenido
        spacing: Theme.espacioM

        Item {
            Layout.preferredWidth: 176 - Theme.espacioM
            Layout.preferredHeight: badge.implicitHeight
            EstadoBadge {
                id: badge
                objectName: "badgeFila"
                clave: fila.estado
                width: Math.min(implicitWidth, parent.width)
            }
        }
        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Layout.preferredWidth: 15
            Layout.horizontalStretchFactor: 15
            Label {
                objectName: "rfcFila"
                text: fila.rfc
                color: Theme.texto
                font.family: Theme.familia
                font.pixelSize: Theme.cuerpoFuerte.size
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
                Accessible.ignored: true
            }
            Label {
                objectName: "nombreFila"
                visible: text.length > 0
                text: fila.nombrePerfil
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                elide: Text.ElideRight
                Layout.fillWidth: true
                Accessible.ignored: true
            }
        }
        RowLayout {
            spacing: Theme.espacioXs
            Layout.preferredWidth: 112 - Theme.espacioM
            Icono {
                nombre: fila.tipoDescarga === "Emitidos" ? "arrow-up-right" : "arrow-down-left"
                color: Theme.textoSecundario
                tamano: 14
            }
            Label {
                text: fila.textoTipo
                color: Theme.texto
                font.family: Theme.familia
                font.pixelSize: Theme.cuerpo.size
                elide: Text.ElideRight
                Layout.fillWidth: true
                Accessible.ignored: true
            }
        }
        Label {
            objectName: "periodoFila"
            text: fila.textoPeriodo
            color: Theme.texto
            font.family: Theme.familia
            font.pixelSize: Theme.cuerpo.size
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.preferredWidth: 11
            Layout.horizontalStretchFactor: 11
            Accessible.ignored: true
        }
        Label {
            objectName: "paquetesFila"
            text: fila.columnaPaquetes
            color: Theme.texto
            font.family: Theme.familia
            font.pixelSize: Theme.cuerpo.size
            font.features: { "tnum": 1 }
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 72 - Theme.espacioM
            Accessible.ignored: true
        }
        Label {
            objectName: "creadaFila"
            visible: fila.mostrarCreada
            text: fila.textoCreada
            color: Theme.textoSecundario
            font.family: Theme.familia
            font.pixelSize: Theme.cuerpo.size
            elide: Text.ElideRight
            Layout.preferredWidth: 150 - Theme.espacioM
            Accessible.ignored: true
        }
        Icono {
            nombre: "chevron-right"
            color: Theme.textoSecundario
            tamano: 16
        }
    }
}
