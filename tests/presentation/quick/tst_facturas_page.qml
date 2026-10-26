import QtQuick
import QtTest
import SatCfdiDownloader

// Pantalla Facturas (T023.4) sobre el FacturasViewModel real y una fuente
// sintetica (FixtureFacturas): estados de la franja con su accion, lista con
// solo la contraparte, teclado (Tab, flechas, Enter, ⌘C, anillo de foco),
// e.firma no lista, guia sin perfiles, y anchos 960x640 y minimo (560x420)
// en claro y oscuro. Con SATCFDI_CAPTURAS guarda una captura por caso.
TestCase {
    id: caso
    name: "FacturasPage"
    when: windowShown
    visible: true
    width: 960
    height: 640

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
        // T024.1: barrido de vocabulario del estado final (Diagnostico excluido).
        const fallasVocabulario = barrido.revisarVentana(caso)
        verify(fallasVocabulario.length === 0, fallasVocabulario.join("\n"))
        Theme.oscuro = false
    }

    // Pagina de `ancho` x `alto` con septiembre en `estado` (perfiles de ejemplo).
    function paginaCon(estado, ancho, alto, conPerfiles) {
        fixtureFacturas.reiniciar(conPerfiles === undefined ? true : conPerfiles)
        if (estado.length > 0)
            fixtureFacturas.fijarEstadoMes(9, estado)
        fixtureFacturas.app.mostrarFacturas()
        const p = createTemporaryObject(paginaComp, caso, { width: ancho || caso.width, height: alto || caso.height })
        verify(p)
        if (conPerfiles === false) {
            tryVerify(() => findChild(p, "guiaPrimerUsoFacturas").visible)
        } else {
            tryCompare(fixtureFacturas.facturas, "mes", 9)
            tryVerify(() => fixtureFacturas.facturas.franja.estado !== FranjaMesViewModel.Cargando)
        }
        wait(0)
        return p
    }

    function capturar(p, nombre) {
        const dir = fixtureFacturas.directorioCapturas()
        if (dir.length > 0) {
            waitForRendering(p)
            grabImage(p).save(dir + "/" + nombre + (Theme.oscuro ? "-oscuro" : "-claro") + ".png")
        }
    }

    // Ningun hijo visible sobresale horizontalmente de la pagina.
    function sinDesbordeHorizontal(p, nombres) {
        for (const nombre of nombres) {
            const item = findChild(p, nombre)
            if (!item || !item.visible)
                continue
            const pos = item.mapToItem(p, 0, 0)
            verify(pos.x >= 0, nombre + " empieza dentro de la pagina")
            verify(pos.x + item.width <= p.width + 0.5, nombre + " termina dentro de la pagina (" + (pos.x + item.width) + " > " + p.width + ")")
        }
    }

    // ---- Seleccion inicial y lista ----

    function test_mesInicialYListaConContraparte() {
        const p = paginaCon("Disponibles")
        const f = fixtureFacturas.facturas
        compare(f.anio, 2026)
        compare(f.mes, 9) // octubre en curso: septiembre por omision
        compare(f.perfilRfc, "XAXX010101000")
        const lista = findChild(p, "listaFacturas")
        tryVerify(() => lista.mostrar && lista.count > 0)
        compare(lista.count, f.conteoEmitidas)
        // D20: solo la contraparte; Emitidas = Receptor.
        compare(findChild(p, "encabezadoContraparte").text, f.encabezadoContraparte)
        const fila = findChild(p, "filaFactura_0")
        verify(fila)
        verify(!fila.mostrarReceptor)
        compare(fila.emisorRfc, "XEXX010101000")
        // Total formateado y segunda linea "MXN · PUE" (detalleTotal del modelo).
        compare(findChild(fila, "totalCfdi").text, f.modelo.data(f.modelo.index(0, 0), FacturasListModel.TotalRole))
        compare(findChild(fila, "monedaCfdi").text, f.modelo.data(f.modelo.index(0, 0), FacturasListModel.DetalleTotalRole))
        verify(findChild(fila, "monedaCfdi").text.indexOf("PUE") > 0)
        compare(fila.Accessible.name, f.modelo.data(f.modelo.index(0, 0), FacturasListModel.NombreAccesibleRole))
        // Recibidas: cambia la columna y la lista.
        f.direccion = FacturasViewModel.Recibidas
        tryCompare(findChild(p, "encabezadoContraparte"), "text", f.encabezadoContraparte)
        tryCompare(lista, "count", f.conteoRecibidas)
        // Meses: 12, futuros no seleccionables, nombre accesible del modelo.
        const meses = findChild(p, "listaMeses")
        compare(meses.count, 12)
        verify(!findChild(p, "mes_11").enabled)
        verify(findChild(p, "mes_8").enabled)
        verify(findChild(p, "mes_9").Accessible.name.length > 0)
        capturar(p, "05-disponibles-960")
    }

    // ---- Estados de la franja ----

    function test_estadosDeLaFranja_data() {
        const estados = [
            { estado: "SinDescargar", accion: true, habilitada: true, enlace: false, ocupado: false },
            { estado: "Preparando", accion: true, habilitada: false, enlace: false, ocupado: true },
            { estado: "Disponibles", accion: false, habilitada: false, enlace: false, ocupado: false },
            { estado: "Parcial", accion: true, habilitada: true, enlace: false, ocupado: false },
            { estado: "Atencion", accion: true, habilitada: true, enlace: true, ocupado: false }
        ]
        const r = []
        for (const e of estados) {
            for (const oscuro of [false, true])
                r.push(Object.assign({ tag: e.estado + (oscuro ? "-oscuro" : "-claro"), oscuro: oscuro }, e))
        }
        return r
    }

    function test_estadosDeLaFranja(d) {
        Theme.oscuro = d.oscuro
        const p = paginaCon(d.estado)
        const franja = fixtureFacturas.facturas.franja
        const boton = findChild(p, "botonAccionFranja")
        compare(boton.visible, d.accion)
        if (d.accion) {
            compare(boton.text, franja.textoAccion)
            compare(boton.enabled, d.habilitada)
            // Deshabilitada: motivo visible y en la descripcion accesible.
            compare(findChild(p, "motivoFranja").visible, !d.habilitada)
            if (!d.habilitada)
                compare(boton.Accessible.description.indexOf(franja.motivoDeshabilitada), 0)
        }
        compare(findChild(p, "titularFranja").text, franja.titular)
        compare(findChild(p, "badgeFranja").texto, franja.estadoTexto)
        compare(findChild(p, "etapaFranja").visible, d.ocupado)
        if (d.ocupado) {
            compare(findChild(p, "notaFranja").text, franja.nota)
            // La etapa no se repite en la descripcion.
            verify(!findChild(p, "descripcionFranja").visible)
        }
        const enlace = findChild(p, "enlaceDetallesTecnicos")
        compare(enlace.visible, d.enlace)
        if (d.enlace) {
            // Variante enlace: sin padding, alineada con el texto de la franja.
            const xEnlace = enlace.mapToItem(p, 0, 0).x
            const xTexto = findChild(p, "descripcionFranja").mapToItem(p, 0, 0).x
            compare(Math.round(xEnlace), Math.round(xTexto))
        }
        // Cmd+R (la funcion que usa Main.qml): solo con la accion habilitada.
        const antes = fixtureFacturas.numPlanificaciones()
        p.ejecutarAccion()
        if (d.accion && d.habilitada && franja.accion !== FranjaMesViewModel.VerDetalles)
            tryVerify(() => fixtureFacturas.numPlanificaciones() === antes + 1)
        else
            compare(fixtureFacturas.numPlanificaciones(), antes)
        capturar(p, "franja-" + d.estado.toLowerCase() + "-960")
    }

    // ---- Teclado y foco ----

    function test_tecladoTabFlechasEnterYCopiar() {
        const p = paginaCon("Disponibles")
        fixtureFacturas.habilitarExportacion()
        const lista = findChild(p, "listaFacturas")
        tryVerify(() => lista.activeFocus && lista.currentIndex === 0)
        // Foco inicial sin anillo en la primera fila.
        verify(!lista.focoVisible)
        verify(!findChild(p, "filaFactura_0").conFoco)
        keyClick(Qt.Key_Down)
        compare(lista.currentIndex, 1)
        verify(lista.focoVisible)
        verify(findChild(p, "filaFactura_1").conFoco)
        keyClick(Qt.Key_Up)
        compare(lista.currentIndex, 0)
        // ⌘C copia el UUID completo de la fila.
        verify(p.copiarUuidActual())
        // Orden de Tab: perfil, meses, accion (si hay), pestanas, busqueda, filtro, lista.
        const combo = findChild(p, "comboPerfilFacturas")
        combo.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Tab)
        verify(findChild(p, "botonHistorialFacturas").activeFocus, "Historial tras el perfil")
        keyClick(Qt.Key_Tab)
        verify(findChild(p, "botonPerfilesFacturas").activeFocus, "Perfiles SAT tras Historial")
        // Selector de anio (chevrones habilitados) y luego los meses.
        const intermedios = []
        for (let i = 0; i < 3 && !findChild(p, "listaMeses").activeFocus; ++i) {
            keyClick(Qt.Key_Tab)
            if (!findChild(p, "listaMeses").activeFocus)
                intermedios.push(caso.Window.window.activeFocusItem.objectName)
        }
        verify(findChild(p, "listaMeses").activeFocus, "meses tras el encabezado y el anio: " + intermedios)
        for (const nombre of intermedios)
            verify(nombre === "botonAnioAnterior" || nombre === "botonAnioSiguiente", nombre)
        keyClick(Qt.Key_Tab)
        verify(findChild(p, "segmento_emitidas").activeFocus, "pestanas tras los meses (Disponibles no tiene accion)")
        keyClick(Qt.Key_Tab)
        verify(findChild(p, "campoBusquedaFacturas").activeFocus, "busqueda")
        keyClick(Qt.Key_Tab)
        verify(findChild(p, "filtroTipoFacturas").activeFocus, "filtro")
        // T026.3: exportar y elegir facturas antes de la lista.
        keyClick(Qt.Key_Tab)
        verify(findChild(p, "botonExportarMes").activeFocus, "Exportar este mes")
        keyClick(Qt.Key_Tab)
        verify(findChild(p, "botonElegirFacturas").activeFocus, "Elegir facturas")
        keyClick(Qt.Key_Tab)
        verify(lista.activeFocus, "lista")
        // Entrar con Tab dibuja el anillo.
        verify(lista.focoVisible)
        // Flechas en los meses piden el mes anterior/siguiente al view model.
        findChild(p, "listaMeses").forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Up)
        tryCompare(fixtureFacturas.facturas, "mes", 8)
        keyClick(Qt.Key_Down)
        tryCompare(fixtureFacturas.facturas, "mes", 9)
        // ⌘↑/⌘↓ (funcion de Main.qml).
        verify(p.moverMes(-1))
        tryCompare(fixtureFacturas.facturas, "mes", 8)
        // ⌘F enfoca la busqueda; Escape la vacia.
        verify(p.enfocarBusqueda())
        keyClick(Qt.Key_A)
        tryCompare(fixtureFacturas.facturas, "texto", "a")
        keyClick(Qt.Key_Escape)
        tryCompare(fixtureFacturas.facturas, "texto", "")
    }

    function test_tabIncluyeLaAccionDeLaFranja() {
        const p = paginaCon("SinDescargar")
        findChild(p, "listaMeses").forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Tab)
        verify(findChild(p, "botonAccionFranja").activeFocus)
        keyClick(Qt.Key_Tab)
        verify(findChild(p, "segmento_emitidas").activeFocus)
    }

    function test_sinCoincidenciasYLimpiarFiltros() {
        const p = paginaCon("Disponibles")
        fixtureFacturas.facturas.texto = "zzzz-no-existe"
        tryCompare(fixtureFacturas.facturas, "vista", "sinCoincidencias")
        const vacio = findChild(p, "estadoSinCoincidenciasFacturas")
        verify(vacio.visible)
        mouseClick(findChild(p, "botonLimpiarFiltrosVacioFacturas"))
        tryCompare(fixtureFacturas.facturas, "texto", "")
        tryCompare(fixtureFacturas.facturas, "vista", "lista")
        capturar(p, "07-sin-coincidencias-960")
    }

    // ---- e.firma no lista ----

    function test_perfilSinEFirma() {
        const p = paginaCon("SinDescargar")
        fixtureFacturas.facturas.seleccionarPerfil(1)
        tryCompare(fixtureFacturas.facturas, "eFirmaLista", false)
        tryVerify(() => findChild(p, "avisoEFirmaFacturas").visible)
        const boton = findChild(p, "botonAccionFranja")
        tryCompare(fixtureFacturas.facturas.franja, "accion", FranjaMesViewModel.RegistrarEFirma)
        verify(boton.enabled)
        capturar(p, "02-sin-efirma-960")
        mouseClick(boton)
        tryCompare(fixtureFacturas.app, "pagina", AppViewModel.Perfiles)
    }

    // ---- Guia de primer uso ----

    function test_guiaSinPerfiles() {
        const p = paginaCon("", 0, 0, false)
        verify(findChild(p, "guiaPrimerUsoFacturas").visible)
        verify(findChild(p, "pasoFacturas"))
        compare(findChild(p, "pasoFacturas").Accessible.name.indexOf("Descarga las facturas de un mes") >= 0, true)
        verify(!findChild(p, "listaMeses").visible)
        verify(!findChild(p, "comboPerfilFacturas").visible)
        capturar(p, "01-sin-perfiles-960")
        mouseClick(findChild(p, "boton_pasoPerfil"))
        tryCompare(fixtureFacturas.app, "pagina", AppViewModel.Perfiles)
    }

    // ---- Vocabulario (T023 D10): fuera de Diagnostico sin terminos del SAT ----

    // Textos visibles y accesibles de los items visibles bajo `raiz`.
    function textosVisibles(raiz) {
        const r = []
        const visitar = (item) => {
            if (!item || item.visible === false || item.opacity === 0)
                return
            for (const prop of ["text", "displayText", "placeholderText", "titulo", "descripcion", "texto"]) {
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

    function sinVocabularioSat(p, contexto) {
        // T024.1: barrido comun (normaliza acentos y plurales, inventario de
        // excepciones) sobre toda la ventana, incluidos items ocultos.
        const fallas = barrido.revisarVentana(p)
        verify(fallas.length === 0, contexto + ":\n" + fallas.join("\n"))
        const textos = textosVisibles(p)
        verify(textos.length > 10, contexto + ": se recorrieron textos")
        for (const t of textos) {
            const bajo = t.toLowerCase()
            for (const prohibido of ["solicitud", "paquete", "verificaci"])
                verify(bajo.indexOf(prohibido) < 0, contexto + ": \"" + t + "\" contiene \"" + prohibido + "\"")
        }
    }

    function test_vocabularioSinTerminosSat_data() {
        return ["SinDescargar", "Preparando", "Disponibles", "Parcial", "Atencion"].map(e => ({ tag: e, estado: e }))
    }

    function test_vocabularioSinTerminosSat(d) {
        const p = paginaCon(d.estado)
        sinVocabularioSat(p, d.estado + " emitidas")
        // Deja terminar la creacion diferida de delegados (cacheBuffer)
        // antes de vaciar la lista.
        waitForRendering(p)
        wait(50)
        fixtureFacturas.facturas.direccion = FacturasViewModel.Recibidas
        wait(0)
        sinVocabularioSat(p, d.estado + " recibidas")
        fixtureFacturas.facturas.texto = "zzzz-no-existe"
        tryCompare(fixtureFacturas.facturas, "vista", "sinCoincidencias")
        sinVocabularioSat(p, d.estado + " sin coincidencias")
    }

    function test_vocabularioSinEFirmaYSinPerfiles() {
        let p = paginaCon("SinDescargar")
        fixtureFacturas.facturas.seleccionarPerfil(1)
        tryCompare(fixtureFacturas.facturas, "eFirmaLista", false)
        tryVerify(() => findChild(p, "avisoEFirmaFacturas").visible)
        sinVocabularioSat(p, "sin e.firma")
        p = paginaCon("", 560, 420, false)
        sinVocabularioSat(p, "sin perfiles")
    }

    // ---- Anchos 960x640 y minimo en claro y oscuro ----

    function test_anchos_data() {
        const r = []
        for (const t of [{ ancho: 960, alto: 640 }, { ancho: 560, alto: 420 }]) {
            for (const estado of ["Disponibles", "Atencion", "Preparando"]) {
                for (const oscuro of [false, true])
                    r.push({ tag: estado + "-" + t.ancho + (oscuro ? "-oscuro" : "-claro"), ancho: t.ancho, alto: t.alto,
                             estado: estado, oscuro: oscuro })
            }
        }
        return r
    }

    function test_anchos(d) {
        Theme.oscuro = d.oscuro
        const p = paginaCon(d.estado, d.ancho, d.alto)
        const angosto = d.ancho < 760
        compare(findChild(p, "listaMesesFacturas").visible, !angosto)
        compare(findChild(p, "selectorMesAngosto").visible, angosto)
        sinDesbordeHorizontal(p, ["franjaMes", "barraFiltrosFacturas", "contenedorListaFacturas", "selectorMesAngosto",
                                  "botonAccionFranja", "filtroTipoFacturas", "campoBusquedaFacturas"])
        // Filas de meses: alto derivado del contenido + padding (sin recortes).
        if (!angosto) {
            const mes = findChild(p, "mes_9")
            const estado = findChild(mes, "estadoMes")
            const abajo = estado.mapToItem(mes, 0, estado.height).y
            verify(abajo <= mes.height - Theme.espacioS + 0.5, "la linea de estado deja padding inferior")
        }
        // Filas de la lista: "Completo" alineado con la segunda linea.
        const fila = findChild(p, "filaFactura_0")
        if (fila && fila.visible) {
            const moneda = findChild(fila, "monedaCfdi")
            const parseo = findChild(fila, "estadoParseoCfdi")
            compare(Math.round(parseo.mapToItem(fila, 0, 0).y), Math.round(moneda.mapToItem(fila, 0, 0).y))
        }
        capturar(p, "facturas-" + d.estado.toLowerCase() + "-" + d.ancho)
    }
}
