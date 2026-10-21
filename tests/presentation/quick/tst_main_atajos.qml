import QtQuick
import QtTest
import SatCfdiDownloader

// Main.qml (T023.4): ⌘1 desde Nueva solicitud pasa por su regresar() antes de
// ir a Facturas. Ventana completa con la app de FixtureFacturas (con fuente:
// Facturas existe) y el resto de view models de las otras fixtures.
TestCase {
    id: caso
    name: "MainAtajos"
    when: windowShown

    Component {
        id: ventanaComp
        Main {
            appViewModel: fixtureFacturas.app
            facturasViewModel: fixtureFacturas.facturas
            solicitudesModel: fixture.solicitudesModel
            nuevaSolicitudViewModel: fixture.nuevaSolicitud
            detalleViewModel: fixture.detalle
            perfilesViewModel: fixture.perfiles
            eFirmaViewModel: fixture.eFirma
            exploradorViewModel: fixtureExplorador.explorador
            avisoRaizPaquetesViewModel: fixtureAvisoRaiz.crear(AvisoRaizPaquetesViewModel.Ninguno)
        }
    }

    // Paginas por las que pasa la app (en orden).
    property var paginas: []
    Connections {
        id: espia
        target: null
        function onPaginaChanged() { caso.paginas.push(espia.target.pagina) }
    }

    function init() {
        failOnWarning(/^(?!Populating font family aliases took).*/)
        fixture.reiniciar()
        fixtureExplorador.reiniciar()
        fixtureFacturas.reiniciar(true)
        caso.paginas = []
        espia.target = fixtureFacturas.app
    }

    function cleanup() {
        espia.target = null
    }

    // D10: ⌘N y "Nueva solicitud" fuera del recorrido principal; solo en la
    // lista provisional de Diagnostico.
    function test_cmdNSoloEnDiagnostico() {
        const v = createTemporaryObject(ventanaComp, caso)
        verify(v)
        v.visible = true
        v.requestActivate()
        tryVerify(() => v.active)
        tryVerify(() => v.paginaFacturas !== null)
        const nueva = findChild(v, "atajoNuevaSolicitud")
        const eliminar = findChild(v, "atajoEliminar")
        verify(!nueva.enabled)
        verify(!eliminar.enabled)
        verify(!v.enDiagnostico)
        v.paginaFacturas.forceActiveFocus()
        keySequence(StandardKey.New)
        wait(0)
        verify(v.paginaFacturas !== null)
        compare(fixtureFacturas.app.pagina, AppViewModel.Facturas)
        // Menus en Facturas: ningun texto con terminos del SAT.
        for (const nombre of ["atajoBuscar", "atajoAccionPrincipal", "atajoMesAnterior", "atajoMesSiguiente",
                              "atajoRegresar", "atajoPerfiles", "accionDiagnostico"]) {
            const t = findChild(v, nombre).text.toLowerCase()
            for (const prohibido of ["solicitud", "paquete", "verificaci"])
                verify(t.indexOf(prohibido) < 0, nombre + ": " + t)
        }
        // Ir > Diagnostico: ahi si esta disponible.
        findChild(v, "accionDiagnostico").trigger()
        tryVerify(() => v.paginaLista !== null)
        verify(v.enDiagnostico)
        tryVerify(() => nueva.enabled)
    }

    function test_cmd1DesdeNuevaPasaPorRegresar() {
        const v = createTemporaryObject(ventanaComp, caso)
        verify(v)
        v.visible = true
        v.requestActivate()
        tryVerify(() => v.active)
        fixtureFacturas.app.mostrarNueva()
        tryVerify(() => v.paginaNueva !== null)
        caso.paginas = []
        const atajo = findChild(v, "atajoRegresar")
        verify(atajo)
        verify(atajo.enabled)
        atajo.trigger()
        // regresar() de Nueva (mostrarLista) y despues Facturas.
        compare(caso.paginas, [AppViewModel.Lista, AppViewModel.Facturas])
        tryVerify(() => v.paginaFacturas !== null)
    }
}
