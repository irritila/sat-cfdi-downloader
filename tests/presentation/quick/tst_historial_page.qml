import QtQuick
import QtTest
import SatCfdiDownloader

// Historial (T023.5) sobre HistorialViewModel real y una FuenteHistorial
// sintetica (FixtureFacturas): filas mixtas con sus acciones (incluye
// "Cubierta por otra descarga" sin Reintentar y "Eliminada" solo con detalles
// tecnicos), vocabulario sin terminos del SAT, teclado (foco sin anillo,
// flechas, Tab a los botones de la fila, Enter), eleccion de direccion para
// Ver detalles tecnicos con "Atras" de vuelta, estados vacio y error, y anchos
// 960x640 y minimo en claro y oscuro. Con SATCFDI_CAPTURAS guarda capturas.
TestCase {
    id: caso
    name: "HistorialPage"
    when: windowShown
    visible: true
    width: 960
    height: 640

    Component {
        id: paginaComp
        HistorialPage {
            app: fixtureFacturas.app
            historial: fixtureFacturas.historial
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

    function paginaCon(caso_, ancho, alto) {
        fixtureFacturas.reiniciar(true)
        tryVerify(() => fixtureFacturas.facturas.perfilRfc === "XAXX010101000")
        if (caso_ !== undefined && caso_ !== "mixto")
            fixtureFacturas.fijarHistorial(caso_)
        fixtureFacturas.app.mostrarHistorial()
        tryCompare(fixtureFacturas.app, "pagina", AppViewModel.Historial)
        const p = createTemporaryObject(paginaComp, caso, { width: ancho || caso.width, height: alto || caso.height })
        verify(p)
        tryVerify(() => fixtureFacturas.historial.vista !== "cargando")
        wait(0)
        return p
    }

    function rol(fila, nombre) {
        const m = fixtureFacturas.historial.modelo
        return m.data(m.index(fila, 0), nombre)
    }

    function capturar(p, nombre) {
        const dir = fixtureFacturas.directorioCapturas()
        if (dir.length > 0) {
            waitForRendering(p)
            grabImage(p).save(dir + "/" + nombre + (Theme.oscuro ? "-oscuro" : "-claro") + ".png")
        }
    }

    // Cada fila muestra lo que dice su modelo; los botones siguen a puede*.
    function test_filasMixtasConSusAcciones() {
        const p = paginaCon("mixto")
        const lista = findChild(p, "listaHistorial")
        tryVerify(() => lista.mostrar && lista.count === fixtureFacturas.historial.modelo.count)
        verify(lista.count >= 8)
        compare(findChild(p, "botonRegresar").text, fixtureFacturas.app.textoRegresar)
        const resultados = []
        for (let i = 0; i < lista.count; ++i) {
            lista.positionViewAtIndex(i, ListView.Contain)
            const fila = findChild(p, "filaHistorial_" + i)
            verify(fila, "fila " + i)
            resultados.push(rol(i, HistorialListModel.ResultadoRole))
            compare(findChild(fila, "tituloFila").text, rol(i, HistorialListModel.TituloRole))
            compare(findChild(fila, "badgeResultado").texto, rol(i, HistorialListModel.ResultadoTextoRole))
            compare(fila.Accessible.name, rol(i, HistorialListModel.NombreAccesibleRole))
            compare(findChild(fila, "botonVerFacturas").visible, rol(i, HistorialListModel.PuedeVerFacturasRole))
            compare(findChild(fila, "botonReintentar").visible, rol(i, HistorialListModel.PuedeReintentarRole))
            compare(findChild(fila, "botonDetalles").visible, rol(i, HistorialListModel.PuedeVerDetallesRole))
            const detalle = rol(i, HistorialListModel.DetalleRole)
            if (detalle.length > 0)
                verify(findChild(fila, "conteosFila").text.indexOf(detalle) >= 0, "detalle visible en la fila " + i)
            const r = rol(i, HistorialListModel.ResultadoRole)
            if (r === "NoCompletada" || r === "Eliminada" || r === "Anterior")
                verify(!findChild(fila, "botonReintentar").visible, r + " sin Reintentar")
            if (r === "Eliminada" || r === "Anterior")
                verify(!findChild(fila, "botonVerFacturas").visible, r + " sin Ver facturas")
        }
        for (const r of ["Completa", "EnPreparacion", "RequiereAtencion", "SinFacturas", "NoCompletada", "Eliminada", "Anterior"])
            verify(resultados.indexOf(r) >= 0, "hay una fila " + r)
        lista.positionViewAtIndex(0, ListView.Beginning)
        capturar(p, "13-historial-960")
    }

    // Fuera de Diagnostico: sin "solicitud", "paquete" ni "verificacion".
    function test_vocabularioSinTerminosSat() {
        const p = paginaCon("mixto")
        const lista = findChild(p, "listaHistorial")
        tryVerify(() => lista.count > 0)
        const textos = []
        const visitar = (item) => {
            if (!item || item.visible === false || item.opacity === 0)
                return
            for (const prop of ["text", "titulo", "subtitulo", "descripcion", "texto"]) {
                const v = item[prop]
                if (typeof v === "string" && v.length > 0)
                    textos.push(v)
            }
            if (item.Accessible && item.Accessible.name)
                textos.push(item.Accessible.name)
            if (item.Accessible && item.Accessible.description)
                textos.push(item.Accessible.description)
            for (let i = 0; i < item.children.length; ++i)
                visitar(item.children[i])
        }
        for (let i = 0; i < lista.count; ++i) {
            lista.positionViewAtIndex(i, ListView.Contain)
            wait(0)
            visitar(p)
        }
        // Textos de todas las filas del modelo (tambien las fuera de vista).
        const m = fixtureFacturas.historial.modelo
        for (let i = 0; i < m.count; ++i) {
            for (const r of [HistorialListModel.TituloRole, HistorialListModel.TextoFechaRole, HistorialListModel.ResultadoTextoRole,
                             HistorialListModel.TextoConteosRole, HistorialListModel.DetalleRole, HistorialListModel.NombreAccesibleRole])
                textos.push(String(rol(i, r)))
        }
        verify(textos.length > 20)
        for (const t of textos) {
            const bajo = t.toLowerCase()
            for (const prohibido of ["solicitud", "paquete", "verificaci"])
                verify(bajo.indexOf(prohibido) < 0, "\"" + t + "\" contiene \"" + prohibido + "\"")
        }
    }

    function test_tecladoFocoFlechasTabYEnter() {
        const p = paginaCon("mixto")
        const lista = findChild(p, "listaHistorial")
        tryVerify(() => lista.activeFocus && lista.currentIndex === 0)
        verify(!lista.focoVisible) // foco inicial sin anillo
        keyClick(Qt.Key_Down)
        compare(lista.currentIndex, 1)
        verify(lista.focoVisible)
        // Tab pasa a los botones de la fila actual (y no a los de otras filas).
        keyClick(Qt.Key_Tab)
        const fila = findChild(p, "filaHistorial_1")
        const conFoco = caso.Window.window.activeFocusItem
        verify(conFoco === findChild(fila, "botonVerFacturas") || conFoco === findChild(fila, "botonReintentar")
               || conFoco === findChild(fila, "botonDetalles"), "foco en un boton de la fila actual")
        // Enter en la lista: Ver facturas de la fila (Facturas en su mes).
        lista.forceActiveFocus(Qt.TabFocusReason)
        let indice = -1
        for (let i = 0; i < lista.count; ++i) {
            if (rol(i, HistorialListModel.PuedeVerFacturasRole)) { indice = i; break }
        }
        verify(indice >= 0)
        lista.currentIndex = indice
        keyClick(Qt.Key_Return)
        tryCompare(fixtureFacturas.app, "pagina", AppViewModel.Facturas)
        tryCompare(fixtureFacturas.facturas, "mes", rol(indice, HistorialListModel.MesRole))
    }

    function test_detallesConDireccionYAtras() {
        const p = paginaCon("mixto")
        const lista = findChild(p, "listaHistorial")
        tryVerify(() => lista.count > 0)
        let indice = -1
        for (let i = 0; i < lista.count; ++i) {
            if (rol(i, HistorialListModel.EligeDireccionRole)) { indice = i; break }
        }
        verify(indice >= 0, "hay una fila con emitidas y recibidas")
        lista.positionViewAtIndex(indice, ListView.Contain)
        const fila = findChild(p, "filaHistorial_" + indice)
        mouseClick(findChild(fila, "botonDetalles"))
        const menu = findChild(p, "menuDireccion")
        tryVerify(() => menu.opened)
        verify(p.dialogoAbierto)
        capturar(p, "historial-direccion-960")
        menu.itemAt(1).triggered() // Recibidas
        tryCompare(fixtureFacturas.app, "pagina", AppViewModel.Detalle)
        // "Atras" del diagnostico vuelve a Historial.
        compare(fixtureFacturas.app.textoRegresar, "Historial")
        fixtureFacturas.app.regresar()
        compare(fixtureFacturas.app.pagina, AppViewModel.Historial)
    }

    function test_atrasYEscapeRegresanAlOrigen() {
        let p = paginaCon("mixto")
        compare(fixtureFacturas.app.textoRegresar, "Facturas")
        mouseClick(findChild(p, "botonRegresar"))
        tryCompare(fixtureFacturas.app, "pagina", AppViewModel.Facturas)
        p = paginaCon("mixto")
        tryVerify(() => findChild(p, "listaHistorial").activeFocus)
        keyClick(Qt.Key_Escape)
        tryCompare(fixtureFacturas.app, "pagina", AppViewModel.Facturas)
    }

    function test_vacioYError() {
        let p = paginaCon("vacio")
        tryCompare(fixtureFacturas.historial, "vista", "vacio")
        verify(findChild(p, "estadoVacioHistorial").visible)
        verify(!findChild(p, "listaHistorial").mostrar)
        p = paginaCon("error")
        tryCompare(fixtureFacturas.historial, "vista", "error")
        const error = findChild(p, "estadoErrorHistorial")
        verify(error.visible)
        compare(findChild(p, "errorHistorial").text, fixtureFacturas.historial.textoVista)
        const antes = fixtureFacturas.numHistoriales()
        mouseClick(findChild(p, "botonReintentarHistorial"))
        tryVerify(() => fixtureFacturas.numHistoriales() > antes)
    }

    function test_anchos_data() {
        const r = []
        for (const t of [{ ancho: 960, alto: 640 }, { ancho: 560, alto: 420 }]) {
            for (const oscuro of [false, true])
                r.push({ tag: t.ancho + (oscuro ? "-oscuro" : "-claro"), ancho: t.ancho, alto: t.alto, oscuro: oscuro })
        }
        return r
    }

    function test_anchos(d) {
        Theme.oscuro = d.oscuro
        const p = paginaCon("mixto", d.ancho, d.alto)
        const lista = findChild(p, "listaHistorial")
        tryVerify(() => lista.count > 0)
        const xa = findChild(findChild(p, "filaHistorial_0"), "badgeResultado").mapToItem(p, 0, 0).x
        for (let i = 0; i < Math.min(4, lista.count); ++i) {
            lista.positionViewAtIndex(i, ListView.Contain)
            wait(0)
            const fila = findChild(p, "filaHistorial_" + i)
            verify(fila, "fila " + i)
            // Botones dentro de la fila (sin desborde horizontal).
            for (const nombre of ["botonVerFacturas", "botonReintentar", "botonDetalles"]) {
                const b = findChild(fila, nombre)
                if (!b.visible)
                    continue
                const x = b.mapToItem(fila, 0, 0).x
                verify(x + b.width <= fila.width + 0.5, nombre + " de la fila " + i + " dentro de la fila")
            }
            // Alto derivado del contenido con padding espacioM.
            const titulo = findChild(fila, "tituloFila")
            verify(titulo.mapToItem(fila, 0, 0).y >= Theme.espacioM - 0.5)
            // Badges alineados (columna de mes de ancho fijo).
            if (i > 0) {
                const xb = findChild(fila, "badgeResultado").mapToItem(p, 0, 0).x
                compare(Math.round(xb), Math.round(xa))
            }
        }
        lista.positionViewAtIndex(0, ListView.Beginning)
        capturar(p, "historial-" + d.ancho)
    }
}
