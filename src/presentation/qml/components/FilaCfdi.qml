import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// CFDI en la lista del explorador (T015.3). Dos lineas por columna, como
// FilaSolicitud:
//   Fecha (fecha / tipo · serie-folio, 120) | Emisor (RFC / nombre, 1fr) |
//   Receptor (RFC / nombre, 1fr) | Total (total tal cual el XML / moneda ·
//   metodo de pago, 130, derecha) | UUID (8 primeros + Copiar / estado de
//   parseo con icono, 140).
// Fondo: superficie, hover superficieSeccion, `seleccionada` Theme.seleccion;
// `conFoco` dibuja el anillo interior. Se usa como delegado (no toma foco: el
// foco es de la lista); "Copiar UUID" se alcanza con el puntero o con ⌘C en la
// lista. Nombre accesible: "tipo, fecha, emisor, total" (view model).
ItemDelegate {
    id: fila

    property string fecha: ""
    property string tipoTexto: ""
    property string serieFolio: ""
    property string emisorRfc: ""
    property string emisorNombre: ""
    property string receptorRfc: ""
    property string receptorNombre: ""
    property string total: ""
    property string moneda: ""
    property string metodoPago: ""
    property string uuid: ""
    property string uuidCorto: ""
    property string estadoParseo: "Completo"
    property string estadoParseoTexto: ""
    property string nombreAccesible: ""
    property bool seleccionada: false
    property bool conFoco: false
    // Por debajo de 760 de ancho se oculta la columna Receptor (queda en el
    // nombre accesible y en la previsualizacion).
    property bool mostrarReceptor: true

    readonly property var iconoEstado: {
        switch (fila.estadoParseo) {
        case "Completo": return { nombre: "check-circle", color: Theme.exito }
        case "Parcial": return { nombre: "exclamation-triangle", color: Theme.advertencia }
        case "VersionNoSoportada": return { nombre: "question-diamond", color: Theme.advertencia }
        default: return { nombre: "xmark-octagon", color: Theme.error }
        }
    }

    focusPolicy: Qt.NoFocus
    implicitHeight: Math.max(Theme.altoFila, contenido.implicitHeight + 16)
    leftPadding: Theme.espacioL
    rightPadding: Theme.espacioL
    topPadding: 8
    bottomPadding: 8

    Accessible.role: Accessible.ListItem
    Accessible.name: fila.nombreAccesible

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

    component Linea: Label {
        property bool secundaria: false
        color: secundaria ? Theme.textoSecundario : Theme.texto
        font.family: Theme.familia
        font.pixelSize: secundaria ? Theme.leyenda.size : Theme.cuerpo.size
        elide: Text.ElideRight
        textFormat: Text.PlainText
        Layout.fillWidth: true
        Accessible.ignored: true
    }

    contentItem: RowLayout {
        id: contenido
        spacing: Theme.espacioM

        ColumnLayout {
            spacing: 0
            Layout.preferredWidth: 120 - Theme.espacioM
            Layout.maximumWidth: 120 - Theme.espacioM
            Linea {
                objectName: "fechaCfdi"
                text: fila.fecha.length > 0 ? fila.fecha : "—"
            }
            Linea {
                objectName: "tipoCfdi"
                secundaria: true
                text: [fila.tipoTexto, fila.serieFolio].filter(t => t.length > 0).join(" · ")
            }
        }
        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Layout.preferredWidth: 10
            Layout.horizontalStretchFactor: 10
            Linea {
                objectName: "emisorRfcCfdi"
                text: fila.emisorRfc.length > 0 ? fila.emisorRfc : "—"
                font.pixelSize: Theme.cuerpoFuerte.size
                font.weight: Font.DemiBold
            }
            Linea {
                objectName: "emisorNombreCfdi"
                visible: text.length > 0
                secundaria: true
                text: fila.emisorNombre
            }
        }
        ColumnLayout {
            visible: fila.mostrarReceptor
            spacing: 0
            Layout.fillWidth: true
            Layout.preferredWidth: 10
            Layout.horizontalStretchFactor: 10
            Linea {
                objectName: "receptorRfcCfdi"
                text: fila.receptorRfc.length > 0 ? fila.receptorRfc : "—"
            }
            Linea {
                objectName: "receptorNombreCfdi"
                visible: text.length > 0
                secundaria: true
                text: fila.receptorNombre
            }
        }
        ColumnLayout {
            spacing: 0
            Layout.preferredWidth: 130 - Theme.espacioM
            Layout.maximumWidth: 130 - Theme.espacioM
            Linea {
                objectName: "totalCfdi"
                text: fila.total.length > 0 ? fila.total : "—"
                font.features: { "tnum": 1 }
                horizontalAlignment: Text.AlignRight
            }
            Linea {
                objectName: "monedaCfdi"
                secundaria: true
                text: [fila.moneda, fila.metodoPago].filter(t => t.length > 0).join(" · ")
                horizontalAlignment: Text.AlignRight
            }
        }
        ColumnLayout {
            spacing: 0
            Layout.preferredWidth: 140 - Theme.espacioM
            Layout.maximumWidth: 140 - Theme.espacioM
            RowLayout {
                spacing: 2
                Layout.fillWidth: true
                Label {
                    objectName: "uuidCfdi"
                    text: fila.uuidCorto.length > 0 ? fila.uuidCorto : "—"
                    color: Theme.texto
                    font.family: Theme.familiaMono
                    font.pixelSize: Theme.mono.size
                    textFormat: Text.PlainText
                    Accessible.ignored: true
                }
                BotonCopiar {
                    objectName: "botonCopiarUuid"
                    focusPolicy: Qt.NoFocus
                    valor: fila.uuid
                    descripcionValor: qsTr("UUID")
                    implicitHeight: 20
                }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                spacing: Theme.espacioXs
                Layout.fillWidth: true
                Icono {
                    nombre: fila.iconoEstado.nombre
                    color: fila.iconoEstado.color
                    tamano: 12
                }
                Linea {
                    objectName: "estadoParseoCfdi"
                    secundaria: true
                    text: fila.estadoParseoTexto
                }
            }
        }
    }
}
