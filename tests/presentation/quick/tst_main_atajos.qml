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
            respaldoViewModel: fixtureRespaldo.respaldo
        }
    }

    function init() {
        failOnWarning(/^(?!Populating font family aliases took).*/)
        fixture.reiniciar()
        fixtureExplorador.reiniciar()
        fixtureFacturas.reiniciar(true)
        fixtureRespaldo.reiniciar(0, false, false)
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

    // T025.6: ⌘4 abre Respaldo (secundaria) y Escape regresa al origen.
    function test_cmd4RespaldoYAtras() {
        const v = ventanaEnFacturas()
        const respaldo = findChild(v, "atajoRespaldo")
        verify(respaldo.enabled)
        respaldo.trigger()
        tryVerify(() => v.paginaRespaldo !== null)
        compare(fixtureFacturas.app.pagina, AppViewModel.Respaldo)
        verify(!respaldo.enabled)
        compare(findChild(v.paginaRespaldo, "botonRegresar").text, "Facturas")
        tryVerify(() => v.activeFocusItem !== null)
        keyClick(Qt.Key_Escape)
        tryVerify(() => v.paginaFacturas !== null)
    }

    // T025.6: la franja de datos previos es persistente y solo navega.
    function test_franjaDatosPreviosNavegaARespaldo() {
        const v = ventanaEnFacturas()
        const franja = findChild(v, "franjaAvisosRespaldo")
        verify(!franja.visible)
        compare(franja.height, 0)
        fixtureRespaldo.responderDatosPrevios(true)
        tryVerify(() => franja.visible)
        const revisar = findChild(franja, "botonRevisarDatosPrevios")
        verify(revisar.visible)
        tryVerify(() => revisar.width > 0 && revisar.height > 0)
        waitForRendering(v.contentItem)
        mouseClick(revisar)
        tryVerify(() => v.paginaRespaldo !== null)
        verify(franja.visible)
        verify(!findChild(franja, "botonRevisarDatosPrevios").visible)
        compare(fixtureRespaldo.descartesDatosPrevios(), 0)
    }

    function ventanaEnRespaldo() {
        const v = ventanaEnFacturas()
        findChild(v, "atajoRespaldo").trigger()
        tryVerify(() => v.paginaRespaldo !== null)
        wait(0)
        return v
    }

    // H2: salir de Respaldo mientras se crea la contrasena no pierde la clave.
    function test_navegarDuranteCrearContrasenaConservaLaClave() {
        const v = ventanaEnRespaldo()
        const p = v.paginaRespaldo
        findChild(p, "botonContrasena").clicked()
        const d = p.dialogoContrasena
        tryVerify(() => d.opened)
        d.campoNueva.text = "contrasena-sintetica-1"
        d.campoConfirmacion.text = "contrasena-sintetica-1"
        findChild(d.contentItem, "dialogoContrasenaConfirmar").clicked()
        compare(fixtureRespaldo.llamadasContrasena(), 1)
        // Navegacion externa (menu bar o notificacion) con la operacion en curso.
        fixtureFacturas.app.mostrarFacturas()
        tryVerify(() => v.paginaFacturas !== null)
        fixtureRespaldo.responderContrasena()
        const franja = findChild(v, "franjaAvisosRespaldo")
        tryVerify(() => findChild(franja, "avisoClavePendiente").visible)
        findChild(franja, "botonVerClavePendiente").clicked()
        tryVerify(() => v.paginaRespaldo !== null)
        tryVerify(() => v.paginaRespaldo.dialogoClave.opened)
        compare(v.paginaRespaldo.dialogoClave.clave, "ABCD-EFGH-JKMN-PQRS")
        verify(!findChild(franja, "avisoClavePendiente").visible)
        findChild(v.paginaRespaldo.dialogoClave.contentItem, "botonClaveGuardada").clicked()
        tryVerify(() => !v.paginaRespaldo.dialogoClave.visible)
        verify(!fixtureRespaldo.respaldo.hayClavePendiente)
    }

    // Calidad: salir durante un respaldo y una verificacion y volver.
    function test_navegarDuranteRespaldoYVerificacion() {
        fixtureRespaldo.reiniciar(0, true, false)
        const v = ventanaEnRespaldo()
        findChild(v.paginaRespaldo, "botonRespaldarAhora").clicked()
        compare(fixtureRespaldo.respaldos(), 1)
        const p = v.paginaRespaldo
        findChild(p, "botonRestaurar").clicked()
        const d = p.dialogoRestaurar
        tryVerify(() => d.opened)
        d.archivo = "file:///Users/usuario/Respaldos/a.afbak"
        d.nombreArchivo = "a.afbak"
        d.campoSecreto.text = "x"
        findChild(d.contentItem, "botonVerificarRestaurar").clicked()
        compare(fixtureRespaldo.preparaciones(), 1)

        fixtureFacturas.app.mostrarFacturas()
        tryVerify(() => v.paginaFacturas !== null)
        fixtureRespaldo.terminarRespaldoConCopiasSinRegistro(1)
        fixtureRespaldo.responderPreparacion()
        tryCompare(fixtureRespaldo.respaldo, "estadoRestauracion", RespaldoViewModel.Preparada)
        verify(!fixtureRespaldo.respaldo.respaldando)

        findChild(v, "atajoRespaldo").trigger()
        tryVerify(() => v.paginaRespaldo !== null)
        // Al volver, el resultado y el resumen preparado siguen ahi.
        tryVerify(() => v.paginaRespaldo.dialogoRestaurar.opened)
        verify(findChild(v.paginaRespaldo.dialogoRestaurar.contentItem, "resumenRestaurar").visible)
        verify(findChild(v.paginaRespaldo, "resultadoRespaldo").visible)
        verify(findChild(v.paginaRespaldo, "avisoCopiasSinRegistro").visible)
        findChild(v.paginaRespaldo.dialogoRestaurar.contentItem, "botonDescartarRestaurar").clicked()
        tryVerify(() => !v.paginaRespaldo.dialogoRestaurar.visible)
        compare(fixtureRespaldo.descartesRestauracion(), 1)
    }

    // Menus en Facturas: ningun texto con terminos del SAT.
    function test_menusSinVocabularioSatEnFacturas() {
        const v = ventanaEnFacturas()
        for (const nombre of ["atajoBuscar", "atajoAccionPrincipal", "atajoMesAnterior", "atajoMesSiguiente",
                              "atajoRegresar", "atajoPerfiles", "atajoHistorial", "atajoRespaldo", "accionDiagnostico"]) {
            const accion = findChild(v, nombre)
            verify(accion, nombre)
            const t = accion.text.toLowerCase()
            for (const prohibido of ["solicitud", "paquete", "verificaci"])
                verify(t.indexOf(prohibido) < 0, nombre + ": " + t)
        }
    }
}
