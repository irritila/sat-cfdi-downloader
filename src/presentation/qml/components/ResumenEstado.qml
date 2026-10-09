import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Cabecera del detalle (T013, ficha ResumenEstado): tarjeta Theme.superficie
// con borde Theme.separador y radio de tarjeta. Izquierda: EstadoBadge grande,
// titular 15/600 (Heading) y descripcion. Derecha: la accion principal
// (`textoAccion`, `accionPrimaria`, `accionHabilitada`), el `motivo` si esta
// deshabilitada y el `mensajeResultado` (nota con icono; error si
// `resultadoError`, anunciada). Abajo, la rejilla de `datos`
// ([{etiqueta, valor}], hasta 4) con linea superior Theme.separador.
Rectangle {
    id: resumen

    property string clave: ""
    property string titular: ""
    property string descripcion: ""
    property string textoAccion: ""
    property bool accionPrimaria: true
    property bool accionHabilitada: true
    property bool accionCargando: false
    property string motivo: ""
    property string mensajeResultado: ""
    property bool resultadoError: false
    property var datos: []
    readonly property alias botonAccion: accion
    // Botones de accion propios de la pagina (con su objectName y visibilidad);
    // normalmente uno solo visible. Se colocan junto a la accion de textoAccion.
    default property alias acciones: contenedorAcciones.data
    // Spinner junto al titular (p. ej. Enviando).
    property bool ocupado: false
    // objectName del motivo y del resultado (para pruebas, D10).
    property string objectNameMotivo: "motivoResumen"
    property string objectNameResultado: "resultadoResumen"

    signal accionSolicitada()

    implicitWidth: 640
    implicitHeight: columna.implicitHeight + 2 * Theme.espacioL
    radius: Theme.radioTarjeta
    color: Theme.superficie
    border.width: 1
    border.color: Theme.separador

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Resumen")

    // Sombra sutil de tarjeta solo en claro (sombra-tarjeta); en oscuro basta el borde.
    Rectangle {
        z: -1
        visible: !Theme.oscuro
        x: 0
        y: 1
        width: parent.width
        height: parent.height
        radius: resumen.radius
        color: Theme.sombra
        opacity: 0.6
    }

    ColumnLayout {
        id: columna
        anchors.fill: parent
        anchors.margins: Theme.espacioL
        spacing: Theme.espacioM

        RowLayout {
            spacing: Theme.espacioL
            Layout.fillWidth: true

            ColumnLayout {
                spacing: Theme.espacioS
                Layout.fillWidth: true
                // Badge grande y titular en la misma linea; spinner si `ocupado`.
                RowLayout {
                    spacing: Theme.espacioM
                    Layout.fillWidth: true
                    EstadoBadge {
                        objectName: "badgeResumen"
                        grande: true
                        clave: resumen.clave
                        Layout.alignment: Qt.AlignTop
                    }
                    Label {
                        objectName: "titularResumen"
                        text: resumen.titular
                        color: Theme.texto
                        font.family: Theme.familia
                        font.pixelSize: Theme.subtitulo.size
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    BusyIndicator {
                        visible: resumen.ocupado
                        running: resumen.ocupado
                        implicitWidth: 18
                        implicitHeight: 18
                        Accessible.ignored: true
                    }
                }
                Label {
                    objectName: "descripcionResumen"
                    visible: text.length > 0
                    text: resumen.descripcion
                    color: Theme.textoSecundario
                    font.family: Theme.familia
                    font.pixelSize: Theme.cuerpo.size
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                }
            }
            // Siempre presente: vacia no ocupa espacio (sus hijos deciden su visibilidad).
            ColumnLayout {
                spacing: Theme.espacioXs
                Layout.alignment: Qt.AlignTop | Qt.AlignRight
                Layout.maximumWidth: 280

                RowLayout {
                    id: contenedorAcciones
                    spacing: Theme.espacioS
                    Layout.alignment: Qt.AlignRight
                }
                BotonAccion {
                    id: accion
                    objectName: "accionResumen"
                    visible: resumen.textoAccion.length > 0
                    text: resumen.textoAccion
                    variante: resumen.accionPrimaria ? "primario" : "secundario"
                    enabled: resumen.accionHabilitada && !resumen.accionCargando
                    cargando: resumen.accionCargando
                    Layout.alignment: Qt.AlignRight
                    onClicked: resumen.accionSolicitada()
                }
                Label {
                    objectName: resumen.objectNameMotivo
                    visible: !resumen.accionHabilitada && resumen.motivo.length > 0
                    text: resumen.motivo
                    color: Theme.textoSecundario
                    font.family: Theme.familia
                    font.pixelSize: Theme.leyenda.size
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignRight
                    Layout.fillWidth: true
                }
                RowLayout {
                    objectName: resumen.objectNameResultado
                    readonly property string text: resumen.mensajeResultado
                    visible: resumen.mensajeResultado.length > 0
                    spacing: Theme.espacioXs
                    Layout.alignment: Qt.AlignRight
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: resumen.mensajeResultado
                    Icono {
                        nombre: resumen.resultadoError ? "exclamation-octagon" : "clock"
                        color: resumen.resultadoError ? Theme.error : Theme.info
                        tamano: 13
                    }
                    Label {
                        text: resumen.mensajeResultado
                        color: resumen.resultadoError ? Theme.error : Theme.textoSecundario
                        font.family: Theme.familia
                        font.pixelSize: Theme.leyenda.size
                        wrapMode: Text.WordWrap
                        Layout.maximumWidth: 260
                        Accessible.ignored: true
                    }
                }
            }
        }

        Rectangle {
            visible: resumen.datos.length > 0
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.separador
        }
        GridLayout {
            visible: resumen.datos.length > 0
            // 4 columnas; 2 si la tarjeta es angosta (ventana < 760).
            columns: Math.max(1, Math.min(resumen.width < 720 ? 2 : 4, resumen.datos.length))
            columnSpacing: Theme.espacioL
            rowSpacing: Theme.espacioS
            Layout.fillWidth: true

            Repeater {
                model: resumen.datos
                delegate: ColumnLayout {
                    id: dato
                    required property var modelData
                    required property int index
                    objectName: "datoResumen_" + index
                    spacing: 2
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Accessible.role: Accessible.StaticText
                    Accessible.name: dato.modelData.etiqueta + ": " + dato.modelData.valor
                    Label {
                        text: dato.modelData.etiqueta
                        color: Theme.textoSecundario
                        font.family: Theme.familia
                        font.pixelSize: Theme.etiqueta.size
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Accessible.ignored: true
                    }
                    Label {
                        text: dato.modelData.valor && String(dato.modelData.valor).length > 0 ? dato.modelData.valor : "—"
                        color: Theme.texto
                        font.family: Theme.familia
                        font.pixelSize: Theme.cuerpo.size
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Accessible.ignored: true
                    }
                }
            }
        }
    }
}
