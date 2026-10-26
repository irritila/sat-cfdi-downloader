import QtQuick
import QtTest
import SatCfdiDownloader

// T026.3 (corte 2): exportacion desde Facturas sobre el
// ExportacionFacturasViewModel real y el ExportacionFacturasService real
// (FixtureFacturas.habilitarExportacion: ZIP sintetico de 2 facturas, destino
// en memoria, lector con compuerta). "Exportar este mes", modo de seleccion
// con casilla por fila, DialogoExportarFacturas (aviso D10, alcance, rango,
// direccion, PDF, conteo, carpeta y nombre, progreso, EsperandoNombre y
// resultado): foco inicial, Tab, Escape por estado, nombres accesibles y
// vocabulario. Carpeta sintetica /Users/usuario/Documentos (el FolderDialog
// nativo no se usa en offscreen). Con SATCFDI_CAPTURAS guarda capturas.
TestCase {
    id: caso
    name: "ExportarFacturas"
    when: windowShown
    visible: true
    width: 960
    height: 760

    readonly property string carpeta: "file:///Users/usuario/Documentos"

    Component {
        id: paginaComp
        FacturasPage {
            app: fixtureFacturas.app
            facturas: fixtureFacturas.facturas
            exportacion: fixtureFacturas.exportacion
        }
    }

    function init() {
        failOnWarning(/^(?!Populating font family aliases took).*/)
        wait(0)
        Theme.oscuro = false
    }

    function cleanup() {
        fixtureFacturas.liberarCompuerta()
        Theme.oscuro = false
    }

    function pagina() {
        fixtureFacturas.reiniciar(true)
        fixtureFacturas.fijarEstadoMes(9, "Disponibles")
        fixtureFacturas.habilitarExportacion()
        fixtureFacturas.app.mostrarFacturas()
        const p = createTemporaryObject(paginaComp, caso, { width: caso.width, height: caso.height })
        verify(p)
        tryCompare(fixtureFacturas.facturas, "mes", 9)
        const lista = findChild(p, "listaFacturas")
        tryVerify(() => lista.mostrar && lista.count > 0)
        tryVerify(() => fixtureFacturas.exportacion.puedeExportar)
        wait(0)
        return p
    }

    function abrirMes(p) {
        clic(findChild(p, "botonExportarMes"))
        const d = p.dialogoExportar
        tryVerify(() => d.opened)
        tryCompare(fixtureFacturas.exportacion, "estado", ExportacionFacturasViewModel.Dialogo)
        return d
    }

    // Dos clics seguidos dentro del intervalo de doble clic llegan como doble
    // clic y el boton no emite clicked(): se espera ese intervalo antes.
    function clic(item) {
        wait(Qt.styleHints.mouseDoubleClickInterval + 20)
        mouseClick(item)
    }

    function hijo(d, nombre) {
        const item = findChild(d.contentItem, nombre)
        verify(item, nombre)
        return item
    }

    function foco() {
        return caso.Window.window.activeFocusItem
    }

    function capturar(nombre) {
        const dir = fixtureFacturas.directorioCapturas()
        if (dir.length > 0) {
            waitForRendering(caso)
            grabImage(caso).save(dir + "/" + nombre + (Theme.oscuro ? "-oscuro" : "-claro") + ".png")
        }
    }

    function textosVisibles(raiz) {
        const r = []
        const visitar = (item) => {
            if (!item || item.visible === false || item.opacity === 0)
                return
            for (const prop of ["text", "displayText", "placeholderText", "titulo", "descripcion", "texto", "title"]) {
                const v = item[prop]
                if (typeof v === "string" && v.length > 0)
                    r.push(v)
            }
            if (item.Accessible) {
                if (item.Accessible.name && item.Accessible.name.length > 0)
                    r.push(item.Accessible.name)
                if (item.Accessible.description && item.Accessible.description.length > 0)
                    r.push(item.Accessible.description)
            }
            for (let i = 0; i < item.children.length; ++i)
                visitar(item.children[i])
        }
        visitar(raiz)
        return r
    }

    function sinVocabularioSat(raiz, contexto) {
        const textos = textosVisibles(raiz)
        verify(textos.length > 3, contexto + ": se recorrieron textos")
        for (const t of textos) {
            const bajo = t.toLowerCase()
            for (const prohibido of ["solicitud", "paquete", "verificaci"])
                verify(bajo.indexOf(prohibido) < 0, contexto + ": \"" + t + "\" contiene \"" + prohibido + "\"")
        }
    }

    // ---- Dialogo: aviso, conteo, foco inicial, Tab, Escape ----

    function test_exportarEsteMesFocoTabYEscape() {
        const p = pagina()
        const e = fixtureFacturas.exportacion
        const boton = findChild(p, "botonExportarMes")
        compare(boton.text, "Exportar este mes")
        verify(boton.Accessible.description.indexOf("Septiembre 2026") >= 0)
        const d = abrirMes(p)
        verify(p.dialogoAbierto)
        compare(d.title, "Exportar facturas")
        // Aviso D10 siempre visible, texto fijo.
        const aviso = hijo(d, "avisoExportar")
        verify(aviso.visible)
        compare(aviso.descripcion, e.aviso)
        verify(aviso.descripcion.indexOf("no está protegido con contraseña") > 0)
        compare(hijo(d, "conteoExportar").text, "Se exportarán 2 facturas.")
        compare(hijo(d, "alcanceExportar").text, "Septiembre 2026")
        verify(hijo(d, "selectorDireccionExportar").visible)
        compare(hijo(d, "campoNombreExportar").text, "Archivo-Fiscal-2026-09.zip")
        compare(hijo(d, "carpetaExportar").text, "Ninguna carpeta elegida")
        verify(!hijo(d, "botonExportar").enabled) // falta carpeta
        // Foco inicial: el alcance (segmento activo).
        tryVerify(() => hijo(d, "segmento_modoMes").activeFocus)
        // Carpeta: solo su nombre.
        e.elegirDestino(caso.carpeta)
        compare(hijo(d, "carpetaExportar").text, "Documentos")
        compare(hijo(d, "carpetaExportar").Accessible.name, "Carpeta: Documentos")
        verify(hijo(d, "botonExportar").enabled)
        capturar("exportar-dialogo")
        // Orden de Tab: alcance, direccion, PDF, carpeta, nombre, Cancelar, Exportar.
        const orden = ["segmento_expEmitidas", "casillaPdfExportar", "botonElegirCarpetaExportar",
                       "campoNombreExportar", "botonCancelarExportar", "botonExportar"]
        for (const nombre of orden) {
            keyClick(Qt.Key_Tab)
            compare(foco().objectName, nombre, "Tab tras " + nombre)
        }
        // Nombres accesibles.
        compare(hijo(d, "casillaPdfExportar").Accessible.name, "Incluir el PDF de cada factura")
        compare(hijo(d, "campoNombreExportar").Accessible.name, "Nombre del archivo")
        compare(hijo(d, "selectorModoExportar").Accessible.name, "Qué exportar")
        compare(hijo(d, "selectorDireccionExportar").Accessible.name, "Facturas a exportar")
        sinVocabularioSat(d.contentItem, "dialogo")
        // Escape en Dialogo cierra.
        keyClick(Qt.Key_Escape)
        tryCompare(e, "estado", ExportacionFacturasViewModel.Inactivo)
        tryVerify(() => !d.visible)
    }

    function test_conteoCeroDeshabilitaExportar() {
        const p = pagina()
        fixtureFacturas.fijarConteoPlan(0)
        const d = abrirMes(p)
        fixtureFacturas.exportacion.elegirDestino(caso.carpeta)
        compare(hijo(d, "conteoExportar").text, "No hay facturas para exportar")
        const exportar = hijo(d, "botonExportar")
        verify(!exportar.enabled)
        compare(exportar.Accessible.description, "No hay facturas para exportar")
        d.close()
        tryCompare(fixtureFacturas.exportacion, "estado", ExportacionFacturasViewModel.Inactivo)
    }

    function test_rangoYDireccionSoloParaMesORango() {
        const p = pagina()
        const e = fixtureFacturas.exportacion
        const d = abrirMes(p)
        clic(hijo(d, "segmento_modoRango"))
        tryCompare(e, "modo", ExportacionFacturasViewModel.Rango)
        tryCompare(e, "estado", ExportacionFacturasViewModel.Dialogo)
        verify(hijo(d, "rangoExportar").visible)
        compare(hijo(d, "comboDesdeExportar").displayText, "Septiembre 2026")
        compare(hijo(d, "comboHastaExportar").displayText, "Septiembre 2026")
        compare(hijo(d, "comboDesdeExportar").Accessible.name, "Desde el mes")
        verify(!hijo(d, "errorRangoExportar").visible)
        // Invertido: error visible y sin plan.
        e.fijarRango(2026, 9, 2026, 8)
        tryVerify(() => hijo(d, "errorRangoExportar").visible)
        compare(hijo(d, "errorRangoExportar").text, "El mes inicial debe ser anterior o igual al mes final.")
        verify(!hijo(d, "botonExportar").enabled)
        capturar("exportar-rango-error")
        e.fijarRango(2026, 8, 2026, 9)
        tryVerify(() => !hijo(d, "errorRangoExportar").visible)
        compare(hijo(d, "campoNombreExportar").text, "Archivo-Fiscal-2026-08_2026-09.zip")
        verify(hijo(d, "selectorDireccionExportar").visible)
        // Elegidas: sin selector de direccion (D8).
        clic(hijo(d, "segmento_modoSeleccion"))
        tryCompare(e, "modo", ExportacionFacturasViewModel.Seleccion)
        verify(!hijo(d, "selectorDireccionExportar").visible)
        sinVocabularioSat(d.contentItem, "rango")
        d.close()
    }

    // ---- Exportando: progreso y Escape cancela ----

    function test_exportandoEscapeCancela() {
        const p = pagina()
        const e = fixtureFacturas.exportacion
        const d = abrirMes(p)
        e.elegirDestino(caso.carpeta)
        fixtureFacturas.cerrarCompuerta()
        clic(hijo(d, "botonExportar"))
        tryVerify(() => fixtureFacturas.compuertaBloqueada())
        compare(e.estado, ExportacionFacturasViewModel.Exportando)
        verify(hijo(d, "progresoExportar").visible)
        verify(!hijo(d, "configuracionExportar").visible)
        verify(hijo(d, "avisoExportar").visible)
        compare(hijo(d, "barraExportar").Accessible.name, "Progreso de la exportación")
        tryVerify(() => hijo(d, "botonCancelarExportacion").activeFocus)
        compare(hijo(d, "botonCancelarExportacion").text, "Cancelar exportación")
        capturar("exportar-progreso")
        // Escape no cierra: cancela.
        keyClick(Qt.Key_Escape)
        verify(d.visible)
        fixtureFacturas.liberarCompuerta()
        tryCompare(e, "estado", ExportacionFacturasViewModel.Resultado)
        compare(hijo(d, "resultadoExportar").titulo, "Exportación cancelada")
        verify(!hijo(d, "archivoCreadoExportar").visible)
        compare(fixtureFacturas.parcialesVivos(), 0)
        compare(fixtureFacturas.archivosEnCarpeta("/Users/usuario/Documentos"), 0)
        tryVerify(() => hijo(d, "botonCerrarExportar").activeFocus)
        // Escape en Resultado cierra.
        keyClick(Qt.Key_Escape)
        tryCompare(e, "estado", ExportacionFacturasViewModel.Inactivo)
    }

    // ---- EsperandoNombre y resultado ----

    function test_esperandoNombreYResultado() {
        const p = pagina()
        const e = fixtureFacturas.exportacion
        let d = abrirMes(p)
        e.elegirDestino(caso.carpeta)
        fixtureFacturas.colisionAlPublicar()
        clic(hijo(d, "botonExportar"))
        tryCompare(e, "estado", ExportacionFacturasViewModel.EsperandoNombre)
        verify(hijo(d, "errorDestinoExportar").visible)
        compare(hijo(d, "errorDestinoExportar").descripcion,
                "Ya existe un archivo con ese nombre en la carpeta. Escribe otro nombre.")
        verify(hijo(d, "botonGuardarConNombre").visible)
        verify(hijo(d, "botonCancelarExportacion").visible)
        tryVerify(() => hijo(d, "campoNombreExportar").activeFocus)
        compare(fixtureFacturas.parcialesVivos(), 1)
        capturar("exportar-esperando-nombre")
        // Otro nombre y guardar el mismo archivo preparado.
        const campo = hijo(d, "campoNombreExportar")
        campo.selectAll()
        keyClick(Qt.Key_Delete)
        for (const c of "Facturas sep")
            keyClick(c)
        compare(e.nombreZip, "Facturas sep")
        clic(hijo(d, "botonGuardarConNombre"))
        tryCompare(e, "estado", ExportacionFacturasViewModel.Resultado)
        compare(hijo(d, "resultadoExportar").titulo, "Exportación terminada")
        compare(hijo(d, "resultadoExportar").descripcion, "Se exportaron 2 facturas.")
        verify(hijo(d, "archivoCreadoExportar").visible)
        compare(hijo(d, "archivoCreadoExportar").text, "Archivo creado: Facturas sep.zip")
        compare(fixtureFacturas.parcialesVivos(), 0)
        capturar("exportar-resultado")
        sinVocabularioSat(d.contentItem, "resultado")
        clic(hijo(d, "botonCerrarExportar"))
        tryCompare(e, "estado", ExportacionFacturasViewModel.Inactivo)
        verify(e.nombrePublicado.length === 0)

        // EsperandoNombre + Escape: cancela y descarta el archivo preparado.
        d = abrirMes(p)
        e.nombreZip = "Otro"
        fixtureFacturas.colisionAlPublicar()
        clic(hijo(d, "botonExportar"))
        tryCompare(e, "estado", ExportacionFacturasViewModel.EsperandoNombre)
        keyClick(Qt.Key_Escape)
        tryCompare(e, "estado", ExportacionFacturasViewModel.Resultado)
        compare(hijo(d, "resultadoExportar").titulo, "Exportación cancelada")
        compare(fixtureFacturas.parcialesVivos(), 0)
        d.close()
    }

    // ---- Modo de seleccion ----

    function test_seleccionConCasillasYEspacio() {
        const p = pagina()
        const e = fixtureFacturas.exportacion
        const lista = findChild(p, "listaFacturas")
        verify(!findChild(p, "barraSeleccionFacturas").visible)
        verify(!findChild(p, "casillaFactura_0").visible)
        clic(findChild(p, "botonElegirFacturas"))
        verify(e.seleccionando)
        verify(findChild(p, "barraSeleccionFacturas").visible)
        verify(!findChild(p, "barraExportarFacturas").visible)
        const casilla = findChild(p, "casillaFactura_0")
        verify(casilla.visible)
        verify(casilla.Accessible.name.indexOf("Elegir factura: ") === 0)
        verify(!casilla.checked)
        verify(!findChild(p, "botonExportarElegidas").enabled)
        // Espacio alterna la fila actual.
        tryVerify(() => lista.activeFocus)
        lista.currentIndex = 0
        keyClick(Qt.Key_Space)
        tryVerify(() => casilla.checked)
        compare(findChild(p, "conteoSeleccionFacturas").text, "1 factura elegida")
        verify(findChild(p, "filaFactura_0").Accessible.name.endsWith(", elegida"))
        // Clic en la casilla de otra fila.
        clic(findChild(p, "casillaFactura_1"))
        tryCompare(e, "conteoSeleccionadas", 2)
        // Pestana y vuelta: lo elegido se conserva.
        fixtureFacturas.facturas.direccion = FacturasViewModel.Recibidas
        tryVerify(() => lista.count === fixtureFacturas.facturas.conteoRecibidas)
        fixtureFacturas.facturas.direccion = FacturasViewModel.Emitidas
        tryVerify(() => lista.count === fixtureFacturas.facturas.conteoEmitidas)
        tryVerify(() => {
            const c0 = findChild(p, "casillaFactura_0")
            const c1 = findChild(p, "casillaFactura_1")
            return !!c0 && !!c1 && c0.checked && c1.checked
        })
        capturar("exportar-seleccion")
        sinVocabularioSat(p, "seleccion")
        // Exportar elegidas: sin selector de direccion.
        clic(findChild(p, "botonExportarElegidas"))
        const d = p.dialogoExportar
        tryVerify(() => d.opened)
        tryCompare(e, "estado", ExportacionFacturasViewModel.Dialogo)
        compare(e.modo, ExportacionFacturasViewModel.Seleccion)
        verify(!hijo(d, "selectorDireccionExportar").visible)
        compare(hijo(d, "alcanceExportar").text, "2 facturas elegidas")
        d.close()
        tryCompare(e, "estado", ExportacionFacturasViewModel.Inactivo)
        // Escape en la pagina sale del modo de seleccion.
        lista.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Escape)
        verify(!e.seleccionando)
        compare(e.conteoSeleccionadas, 0)
        verify(!findChild(p, "casillaFactura_0").visible)
    }

    function test_sinServicioBotonesDeshabilitados() {
        fixtureFacturas.reiniciar(true)
        fixtureFacturas.app.mostrarFacturas()
        const p = createTemporaryObject(paginaComp, caso, { width: caso.width, height: caso.height })
        tryCompare(fixtureFacturas.facturas, "mes", 9)
        tryVerify(() => findChild(p, "botonExportarMes").visible)
        verify(!findChild(p, "botonExportarMes").enabled)
        verify(!findChild(p, "botonElegirFacturas").enabled)
    }
}
