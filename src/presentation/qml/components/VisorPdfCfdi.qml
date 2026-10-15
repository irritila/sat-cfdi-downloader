pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Pdf

// Visor del PDF de un CFDI (T015.4, T015 D7). Ocupa toda la pagina Facturas
// (encabezado incluido) mientras esta abierto; la lista sigue viva debajo y
// conserva la seleccion. A 960 de ancho un panel lateral dejaria la hoja
// carta ilegible, y la navegacion de T013 es por paginas: por eso es una
// vista completa con "‹ Facturas" para volver.
// - Encabezado: "‹ Facturas", titulo (tipo serie-folio) y subtitulo (RFC del
//   emisor · fecha · total), con "Guardar PDF…" y "Abrir en Vista Previa".
// - Mensajes en linea (exito o error de Guardar y Vista Previa), sin modales;
//   el unico dialogo es el selector nativo de guardado (nombre sugerido).
// - Estados (VisorPdfCfdiViewModel.estado): Generando (indicador tras 300 ms),
//   Listo (PaginasPdf ajustado al ancho hasta 125 %, "Pagina N de M") y Error con el
//   motivo y "Reintentar" (enfocado) cuando reintentar puede cambiar algo.
// Vida del documento: QtQuick.Pdf renderiza las paginas en el hilo de
// QQuickPixmapReader leyendo del PdfDocument; cambiar su `source` o destruirlo
// mientras renderiza deja ese hilo con un puntero colgante (fallo observado en
// Qt 6.10). Por eso cada PDF tiene su propio PdfDocument, que nunca cambia de
// `source`, vive bajo el view model (no bajo la pagina) y se destruye con un
// retraso (kRetiroMs) al reemplazarse, al cerrar o al destruir la pagina.
// Nitidez: las paginas se rasterizan con al menos kMinPxPorPunto (2 px/pt)
// sea cual sea el devicePixelRatio. PdfMultiPageView usa renderScale * dpr
// (~1.25 px/pt en un monitor externo con dpr 1) y a esa escala pdfium pierde
// acentos ("Bolígrafo" -> "Boligrafo"); por eso el visor usa su propia lista
// de PdfPageImage (PaginasPdf) con sourceSize = puntos * pxPorPunto().
// Teclado: Escape (o "‹ Facturas") cierra el visor y devuelve el foco a la
// fila; Tab recorre regresar, acciones, aviso y documento; ⌘+ / ⌘- / ⌘0
// cambian el zoom.
FocusScope {
    id: visor
    objectName: "visorPdfCfdi"

    required property VisorPdfCfdiViewModel vm

    readonly property bool generando: visor.vm.estado === VisorPdfCfdiViewModel.Generando
    readonly property bool listo: visor.vm.estado === VisorPdfCfdiViewModel.Listo
    readonly property bool conError: visor.vm.estado === VisorPdfCfdiViewModel.Error
    // PdfDocument del PDF actual (null sin PDF) y su estado.
    property PdfDocument documento: null
    property int estadoDocumento: PdfDocument.Null
    // La vista de paginas se destruye antes de soltar su documento.
    property bool vistaActiva: false
    readonly property PaginasPdf vista: vistaPdf.item as PaginasPdf
    readonly property real kMinPxPorPunto: 2
    readonly property real kMaxPxPorPunto: 6

    // Pixeles por punto PDF con los que se rasteriza una pagina a la escala
    // de pantalla `escala` (pt -> px logicos) y el dpr dado: nunca menos de
    // kMinPxPorPunto (acentos legibles con dpr 1) ni mas de kMaxPxPorPunto
    // (memoria acotada con zoom alto en Retina).
    function pxPorPunto(escala, dpr) {
        return Math.min(visor.kMaxPxPorPunto, Math.max(visor.kMinPxPorPunto, escala * dpr))
    }

    // Lista vertical de paginas (PdfPageImage asincrono), centradas y con
    // desplazamiento horizontal si el zoom las hace mas anchas que la vista.
    component PaginasPdf: ListView {
        id: paginas
        property real renderScale: 1
        property real dpr: Screen.devicePixelRatio
        readonly property real anchoPaginas: visor.documento !== null && visor.paginas > 0
                                             ? visor.documento.maxPageWidth * paginas.renderScale : 0
        readonly property int currentPage: Math.max(0, paginas.indexAt(paginas.contentX + paginas.width / 2,
                                                                        paginas.contentY + paginas.height / 3))
        clip: true
        spacing: Theme.espacioM
        topMargin: Theme.espacioM
        bottomMargin: Theme.espacioM
        contentWidth: Math.max(paginas.width, paginas.anchoPaginas + 2 * Theme.espacioM)
        flickableDirection: Flickable.AutoFlickIfNeeded
        boundsBehavior: Flickable.StopAtBounds
        model: visor.paginas
        ScrollBar.vertical: ScrollBar { }
        ScrollBar.horizontal: ScrollBar { }
        delegate: Item {
            id: hoja
            required property int index
            readonly property size puntos: visor.documento !== null ? visor.documento.pagePointSize(hoja.index)
                                                                    : Qt.size(1, 1)
            width: paginas.contentWidth
            height: hoja.puntos.height * paginas.renderScale
            Rectangle {
                anchors.fill: imagen
                color: "white"
                border.width: 1
                border.color: Theme.separador
            }
            PdfPageImage {
                id: imagen
                objectName: "imagenPaginaPdf_" + hoja.index
                document: visor.documento
                currentFrame: hoja.index
                asynchronous: true
                fillMode: Image.PreserveAspectFit
                anchors.horizontalCenter: parent.horizontalCenter
                width: hoja.puntos.width * paginas.renderScale
                height: hoja.puntos.height * paginas.renderScale
                // Solo el ancho: el alto sigue la proporcion de la pagina.
                sourceSize.width: Math.ceil(hoja.puntos.width * visor.pxPorPunto(paginas.renderScale, paginas.dpr))
                sourceSize.height: 0
                Accessible.ignored: true
            }
        }
    }
    readonly property int paginas: visor.documento !== null && visor.documentoListo ? visor.documento.pageCount : 0
    readonly property bool documentoListo: visor.listo && visor.documento !== null
                                           && visor.estadoDocumento === PdfDocument.Ready
    readonly property int kRetiroMs: 10000
    // Abierto el selector de guardado (Main.qml suspende los atajos).
    property bool dialogoAbierto: false
    readonly property alias botonGuardar: botonGuardar
    readonly property alias botonVistaPrevia: botonVistaPrevia

    signal cerrarSolicitado()

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("PDF de la factura: %1").arg(visor.vm.nombreAccesibleCfdi)

    Keys.onEscapePressed: visor.cerrarSolicitado()
    Keys.onPressed: (evento) => {
        if (!visor.documentoListo || visor.vista === null)
            return
        if (evento.matches(StandardKey.ZoomIn)) {
            visor.vista.renderScale = Math.min(3, visor.vista.renderScale * 1.25)
            evento.accepted = true
        } else if (evento.matches(StandardKey.ZoomOut)) {
            visor.vista.renderScale = Math.max(0.25, visor.vista.renderScale / 1.25)
            evento.accepted = true
        } else if (evento.key === Qt.Key_0 && (evento.modifiers & Qt.ControlModifier)) {
            visor.ajustarAncho()
            evento.accepted = true
        }
    }

    // Foco inicial: Reintentar si hay error; si no, el boton de regreso (el
    // documento no tiene foco propio).
    function enfocarInicial() {
        if (error.visible && error.botonAccion.visible)
            error.botonAccion.forceActiveFocus(Qt.OtherFocusReason)
        else
            encabezado.botonRegresar.forceActiveFocus(Qt.OtherFocusReason)
    }

    function ajustarAncho() {
        // Ajuste al ancho disponible, sin pasar de 125 % (la hoja carta a 960
        // de ancho queda legible y se ve mas de media pagina).
        if (!visor.documentoListo || visor.vista === null)
            return
        const ancho = visor.documento.pagePointSize(0).width
        if (ancho > 0)
            visor.vista.renderScale = Math.max(0.25, Math.min(1.25, (visor.vista.width - 2 * Theme.espacioXl) / ancho))
    }

    function abrirDialogoGuardar() {
        if (!visor.vm.puedeGuardar || visor.dialogoAbierto)
            return
        const dialogo = dialogoGuardarComp.createObject(visor)
        if (!dialogo)
            return
        // Carpeta (ultima usada o Documentos) y nombre sugerido sin RFC.
        dialogo.currentFolder = visor.vm.carpetaGuardado
        dialogo.selectedFile = visor.vm.archivoSugerido
        visor.dialogoAbierto = true
        dialogo.accepted.connect(() => {
            visor.vm.guardarEn(dialogo.selectedFile)
            visor.cerrarDialogo(dialogo)
        })
        dialogo.rejected.connect(() => visor.cerrarDialogo(dialogo))
        dialogo.open()
    }

    function cerrarDialogo(dialogo) {
        visor.dialogoAbierto = false
        dialogo.destroy()
        botonGuardar.forceActiveFocus(Qt.OtherFocusReason)
    }

    // Un PdfDocument por PDF (ver "Vida del documento").
    Component {
        id: documentoComp
        PdfDocument { }
    }

    function cargarDocumento() {
        visor.retirarDocumento()
        const fuente = visor.vm.fuente
        if (fuente.toString().length === 0)
            return
        const d = documentoComp.createObject(visor.vm, { source: fuente }) as PdfDocument
        if (!d) {
            visor.vm.documentoIlegible()
            return
        }
        d.statusChanged.connect(visor.alCambiarEstadoDocumento)
        visor.documento = d
        visor.vistaActiva = true
        visor.alCambiarEstadoDocumento()
    }

    function retirarDocumento() {
        const d = visor.documento
        if (d === null)
            return
        d.statusChanged.disconnect(visor.alCambiarEstadoDocumento)
        visor.vistaActiva = false
        visor.documento = null
        visor.estadoDocumento = PdfDocument.Null
        d.destroy(visor.kRetiroMs)
    }

    function alCambiarEstadoDocumento() {
        if (visor.documento === null)
            return
        visor.estadoDocumento = visor.documento.status
        if (visor.estadoDocumento === PdfDocument.Error)
            visor.vm.documentoIlegible()
        else if (visor.estadoDocumento === PdfDocument.Ready)
            Qt.callLater(visor.ajustarAncho)
    }

    Connections {
        target: visor.vm
        function onEstadoChanged() {
            // Error "documento" conserva el PDF para Vista Previa; el resto
            // de los cambios cargan el PDF nuevo (o ninguno).
            if (visor.vm.motivo !== "documento")
                visor.cargarDocumento()
            // El error lleva el foco a "Reintentar" (o al regreso si no aplica).
            if (visor.conError && visor.visible)
                Qt.callLater(visor.enfocarInicial)
        }
    }
    Component.onDestruction: visor.retirarDocumento()

    Component {
        id: dialogoGuardarComp
        FileDialog {
            title: qsTr("Guardar PDF")
            fileMode: FileDialog.SaveFile
            defaultSuffix: "pdf"
            nameFilters: [qsTr("Documento PDF (*.pdf)")]
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.superficie
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        EncabezadoPagina {
            id: encabezado
            objectName: "encabezadoVisorPdf"
            Layout.fillWidth: true
            titulo: visor.vm.titulo
            subtitulo: visor.vm.subtitulo
            mostrarRegresar: true
            textoRegresar: qsTr("Facturas")
            descripcionRegresar: qsTr("Cerrar el PDF y regresar a la lista de facturas (Escape)")
            atajoRegresar: ""
            onRegresarSolicitado: visor.cerrarSolicitado()

            BotonAccion {
                id: botonGuardar
                objectName: "botonGuardarPdf"
                text: qsTr("Guardar PDF…")
                descripcion: qsTr("Guardar una copia del PDF en la carpeta que elijas")
                enabled: visor.vm.puedeGuardar && !visor.dialogoAbierto
                cargando: visor.vm.guardando
                onClicked: visor.abrirDialogoGuardar()
            }
            BotonAccion {
                id: botonVistaPrevia
                objectName: "botonVistaPreviaPdf"
                text: qsTr("Abrir en Vista Previa")
                icono: "arrow-up-right"
                descripcion: qsTr("Abrir el PDF en la app Vista Previa de macOS")
                enabled: visor.vm.puedeAbrirVistaPrevia
                cargando: visor.vm.abriendoVistaPrevia
                onClicked: visor.vm.abrirEnVistaPrevia()
            }
        }

        // Resultado de Guardar o Vista Previa (en linea, sin modal).
        AvisoEnLinea {
            objectName: "mensajeVisorPdf"
            visible: visor.vm.mensaje.length > 0
            variante: visor.vm.mensajeEsError ? "error" : "exito"
            titulo: visor.vm.mensaje
            Layout.fillWidth: true
            Layout.leftMargin: Theme.espacioL
            Layout.rightMargin: Theme.espacioL
            Layout.topMargin: Theme.espacioM
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                color: Theme.superficieSeccion
                visible: visor.listo
            }

            // Se recrea con cada documento (el visor anterior no debe pedir
            // paginas a un PdfDocument retirado).
            Loader {
                id: vistaPdf
                objectName: "vistaPdf"
                anchors.fill: parent
                active: visor.vistaActiva
                visible: visor.documentoListo
                sourceComponent: PaginasPdf {
                    Accessible.role: Accessible.Document
                    Accessible.name: qsTr("Documento PDF, %n página(s)", "", visor.paginas)
                    onWidthChanged: if (visor.documentoListo) Qt.callLater(visor.ajustarAncho)
                }
            }

            Label {
                objectName: "paginaPdf"
                visible: visor.paginas > 1
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.espacioM
                padding: Theme.espacioXs
                leftPadding: Theme.espacioS
                rightPadding: Theme.espacioS
                text: qsTr("Página %1 de %2").arg(visor.vista !== null ? visor.vista.currentPage + 1 : 1).arg(visor.paginas)
                color: Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.leyenda.size
                background: Rectangle {
                    radius: 6
                    color: Theme.superficieElevada
                    border.width: 1
                    border.color: Theme.separador
                }
            }

            // Estado: generando (solo si tarda mas de 300 ms).
            ColumnLayout {
                objectName: "estadoGenerandoPdf"
                anchors.centerIn: parent
                visible: (visor.generando || (visor.listo && !visor.documentoListo)) && !retraso.running
                spacing: Theme.espacioS
                Accessible.role: Accessible.StaticText
                Accessible.name: textoGenerando.text

                Timer {
                    id: retraso
                    interval: 300
                    running: visor.generando
                }
                BusyIndicator {
                    running: parent.visible
                    Layout.alignment: Qt.AlignHCenter
                    Accessible.ignored: true
                }
                Label {
                    id: textoGenerando
                    text: qsTr("Generando el PDF…")
                    color: Theme.textoSecundario
                    font.family: Theme.familia
                    font.pixelSize: Theme.cuerpo.size
                    Layout.alignment: Qt.AlignHCenter
                    Accessible.ignored: true
                }
            }

            // Estado: error con el motivo (reemplaza el documento).
            EstadoVacio {
                id: error
                objectName: "estadoErrorPdf"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                visible: visor.conError
                variante: "error"
                titulo: visor.vm.errorMessage
                descripcion: visor.vm.errorDescripcion
                textoAccion: visor.vm.puedeReintentar ? qsTr("Reintentar") : ""
                objectNameTitulo: "errorPdf"
                objectNameAccion: "botonReintentarPdf"
                onAccionSolicitada: visor.vm.reintentar()
            }
        }
    }
}
