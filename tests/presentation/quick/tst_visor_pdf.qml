import QtQuick
import QtTest
import SatCfdiDownloader

// Visor del PDF en el explorador (T015.4): Enter, "Ver PDF" y doble clic lo
// abren con el estado generando; el PDF sintetico (QPdfWriter del fixture) se
// muestra con QtQuick.Pdf; Escape y "‹ Facturas" lo cierran y devuelven el foco
// a la fila; error con motivo y "Reintentar" (solo si puede cambiar algo);
// Guardar y Vista Previa con mensajes en linea; claro/oscuro.
TestCase {
    id: caso
    name: "VisorPdfCfdi"
    when: windowShown
    visible: true
    width: 960
    height: 640

    Component {
        id: paginaComp
        ExploradorCfdiPage {
            width: caso.width
            height: caso.height
            app: fixtureExplorador.app
            explorador: fixtureExplorador.explorador
            detalle: fixtureExplorador.detalle
        }
    }

    function init() {
        failOnWarning(/^(?!Populating font family aliases took).*/)
        wait(0)
        Theme.oscuro = false
        fixtureExplorador.reiniciar()
        fixtureExplorador.activarPrevisualizacion()
    }

    function cleanup() {
        Theme.oscuro = false
    }

    function paginaCon(n) {
        fixtureExplorador.abrirDetalle(["Descargado"])
        tryVerify(() => fixtureExplorador.detalle.cargada)
        verify(fixtureExplorador.app.abrirFacturas(fixtureExplorador.solicitudId))
        const p = createTemporaryObject(paginaComp, caso)
        verify(p)
        fixtureExplorador.resolverResumen(fixtureExplorador.numResumenes() - 1, { total: n })
        fixtureExplorador.resolverLista(fixtureExplorador.numListas() - 1, n)
        tryCompare(fixtureExplorador.explorador, "estado", ExploradorCfdiViewModel.ConDatos)
        const lista = findChild(p, "listaFacturas")
        tryVerify(() => lista.activeFocus && lista.currentIndex === 0)
        return p
    }

    // Abre el visor de la fila actual con Enter y entrega un PDF de `paginas`.
    function visorListo(p, paginas) {
        keyClick(Qt.Key_Return)
        fixtureExplorador.resolverPdf(fixtureExplorador.numGeneraciones() - 1, paginas)
        const visor = findChild(p, "visorPdfCfdi")
        tryVerify(() => visor.documentoListo, 5000)
        return visor
    }

    function test_enterAbreElVisorYEscapeVuelveALaFila() {
        const p = paginaCon(3)
        const lista = findChild(p, "listaFacturas")
        const visor = findChild(p, "visorPdfCfdi")
        verify(!visor.visible)
        keyClick(Qt.Key_Down)
        compare(lista.currentIndex, 1)
        keyClick(Qt.Key_Return)
        // Generando: el visor cubre la lista y la pagina (encabezado incluido).
        compare(fixtureExplorador.numGeneraciones(), 1)
        compare(fixtureExplorador.ultimoPdfPedido(), "PAQ_01:1")
        tryVerify(() => visor.visible && visor.generando)
        verify(!p.header.visible)
        verify(!lista.visible)
        tryVerify(() => visor.activeFocus)
        compare(findChild(visor, "tituloEncabezado").text, "Ingreso A-101")
        compare(visor.Accessible.name, "PDF de la factura: " + lista.itemAtIndex(1).Accessible.name)
        // Indicador tras 300 ms.
        tryVerify(() => findChild(visor, "estadoGenerandoPdf").visible)
        verify(!findChild(visor, "botonGuardarPdf").enabled)
        // ⌘F no aplica con el visor abierto.
        verify(!p.puedeBuscar)

        fixtureExplorador.resolverPdf(0, 2)
        tryVerify(() => visor.documentoListo, 5000)
        compare(visor.paginas, 2)
        verify(findChild(visor, "vistaPdf").visible)
        verify(!findChild(visor, "estadoGenerandoPdf").visible)
        compare(findChild(visor, "paginaPdf").text, "Página 1 de 2")
        verify(findChild(visor, "botonGuardarPdf").enabled)
        verify(findChild(visor, "botonVistaPreviaPdf").enabled)

        // Escape cierra el visor: lista visible con el foco en la misma fila.
        keyClick(Qt.Key_Escape)
        tryVerify(() => !visor.visible)
        verify(p.header.visible)
        tryVerify(() => lista.visible && lista.activeFocus)
        compare(lista.currentIndex, 1)
        compare(fixtureExplorador.app.pagina, AppViewModel.FacturasSolicitud)
        // Un segundo Escape regresa al detalle.
        keyClick(Qt.Key_Escape)
        compare(fixtureExplorador.app.pagina, AppViewModel.Detalle)
    }

    function test_verPdfDobleClicYRegresarConElBoton() {
        const p = paginaCon(3)
        const lista = findChild(p, "listaFacturas")
        const visor = findChild(p, "visorPdfCfdi")
        const verPdf = findChild(p, "botonVerPdf")
        verify(verPdf.visible && verPdf.enabled)
        compare(verPdf.Accessible.description, "Ver el PDF de la factura seleccionada (Enter)")
        mouseClick(verPdf)
        tryVerify(() => visor.visible)
        compare(fixtureExplorador.ultimoPdfPedido(), "PAQ_01:0")
        const regresar = findChild(findChild(visor, "encabezadoVisorPdf"), "botonRegresar")
        compare(regresar.text, "Facturas")
        mouseClick(regresar)
        tryVerify(() => !visor.visible && lista.activeFocus)
        // Doble clic en la tercera fila.
        tryVerify(() => lista.itemAtIndex(2) !== null)
        mouseDoubleClickSequence(lista.itemAtIndex(2))
        tryVerify(() => visor.visible)
        compare(fixtureExplorador.ultimoPdfPedido(), "PAQ_01:2")
        compare(lista.currentIndex, 2)
        // Otra peticion: la anterior se ignora (la ultima gana).
        compare(fixtureExplorador.numGeneraciones(), 2)
        fixtureExplorador.resolverPdf(0)
        wait(50)
        verify(visor.generando)
    }

    function test_errorConMotivoYReintentar() {
        const p = paginaCon(2)
        keyClick(Qt.Key_Return)
        const visor = findChild(p, "visorPdfCfdi")
        fixtureExplorador.fallarPdf(0, "origenNoDisponible")
        const error = findChild(visor, "estadoErrorPdf")
        tryVerify(() => error.visible)
        compare(findChild(error, "errorPdf").text, "El ZIP de esta factura no está disponible.")
        verify(error.Accessible.name.indexOf("Error: ") === 0)
        verify(!findChild(visor, "vistaPdf").visible)
        const reintentar = findChild(error, "botonReintentarPdf")
        tryVerify(() => reintentar.visible && reintentar.activeFocus)
        keyClick(Qt.Key_Space)
        compare(fixtureExplorador.numGeneraciones(), 2)
        tryVerify(() => !error.visible && visor.generando)
        // Version no soportada: motivo sin "Reintentar".
        fixtureExplorador.fallarPdf(1, "versionNoSoportada")
        tryVerify(() => error.visible)
        compare(findChild(error, "errorPdf").text, "La app no representa esta versión de CFDI.")
        verify(!findChild(error, "botonReintentarPdf").visible)
        // Escape sigue cerrando el visor.
        keyClick(Qt.Key_Escape)
        tryVerify(() => !visor.visible)
    }

    function test_accionesConMensajesEnLinea() {
        const p = paginaCon(2)
        const visor = visorListo(p, 1)
        const mensaje = findChild(visor, "mensajeVisorPdf")
        verify(!mensaje.visible)
        // Vista Previa: fallo con el motivo y exito sin mensaje.
        const vistaPrevia = findChild(visor, "botonVistaPreviaPdf")
        mouseClick(vistaPrevia)
        compare(fixtureExplorador.numVistasPrevias(), 1)
        verify(!vistaPrevia.enabled)
        fixtureExplorador.resolverVistaPrevia(0, false, "No se pudo abrir Vista Previa")
        tryVerify(() => mensaje.visible)
        compare(mensaje.titulo, "No se pudo abrir Vista Previa")
        compare(mensaje.variante, "error")
        compare(mensaje.Accessible.role, Accessible.AlertMessage)
        verify(visor.documentoListo) // nada mas cambia
        mouseClick(vistaPrevia)
        verify(!mensaje.visible)
        fixtureExplorador.resolverVistaPrevia(1, true)
        tryVerify(() => vistaPrevia.enabled)
        verify(!mensaje.visible)
        // Guardar (el dialogo nativo entrega la URL): exito y fallo en linea.
        verify(findChild(visor, "botonGuardarPdf").enabled)
        fixtureExplorador.explorador.visor.guardarEn(Qt.resolvedUrl("file:///tmp/satcfdi-prueba/Factura.pdf"))
        compare(fixtureExplorador.numGuardados(), 1)
        verify(!findChild(visor, "botonGuardarPdf").enabled)
        fixtureExplorador.resolverGuardado(0, true)
        tryVerify(() => mensaje.visible && mensaje.variante === "exito")
        compare(mensaje.titulo, "PDF guardado como “Factura.pdf”.")
        fixtureExplorador.explorador.visor.guardarEn(Qt.resolvedUrl("file:///tmp/satcfdi-prueba/Factura.pdf"))
        fixtureExplorador.resolverGuardado(1, false)
        tryVerify(() => mensaje.visible && mensaje.variante === "error")
        verify(visor.documentoListo)
    }

    // Nitidez (dpr 1 en offscreen): cada pagina se rasteriza con al menos
    // 2 px/pt (hoja carta: 612 pt -> >= 1224 px), tambien tras alejar el zoom;
    // ⌘0 vuelve al ajuste al ancho (<= 125 %).
    function test_rasterizaConAlMenosDosPixelesPorPunto() {
        const p = paginaCon(1)
        const visor = visorListo(p, 2)
        compare(Screen.devicePixelRatio, 1)
        compare(visor.pxPorPunto(0.5, 1), 2)
        compare(visor.pxPorPunto(1.25, 2), 2.5)
        compare(visor.pxPorPunto(3, 2), 6)
        const vista = visor.vista
        verify(vista !== null)
        // 960 de ancho: el ajuste al ancho llega al tope de 125 %.
        tryCompare(vista, "renderScale", 1.25)
        const ajuste = vista.renderScale
        let imagen = null
        tryVerify(() => (imagen = findChild(vista, "imagenPaginaPdf_0")) !== null)
        verify(imagen.sourceSize.width >= 2 * 612)
        compare(imagen.width, 612 * ajuste)
        visor.forceActiveFocus()
        keyClick(Qt.Key_Minus, Qt.ControlModifier)
        keyClick(Qt.Key_Minus, Qt.ControlModifier)
        verify(vista.renderScale < ajuste)
        tryVerify(() => imagen.sourceSize.width >= 2 * 612)
        keyClick(Qt.Key_Equal, Qt.ControlModifier | Qt.ShiftModifier)
        keyClick(Qt.Key_0, Qt.ControlModifier)
        tryCompare(vista, "renderScale", ajuste)
        tryVerify(() => imagen.status === Image.Ready, 5000)
    }

    // Cambiar de PDF, cerrar y destruir la pagina mientras QtQuick.Pdf aun
    // renderiza no debe dejar el hilo de render con un documento destruido
    // (documento por PDF con retiro diferido).
    function test_cerrarYCambiarMientrasRenderiza() {
        const p = paginaCon(3)
        const visor = findChild(p, "visorPdfCfdi")
        for (let i = 0; i < 3; ++i) {
            keyClick(Qt.Key_Return)
            fixtureExplorador.resolverPdf(fixtureExplorador.numGeneraciones() - 1, 3)
            tryVerify(() => visor.documentoListo, 5000)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !visor.visible)
            keyClick(Qt.Key_Down)
        }
        keyClick(Qt.Key_Return)
        fixtureExplorador.resolverPdf(fixtureExplorador.numGeneraciones() - 1, 3)
        tryVerify(() => visor.documentoListo, 5000)
        p.destroy()
        wait(500)
    }

    function test_temaOscuro() {
        Theme.oscuro = true
        const p = paginaCon(1)
        const visor = visorListo(p, 1)
        verify(Qt.colorEqual(Theme.superficie, "#252527"))
        compare(visor.children[0].color, Theme.superficie)
        verify(findChild(visor, "vistaPdf").visible)
    }
}
