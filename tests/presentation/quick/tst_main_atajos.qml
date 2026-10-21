import QtQuick
import QtTest
import SatCfdiDownloader

// Main.qml (T023.4, T023.5): ventana completa con la app de FixtureFacturas
// (Facturas e Historial con fuentes sinteticas) y el resto de view models de
// las otras fixtures. Ninguna ruta lleva a Nueva solicitud (sin ⌘N ni items de
// menu); ⌘3 abre Historial; "Ir > Diagnostico" abre la lista tecnica con
// "Atras" al origen; "Eliminar…" solo en el detalle; menus sin terminos del
// SAT fuera de Diagnostico.
TestCase {
    id: caso
    name: "MainAtajos"
    when: windowShown

    Component {
        id: ventanaComp
        Main {
            appViewModel: fixtureFacturas.app
            facturasViewModel: fixtureFacturas.facturas
            historialViewModel: fixtureFacturas.historial
            solicitudesModel: fixture.solicitudesModel
            detalleViewModel: fixture.detalle
            perfilesViewModel: fixture.perfiles
            eFirmaViewModel: fixture.eFirma
            exploradorViewModel: fixtureExplorador.explorador
            avisoRaizPaquetesViewModel: fixtureAvisoRaiz.crear(AvisoRaizPaquetesViewModel.Ninguno)
        }
    }

    function init() {
        failOnWarning(/^(?!Populating font family aliases took).*/)
        fixture.reiniciar()
        fixtureExplorador.reiniciar()
        fixtureFacturas.reiniciar(true)
    }

    function ventanaEnFacturas() {
        const v = createTemporaryObject(ventanaComp, caso)
        verify(v)
        v.visible = true
        v.requestActivate()
        tryVerify(() => v.active)
        fixtureFacturas.app.mostrarFacturas()
        tryVerify(() => v.paginaFacturas !== null)
        // Facturas ya cargo y se dibujo (cambiar de pagina en el primer cuadro
        // provoca un aviso interno de Qt sobre el animation driver offscreen).
        tryVerify(() => fixtureFacturas.facturas.vista !== "cargando")
        waitForRendering(v.contentItem)
        return v
    }

    // T023.5: ninguna ruta a NuevaSolicitudPage.
    function test_ningunaRutaANuevaSolicitud() {
        const v = ventanaEnFacturas()
        verify(findChild(v, "atajoNuevaSolicitud") === null, "sin accion ⌘N")
        verify(v.paginaNueva === undefined, "Main ya no conoce la pagina")
        keySequence(StandardKey.New)
        wait(0)
        verify(v.paginaFacturas !== null)
        // Tambien desde Diagnostico (antes tenia el boton y ⌘N).
        findChild(v, "accionDiagnostico").trigger()
        tryVerify(() => v.paginaLista !== null)
        keySequence(StandardKey.New)
        wait(0)
        verify(v.paginaLista !== null)
        verify(findChild(v.paginaLista, "botonNuevaSolicitud") === null)
        verify(findChild(v.paginaLista, "botonNuevaSolicitudVacia") === null)
    }

    function test_cmd3HistorialYAtras() {
        const v = ventanaEnFacturas()
        const historial = findChild(v, "atajoHistorial")
        verify(historial.enabled)
        historial.trigger()
        tryVerify(() => v.paginaHistorial !== null)
        compare(fixtureFacturas.app.pagina, AppViewModel.Historial)
        verify(!historial.enabled)
        // Atras (Escape) vuelve a Facturas.
        tryVerify(() => v.activeFocusItem !== null)
        keyClick(Qt.Key_Escape)
        tryVerify(() => v.paginaFacturas !== null)
    }

    function test_irADiagnosticoConAtrasYEliminarSoloEnDetalle() {
        const v = ventanaEnFacturas()
        const eliminar = findChild(v, "atajoEliminar")
        verify(!eliminar.enabled)
        findChild(v, "accionDiagnostico").trigger()
        tryVerify(() => v.paginaLista !== null)
        verify(v.enDiagnostico)
        verify(!eliminar.enabled)
        const atras = findChild(v.paginaLista, "botonRegresar")
        verify(atras.visible)
        compare(atras.text, "Facturas")
        mouseClick(atras)
        tryVerify(() => v.paginaFacturas !== null)
        // ⌘1 desde Diagnostico tambien lleva a Facturas.
        findChild(v, "accionDiagnostico").trigger()
        tryVerify(() => v.paginaLista !== null)
        findChild(v, "atajoRegresar").trigger()
        tryVerify(() => v.paginaFacturas !== null)
    }

    // Menus en Facturas: ningun texto con terminos del SAT.
    function test_menusSinVocabularioSatEnFacturas() {
        const v = ventanaEnFacturas()
        for (const nombre of ["atajoBuscar", "atajoAccionPrincipal", "atajoMesAnterior", "atajoMesSiguiente",
                              "atajoRegresar", "atajoPerfiles", "atajoHistorial", "accionDiagnostico"]) {
            const accion = findChild(v, nombre)
            verify(accion, nombre)
            const t = accion.text.toLowerCase()
            for (const prohibido of ["solicitud", "paquete", "verificaci"])
                verify(t.indexOf(prohibido) < 0, nombre + ": " + t)
        }
    }
}
