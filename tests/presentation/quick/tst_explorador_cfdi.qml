import QtQuick
import QtTest
import SatCfdiDownloader

// Explorador de CFDI (T015.3): lista con columnas y nombre accesible, teclado
// (Tab por busqueda, filtro y lista; flechas; Enter = intencion de
// previsualizar; ⌘C copia el UUID), busqueda sin coincidencias con "Limpiar
// filtros", error con "Reintentar", lineas de estado con la accion de la
// carpeta, "Ver facturas (N)" del detalle y claro/oscuro.
TestCase {
    id: caso
    name: "ExploradorCfdiPage"
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
    Component {
        id: detalleComp
        DetalleSolicitudPage {
            width: caso.width
            height: caso.height
            app: fixtureExplorador.app
            detalle: fixtureExplorador.detalle
        }
    }

    function init() {
        failOnWarning(/^(?!Populating font family aliases took).*/)
        wait(0)
        Theme.oscuro = false
        fixtureExplorador.reiniciar()
    }

    function cleanup() {
        Theme.oscuro = false
    }

    // indiceCambiado pide un resumen al explorador y otro al detalle: se
    // resuelven los dos ultimos con el mismo valor.
    function resolverResumenesRecarga(r) {
        const n = fixtureExplorador.numResumenes()
        fixtureExplorador.resolverResumen(n - 2, r)
        fixtureExplorador.resolverResumen(n - 1, r)
    }

    // Detalle con un paquete descargado y explorador abierto con `n` CFDI.
    function paginaCon(n, resumen) {
        fixtureExplorador.abrirDetalle(["Descargado"])
        tryVerify(() => fixtureExplorador.detalle.cargada)
        verify(fixtureExplorador.app.abrirFacturas(fixtureExplorador.solicitudId))
        const p = createTemporaryObject(paginaComp, caso)
        verify(p)
        const r = resumen !== undefined ? resumen : { total: n }
        // El detalle ya pidio su resumen (indice 0); el explorador, el 1.
        fixtureExplorador.resolverResumen(fixtureExplorador.numResumenes() - 1, r)
        fixtureExplorador.resolverLista(fixtureExplorador.numListas() - 1, n)
        tryCompare(fixtureExplorador.explorador, "estado", ExploradorCfdiViewModel.ConDatos)
        // Foco inicial ya aplicado (diferido), antes de que la prueba mueva el foco.
        tryVerify(() => findChild(p, "listaFacturas").activeFocus || findChild(p, "campoBusquedaFacturas").activeFocus)
        return p
    }

    function test_listaConColumnasYNombreAccesible() {
        const p = paginaCon(3)
        const lista = findChild(p, "listaFacturas")
        tryVerify(() => lista.visible && lista.count === 3)
        compare(findChild(p, "contadorEncabezado").text, "3")
        verify(findChild(p, "subtituloEncabezado").text.indexOf("XAXX010101000") === 0)
        tryVerify(() => lista.itemAtIndex(0) !== null)
        const fila = lista.itemAtIndex(0)
        compare(fila.Accessible.name, "Ingreso, 3 sep 2026, Empresa Ejemplo XAXX010101000, 1160.00 MXN")
        compare(findChild(fila, "uuidCfdi").text, "0A1B2C3D")
        compare(findChild(fila, "totalCfdi").text, "1160.00")
        compare(findChild(fila, "monedaCfdi").text, "MXN · PUE")
        compare(findChild(fila, "tipoCfdi").text, "Ingreso · A-100")
        compare(findChild(fila, "estadoParseoCfdi").text, "Completo")
        verify(findChild(fila, "botonCopiarUuid").visible)
        // Sin lineas de estado ni estados vacios.
        verify(!findChild(p, "lineasEstadoFacturas").visible)
        verify(!findChild(p, "estadoSinFacturas").visible)
        verify(!findChild(p, "estadoErrorFacturas").visible)
    }

    function test_tecladoTabFlechasEnterYCopiar() {
        const p = paginaCon(3)
        const lista = findChild(p, "listaFacturas")
        const busqueda = findChild(p, "campoBusquedaFacturas")
        const filtro = findChild(p, "filtroTipoComprobante")
        // Foco inicial en la lista (hay CFDI).
        tryVerify(() => lista.activeFocus && lista.currentIndex === 0)
        busqueda.forceActiveFocus()
        keyClick(Qt.Key_Tab)
        verify(filtro.activeFocus)
        keyClick(Qt.Key_Tab)
        verify(lista.activeFocus)
        compare(lista.currentIndex, 0)
        keyClick(Qt.Key_Down)
        compare(lista.currentIndex, 1)
        keyClick(Qt.Key_Down)
        compare(lista.currentIndex, 2)
        keyClick(Qt.Key_Up)
        compare(lista.currentIndex, 1)
        // Enter: intencion de previsualizar (T015.4), sin efecto visible.
        keyClick(Qt.Key_Return)
        compare(fixtureExplorador.numPrevisualizaciones(), 1)
        compare(fixtureExplorador.ultimaPrevisualizacion(), "PAQ_01:1")
        compare(fixtureExplorador.app.pagina, AppViewModel.Facturas)
        // ⌘C copia el UUID completo de la fila.
        verify(p.copiarUuidActual())
        // Flecha abajo desde la busqueda pasa a la lista.
        busqueda.forceActiveFocus()
        keyClick(Qt.Key_Down)
        verify(lista.activeFocus)
        // Escape regresa al detalle.
        keyClick(Qt.Key_Escape)
        compare(fixtureExplorador.app.pagina, AppViewModel.Detalle)
    }

    function test_busquedaSinCoincidenciasYLimpiarFiltros() {
        const p = paginaCon(3)
        const busqueda = findChild(p, "campoBusquedaFacturas")
        const listas = fixtureExplorador.numListas()
        busqueda.forceActiveFocus()
        keyClick("z")
        keyClick("z")
        // Retardo 0 en el fixture: puede haber una consulta por tecla; gana la ultima.
        tryVerify(() => fixtureExplorador.numListas() > listas && fixtureExplorador.ultimoTexto() === "zz")
        verify(findChild(p, "botonLimpiarFiltrosFacturas").visible)
        fixtureExplorador.resolverLista(fixtureExplorador.numListas() - 1, 0)
        const vacio = findChild(p, "estadoSinCoincidenciasFacturas")
        tryVerify(() => vacio.visible)
        verify(!findChild(p, "listaFacturas").visible)
        compare(findChild(p, "contadorEncabezado").text, "0 de 3")
        mouseClick(findChild(vacio, "botonLimpiarFiltrosVacioFacturas"))
        compare(fixtureExplorador.explorador.texto, "")
        compare(busqueda.text, "")
        verify(busqueda.activeFocus)
        compare(fixtureExplorador.ultimoTexto(), "")
        fixtureExplorador.resolverLista(fixtureExplorador.numListas() - 1, 3)
        tryVerify(() => findChild(p, "listaFacturas").visible)
    }

    function test_filtroPorTipo() {
        const p = paginaCon(2)
        const filtro = findChild(p, "filtroTipoComprobante")
        const listas = fixtureExplorador.numListas()
        filtro.activated(2) // Egreso
        compare(fixtureExplorador.explorador.tipo, "E")
        compare(fixtureExplorador.numListas(), listas + 1)
        compare(fixtureExplorador.ultimoTipo(), "E")
        compare(filtro.currentIndex, 2)
    }

    function test_errorYReintentar() {
        fixtureExplorador.abrirDetalle(["Descargado"])
        tryVerify(() => fixtureExplorador.detalle.cargada)
        verify(fixtureExplorador.app.abrirFacturas(fixtureExplorador.solicitudId))
        const p = createTemporaryObject(paginaComp, caso)
        fixtureExplorador.resolverResumen(fixtureExplorador.numResumenes() - 1, { total: 2 })
        fixtureExplorador.fallarLista(fixtureExplorador.numListas() - 1)
        const error = findChild(p, "estadoErrorFacturas")
        tryVerify(() => error.visible)
        compare(findChild(error, "errorFacturas").text, "No se pudo leer la lista de facturas.")
        verify(!findChild(p, "listaFacturas").visible)
        const reintentar = findChild(error, "botonReintentarFacturas")
        tryVerify(() => reintentar.activeFocus)
        keyClick(Qt.Key_Space)
        compare(fixtureExplorador.explorador.vista, "cargando")
        fixtureExplorador.resolverResumen(fixtureExplorador.numResumenes() - 1, { total: 2 })
        fixtureExplorador.resolverLista(fixtureExplorador.numListas() - 1, 2)
        tryVerify(() => findChild(p, "listaFacturas").visible)
    }

    function test_lineasDeEstadoConAccionDeCarpeta() {
        const p = paginaCon(2, { total: 2, indexando: true, pendientes: 1, noDisponibles: 2, ilegibles: 1,
                                 cfdiIlegibles: 1 })
        const lineas = findChild(p, "lineasEstadoFacturas")
        tryVerify(() => lineas.visible)
        const indexando = findChild(lineas, "lineaEstado_indexando")
        const faltantes = findChild(lineas, "lineaEstado_noDisponibles")
        compare(indexando.titulo, "Indexando 1 paquete…")
        compare(faltantes.titulo, "2 paquetes no disponibles (ZIP faltante)")
        verify(findChild(lineas, "lineaEstado_ilegibles").visible)
        verify(findChild(lineas, "lineaEstado_cfdiIlegibles").visible)
        // Orden: indexando arriba de los no disponibles.
        tryVerify(() => indexando.y < faltantes.y)
        verify(!findChild(indexando, "botonCarpeta_indexando").visible)
        const carpeta = findChild(faltantes, "botonCarpeta_noDisponibles")
        verify(carpeta.visible)
        compare(carpeta.text, "Abrir carpeta de la solicitud")
        mouseClick(carpeta)
        compare(fixtureExplorador.numCarpetas(), 1)
        // La lista sigue visible: no es un error modal.
        verify(findChild(p, "listaFacturas").visible)
    }

    function test_indexandoSinFacturasYSinFacturas() {
        const p = paginaCon(0, { total: 0, indexando: true, pendientes: 2 })
        tryVerify(() => findChild(p, "estadoIndexandoFacturas").visible)
        verify(!findChild(p, "estadoSinFacturas").visible)
        // Termina de indexar sin CFDI: "Sin facturas".
        fixtureExplorador.emitirIndiceCambiado()
        caso.resolverResumenesRecarga({ total: 0 })
        fixtureExplorador.resolverLista(fixtureExplorador.numListas() - 1, 0)
        tryVerify(() => findChild(p, "estadoSinFacturas").visible)
        verify(!findChild(p, "estadoIndexandoFacturas").visible)
        verify(!findChild(p, "lineasEstadoFacturas").visible)
    }

    function test_recargaConservaSeleccion() {
        const p = paginaCon(3)
        const lista = findChild(p, "listaFacturas")
        tryVerify(() => lista.activeFocus && lista.currentIndex === 0)
        keyClick(Qt.Key_Down)
        compare(lista.currentIndex, 1)
        fixtureExplorador.emitirIndiceCambiado()
        verify(lista.visible) // sin parpadeo mientras recarga
        caso.resolverResumenesRecarga({ total: 4 })
        fixtureExplorador.resolverLista(fixtureExplorador.numListas() - 1, 4)
        tryCompare(lista, "count", 4)
        compare(lista.currentIndex, 1)
        verify(lista.visible)
    }

    function test_verFacturasEnElDetalle() {
        fixtureExplorador.abrirDetalle(["Descargado", "Disponible"])
        const d = createTemporaryObject(detalleComp, caso)
        tryVerify(() => fixtureExplorador.detalle.cargada)
        const boton = findChild(d, "botonVerFacturas")
        tryVerify(() => boton.visible)
        compare(boton.text, "Ver facturas")
        fixtureExplorador.resolverResumen(0, { total: 5 })
        tryCompare(boton, "text", "Ver facturas (5)")
        mouseClick(boton)
        compare(fixtureExplorador.app.pagina, AppViewModel.Facturas)
        compare(fixtureExplorador.explorador.solicitudId, fixtureExplorador.solicitudId)
    }

    function test_sinPaquetesDescargadosNoHayVerFacturas() {
        fixtureExplorador.abrirDetalle(["Disponible", "Vencido"])
        const d = createTemporaryObject(detalleComp, caso)
        tryVerify(() => fixtureExplorador.detalle.cargada)
        verify(!findChild(d, "botonVerFacturas").visible)
        compare(fixtureExplorador.numResumenes(), 0)
    }

    function test_temaOscuro() {
        Theme.oscuro = true
        const p = paginaCon(2, { total: 2, noDisponibles: 1 })
        const lista = findChild(p, "listaFacturas")
        tryVerify(() => lista.visible && lista.count === 2)
        compare(p.background.color, Theme.superficie)
        verify(Qt.colorEqual(Theme.superficie, "#252527"))
        verify(findChild(p, "lineaEstado_noDisponibles").visible)
    }
}
