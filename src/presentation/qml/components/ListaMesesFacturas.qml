pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Lista lateral de meses de Facturas (T023.4, D9; prototipo ajustado T023.1).
// Solo representa MesesFacturasModel: estado, conteo, nombre accesible y
// "futuro" vienen de C++; la seleccion se pide con mesElegido y la valida el
// view model (los meses futuros no se seleccionan).
// - Selector de anio con chevrones (habilitados segun puedeAnio*).
// - Cada mes: nombre y numero de facturas en la primera linea, estado (icono
//   y texto, no solo color) en la segunda. Alto derivado del contenido con
//   padding espacioS; margen lateral espacioL, igual que la lista principal.
// Teclado: Tab entra a la lista; flechas arriba/abajo piden el mes anterior o
// siguiente; Enter/Espacio no hacen falta (la seleccion es inmediata).
Rectangle {
    id: panel
    objectName: "listaMesesFacturas"

    required property MesesFacturasModel meses
    property int anio: 0
    property bool puedeAnioAnterior: false
    property bool puedeAnioSiguiente: false

    readonly property alias lista: lista

    // Foco puesto por la pagina (inicial): sin anillo hasta la siguiente tecla.
    function enfocarSinAnillo() {
        lista.focoSilencioso = true
        lista.forceActiveFocus(Qt.OtherFocusReason)
    }

    signal mesElegido(int anio, int mes)
    signal anioCambiado(int delta)
    signal moverSolicitado(int delta)

    // Tonos del view model (neutro, info, exito, aviso, error) a tonos de Theme.
    function colores(tono) {
        switch (tono) {
        case "info": return Theme.tono("progreso")
        case "aviso": return Theme.tono("advertencia")
        default: return Theme.tono(tono)
        }
    }

    color: Theme.superficieSeccion

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.separador
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.espacioL
        anchors.bottomMargin: Theme.espacioS
        spacing: Theme.espacioS

        RowLayout {
            Layout.leftMargin: Theme.espacioL
            Layout.rightMargin: Theme.espacioL
            Layout.fillWidth: true
            BotonAccion {
                objectName: "botonAnioAnterior"
                variante: "icono"
                compacto: true
                icono: "chevron-left"
                nombreAccesible: qsTr("Año anterior")
                enabled: panel.puedeAnioAnterior
                onClicked: panel.anioCambiado(-1)
            }
            Label {
                objectName: "anioMeses"
                text: panel.anio > 0 ? String(panel.anio) : ""
                color: Theme.texto
                font.family: Theme.familia
                font.pixelSize: Theme.subtitulo.size
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: qsTr("Año %1").arg(panel.anio)
            }
            BotonAccion {
                objectName: "botonAnioSiguiente"
                variante: "icono"
                compacto: true
                icono: "chevron-right"
                nombreAccesible: qsTr("Año siguiente")
                enabled: panel.puedeAnioSiguiente
                onClicked: panel.anioCambiado(1)
            }
        }

        ListView {
            id: lista
            objectName: "listaMeses"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.meses
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true
            keyNavigationEnabled: false
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Meses de %1").arg(panel.anio)
            Keys.onUpPressed: { lista.focoSilencioso = false; panel.moverSolicitado(-1) }
            Keys.onDownPressed: { lista.focoSilencioso = false; panel.moverSolicitado(1) }
            property bool focoSilencioso: false
            onActiveFocusChanged: if (!lista.activeFocus) lista.focoSilencioso = false

            // Fila seleccionada (la decide el view model); se mantiene visible.
            property int filaSeleccionada: -1
            onFilaSeleccionadaChanged: {
                lista.currentIndex = lista.filaSeleccionada
                if (lista.filaSeleccionada >= 0)
                    posicionar.restart()
            }
            // Diferido (la vista aplica los cambios en su pulido); el Timer
            // muere con la lista, sin llamadas pendientes tras destruirla.
            Timer {
                id: posicionar
                interval: 0
                onTriggered: lista.positionViewAtIndex(lista.filaSeleccionada, ListView.Contain)
            }

            delegate: ItemDelegate {
                id: mes
                required property int index
                required property int anio
                required property int mes
                required property string nombre
                required property string estadoTexto
                required property string tono
                required property string icono
                required property int conteo
                required property string textoConteo
                required property bool futuro
                required property bool seleccionado
                required property string nombreAccesible

                readonly property var coloresTono: panel.colores(mes.tono)

                objectName: "mes_" + mes.mes
                width: ListView.view.width
                implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
                enabled: !mes.futuro
                focusPolicy: Qt.NoFocus
                leftPadding: Theme.espacioL
                rightPadding: Theme.espacioL
                topPadding: Theme.espacioS
                bottomPadding: Theme.espacioS
                Accessible.role: Accessible.ListItem
                Accessible.name: mes.nombreAccesible
                Accessible.selected: mes.seleccionado
                onClicked: panel.mesElegido(mes.anio, mes.mes)

                Component.onCompleted: if (mes.seleccionado) lista.filaSeleccionada = mes.index
                onSeleccionadoChanged: if (mes.seleccionado) lista.filaSeleccionada = mes.index

                background: Rectangle {
                    color: mes.seleccionado ? Theme.seleccion : mes.hovered ? Theme.superficie : "transparent"
                    AnilloFoco {
                        interior: true
                        visible: lista.activeFocus && !lista.focoSilencioso && mes.seleccionado
                    }
                }

                contentItem: ColumnLayout {
                    spacing: Theme.espacioXs / 2
                    opacity: mes.futuro ? 0.55 : 1
                    RowLayout {
                        spacing: Theme.espacioS
                        Layout.fillWidth: true
                        Label {
                            text: mes.nombre
                            color: Theme.texto
                            font.family: Theme.familia
                            font.pixelSize: Theme.cuerpo.size
                            font.weight: mes.seleccionado ? Font.DemiBold : Font.Normal
                            font.capitalization: Font.Capitalize
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                        Label {
                            objectName: "conteoMes"
                            visible: text.length > 0
                            // Solo el numero; "214 facturas" queda en el nombre
                            // accesible. Sin texto de conteo (futuro o
                            // desconocido) no se muestra.
                            text: mes.textoConteo.length > 0 ? String(mes.conteo) : ""
                            color: Theme.textoSecundario
                            font.family: Theme.familia
                            font.pixelSize: Theme.leyenda.size
                            font.features: { "tnum": 1 }
                            Accessible.ignored: true
                        }
                    }
                    RowLayout {
                        spacing: Theme.espacioXs
                        Layout.fillWidth: true
                        Icono {
                            nombre: mes.icono
                            color: mes.coloresTono.texto
                            tamano: 12
                        }
                        Label {
                            objectName: "estadoMes"
                            text: mes.estadoTexto
                            color: mes.tono === "neutro" || mes.tono.length === 0 ? Theme.textoSecundario : mes.coloresTono.texto
                            font.family: Theme.familia
                            font.pixelSize: Theme.leyenda.size
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                    }
                }
            }
        }
    }
}
