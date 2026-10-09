import QtQuick
import QtTest
import SatCfdiDownloader

// PerfilesSatPage (T005.1): estados de lista, formulario nuevo/edicion,
// RfcDuplicado con foco en RFC, inactivos, Verificando / Estado no disponible
// con reintento, confirmacion previa al reemplazo y limpieza de la contrasena
// al cambiar de perfil y al ocultar la ventana.
TestCase {
    id: caso
    name: "PerfilesSatPage"
    when: windowShown
    // La pagina es hija del TestCase: debe ser visible para layout, foco y clics.
    visible: true
    width: 900
    height: 640

    Component {
        id: paginaComp
        PerfilesSatPage {
            width: caso.width
            height: caso.height
            app: fixture.app
            perfiles: fixture.perfiles
            eFirma: fixture.eFirma
        }
    }

    function init() {
        // Cualquier warning falla la prueba (salvo el aviso de fuentes de offscreen).
        failOnWarning(/^(?!Populating font family aliases took).*/)
        wait(0) // procesa el borrado diferido de los objetos de la prueba anterior
        fixture.reiniciar()
    }

    function escribir(texto) {
        for (let i = 0; i < texto.length; ++i)
            keyClick(texto[i])
    }

    // El panel del formulario se acomoda en el siguiente pulido del layout.
    function esperarPanel(p) {
        const panel = findChild(p, "panelPerfil")
        tryVerify(() => panel.visible && panel.width > 0 && panel.height > 0)
        waitForItemPolished(panel)
    }

    function crearPagina() {
        fixture.app.mostrarPerfiles() // carga la lista (listarNoEliminados 0)
        const p = createTemporaryObject(paginaComp, caso)
        verify(p)
        compare(fixture.numListas(), 1)
        return p
    }

    // Pagina con perfiles cargados y verificados con `estados`.
    function paginaCon(perfiles, estados) {
        const p = crearPagina()
        const ids = fixture.resolverLista(0, perfiles)
        tryVerify(() => fixture.numResumenes() === estados.length, 2000)
        for (let i = 0; i < estados.length; ++i) {
            if (estados[i] === "NoDisponible")
                fixture.fallarResumen(i)
            else
                fixture.resolverResumen(i, estados[i])
        }
        return { pagina: p, ids: ids }
    }

    function test_vaciaYNuevoPerfil() {
        const p = crearPagina()
        verify(findChild(p, "estadoCargandoPerfiles").visible)
        fixture.resolverLista(0, [])
        const vacio = findChild(p, "estadoVacioPerfiles")
        tryVerify(() => vacio.visible)
        verify(vacio.Accessible.name.length > 0)
        tryVerify(() => findChild(p, "botonNuevoPerfil").activeFocus)

        keyClick(Qt.Key_Space) // "Nuevo perfil"
        tryCompare(fixture.perfiles, "modo", PerfilesSatViewModel.Nuevo)
        const rfc = findChild(p, "campoRfcPerfil")
        tryVerify(() => rfc.activeFocus)
        verify(!rfc.readOnly)
        verify(!findChild(p, "seccionEFirma").visible) // e.firma solo con perfil guardado
    }

    function test_rfcDuplicadoConservaYEnfocaRfc() {
        const p = crearPagina()
        fixture.resolverLista(0, [])
        mouseClick(findChild(p, "botonNuevoPerfil"))
        const rfc = findChild(p, "campoRfcPerfil")
        tryVerify(() => rfc.activeFocus)
        escribir("AAA010101AAA")
        keyClick(Qt.Key_Tab)
        const nombre = findChild(p, "campoNombrePerfil")
        tryVerify(() => nombre.activeFocus)
        escribir("Otro")
        keyClick(Qt.Key_Return) // guarda
        compare(fixture.numCreaciones(), 1)
        fixture.resolverCreacionDuplicado(0)
        tryCompare(fixture.perfiles, "errorKey", "RfcDuplicado")
        const error = findChild(p, "errorRfcPerfil")
        tryVerify(() => error.visible)
        verify(error.Accessible.name.indexOf("RFC") >= 0)
        tryVerify(() => rfc.activeFocus)
        compare(rfc.text, "AAA010101AAA")
        compare(nombre.text, "Otro")
        compare(fixture.perfiles.modo, PerfilesSatViewModel.Nuevo)
    }

    function test_creadoQuedaEnEdicionConRfcNoEditable() {
        const p = crearPagina()
        fixture.resolverLista(0, [])
        mouseClick(findChild(p, "botonNuevoPerfil"))
        fixture.perfiles.rfc = "AAA010101AAA"
        fixture.perfiles.nombre = "Contribuyente"
        const guardar = findChild(p, "botonGuardarPerfil")
        esperarPanel(p)
        tryVerify(() => guardar.visible && guardar.enabled)
        mouseClick(guardar)
        compare(fixture.numCreaciones(), 1)
        fixture.resolverCreacion(0, "AAA010101AAA", "Contribuyente")
        tryCompare(fixture.perfiles, "modo", PerfilesSatViewModel.Edicion)
        const rfc = findChild(p, "campoRfcPerfil")
        tryVerify(() => rfc.readOnly)
        verify(rfc.Accessible.name.indexOf("no editable") >= 0)
        verify(findChild(p, "seccionEFirma").visible)
    }

    function test_estadosInactivoYReintentoPorPerfil() {
        const r = paginaCon([
            { rfc: "AAA010101AAA", nombre: "Listo", activo: true },
            { rfc: "BBB010101BBB", nombre: "Inactivo", activo: false },
            { rfc: "CCC010101CCC", nombre: "Sin estado", activo: true },
        ], ["Lista", "Lista", "NoDisponible"])
        const p = r.pagina
        const lista = findChild(p, "listaPerfiles")
        tryVerify(() => lista.visible && lista.count === 3)
        tryVerify(() => findChild(p, "badgePreparacion_2") !== null
                        && findChild(p, "badgePreparacion_2").texto === "Estado no disponible")
        // Estado como texto, no solo color.
        compare(findChild(p, "badgePreparacion_0").texto, "e.firma lista")
        compare(findChild(p, "disponibilidad_0").text, "Disponible para solicitudes")
        verify(findChild(p, "disponibilidad_1").text.indexOf("Inactivo") === 0)
        compare(findChild(p, "badgePreparacion_1").texto, "e.firma lista") // conserva su estado
        verify(findChild(p, "filaPerfil_1").Accessible.name.indexOf("no disponible") >= 0)

        // Reintento por perfil (boton de la fila): solo ese perfil.
        const reintentar = findChild(p, "botonReintentarEstado_2")
        verify(reintentar.visible)
        mouseClick(reintentar)
        compare(fixture.numResumenes(), 4)
        tryCompare(findChild(p, "badgePreparacion_2"), "texto", "Verificando")
        fixture.resolverResumen(3, "SinCredencial")
        tryCompare(findChild(p, "badgePreparacion_2"), "texto", "Sin e.firma")
        verify(!findChild(p, "botonReintentarEstado_2").visible)
    }

    function test_listaErrorConReintento() {
        const p = crearPagina()
        fixture.perfiles.cargar() // la carga 0 queda obsoleta
        compare(fixture.numListas(), 2)
        fixture.resolverLista(1, [])
        fixture.resolverLista(0, [{ rfc: "AAA010101AAA", nombre: "Tardio" }])
        tryVerify(() => findChild(p, "estadoVacioPerfiles").visible)
        wait(0)
        verify(findChild(p, "estadoVacioPerfiles").visible) // la tardia no aplica
    }

    function test_reemplazoPideConfirmacionAntesDeCapturar() {
        const r = paginaCon([{ rfc: "AAA010101AAA", nombre: "Con e.firma", activo: true }], ["Vencida"])
        const p = r.pagina
        verify(fixture.perfiles.seleccionar(r.ids[0]))
        esperarPanel(p)
        const boton = findChild(p, "botonEFirma")
        tryVerify(() => boton.visible && boton.enabled)
        compare(boton.text, "Reemplazar e.firma…")
        mouseClick(boton)
        const confirmacion = findChild(p, "dialogoReemplazoEFirma")
        const dialogo = findChild(p, "dialogoEFirma")
        tryCompare(confirmacion, "opened", true)
        verify(!dialogo.visible) // aun no se captura nada

        // Cancelar: no abre la captura.
        keyClick(Qt.Key_Escape)
        tryCompare(confirmacion, "visible", false)
        wait(0)
        verify(!dialogo.visible)

        mouseClick(boton)
        tryCompare(confirmacion, "opened", true)
        mouseClick(findChild(confirmacion.contentItem, "dialogoReemplazoConfirmar"))
        tryCompare(dialogo, "opened", true)
        verify(fixture.eFirma.esReemplazo)
        compare(fixture.eFirma.perfilId, r.ids[0])
    }

    function test_registrarYCambiarDePerfilLimpiaContrasena() {
        const r = paginaCon([
            { rfc: "AAA010101AAA", nombre: "A", activo: true },
            { rfc: "BBB010101BBB", nombre: "B", activo: true },
        ], ["SinCredencial", "SinCredencial"])
        const p = r.pagina
        verify(fixture.perfiles.seleccionar(r.ids[0]))
        esperarPanel(p)
        const boton = findChild(p, "botonEFirma")
        tryVerify(() => boton.enabled)
        compare(boton.text, "Registrar e.firma…")
        mouseClick(boton)
        const dialogo = findChild(p, "dialogoEFirma")
        tryCompare(dialogo, "opened", true)
        verify(!fixture.eFirma.esReemplazo)
        dialogo.campoContrasena.forceActiveFocus()
        escribir("secreta")
        compare(dialogo.campoContrasena.text, "secreta")

        verify(fixture.perfiles.seleccionar(r.ids[1])) // cambia de perfil
        tryCompare(dialogo, "visible", false)
        compare(dialogo.campoContrasena.text, "")
        compare(fixture.numRegistros(), 0)
    }

    function test_ocultarVentanaLimpiaContrasena() {
        const r = paginaCon([{ rfc: "AAA010101AAA", nombre: "A", activo: true }], ["SinCredencial"])
        const p = r.pagina
        verify(fixture.perfiles.seleccionar(r.ids[0]))
        esperarPanel(p)
        tryVerify(() => findChild(p, "botonEFirma").enabled)
        mouseClick(findChild(p, "botonEFirma"))
        const dialogo = findChild(p, "dialogoEFirma")
        tryCompare(dialogo, "opened", true)
        dialogo.campoContrasena.forceActiveFocus()
        escribir("secreta")

        const ventana = p.Window.window
        ventana.visible = false
        compare(dialogo.campoContrasena.text, "")
        ventana.visible = true
        tryVerify(() => ventana.visible)
        waitForRendering(caso)
    }

    function test_volverASolicitudes() {
        const p = crearPagina()
        fixture.resolverLista(0, [])
        const regresar = findChild(p, "botonRegresar")
        compare(regresar.text, "Solicitudes")
        mouseClick(regresar)
        compare(fixture.app.pagina, AppViewModel.Lista)
    }

    function test_tecladoListaYAccesibilidad() {
        const r = paginaCon([
            { rfc: "AAA010101AAA", nombre: "A", activo: true },
            { rfc: "BBB010101BBB", nombre: "B", activo: true },
        ], ["Lista", "NoDisponible"])
        const p = r.pagina
        const lista = findChild(p, "listaPerfiles")
        tryVerify(() => lista.activeFocus || findChild(p, "botonNuevoPerfil").activeFocus)
        lista.forceActiveFocus()
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_R) // reintenta el perfil con foco
        compare(fixture.numResumenes(), 3)
        keyClick(Qt.Key_Return) // edita el perfil con foco
        tryCompare(fixture.perfiles, "perfilId", r.ids[1])
        tryVerify(() => findChild(p, "campoNombrePerfil").activeFocus)
        verify(lista.Accessible.name.length > 0)
        verify(p.Accessible.name.length > 0)
        for (const nombre of ["botonNuevoPerfil", "botonRegresar", "campoRfcPerfil", "campoNombrePerfil",
                              "botonGuardarPerfil", "botonEFirma"]) {
            verify(findChild(p, nombre).Accessible.name.length > 0, nombre)
        }
    }

    function test_reintentarDeFilaAlcanzableConTab() {
        const r = paginaCon([
            { rfc: "AAA010101AAA", nombre: "A", activo: true },
            { rfc: "BBB010101BBB", nombre: "B", activo: true },
        ], ["Lista", "NoDisponible"])
        const p = r.pagina
        const lista = findChild(p, "listaPerfiles")
        tryVerify(() => findChild(p, "botonReintentarEstado_1") !== null
                        && findChild(p, "botonReintentarEstado_1").visible)
        const boton = findChild(p, "botonReintentarEstado_1")
        compare(boton.Accessible.name, "Reintentar estado de BBB010101BBB")
        lista.forceActiveFocus()
        for (let i = 0; i < 8 && !boton.activeFocus; ++i)
            keyClick(Qt.Key_Tab)
        verify(boton.activeFocus, "Tab no llego a Reintentar de la fila")
        keyClick(Qt.Key_Space)
        compare(fixture.numResumenes(), 3)
        tryCompare(findChild(p, "badgePreparacion_1"), "texto", "Verificando")
    }

    // Registro de e.firma en Validando con la promesa pendiente.
    function registrarEnValidando(r) {
        const p = r.pagina
        verify(fixture.perfiles.seleccionar(r.ids[0]))
        esperarPanel(p)
        const boton = findChild(p, "botonEFirma")
        tryVerify(() => boton.enabled)
        mouseClick(boton)
        const dialogo = findChild(p, "dialogoEFirma")
        tryCompare(dialogo, "opened", true)
        fixture.eFirma.seleccionarCertificado(fixture.urlCentinela("a.cer"))
        fixture.eFirma.seleccionarLlave(fixture.urlCentinela("a.key"))
        const enviar = findChild(dialogo.contentItem, "botonEnviarEFirma")
        verify(!enviar.enabled) // Registrar exige tambien la contrasena
        dialogo.campoContrasena.forceActiveFocus()
        escribir("secreta")
        verify(enviar.enabled)
        keyClick(Qt.Key_Return)
        compare(dialogo.campoContrasena.text, "")
        compare(fixture.numRegistros(), 1)
        compare(fixture.eFirma.fase, EFirmaFormViewModel.Validando)
        return dialogo
    }

    function test_validandoNavegarDescartaRespuestaTardia() {
        const r = paginaCon([{ rfc: "AAA010101AAA", nombre: "A", activo: true }], ["SinCredencial"])
        const dialogo = registrarEnValidando(r)
        fixture.app.mostrarLista() // navega con la promesa pendiente
        compare(fixture.app.pagina, AppViewModel.Lista)
        compare(fixture.eFirma.fase, EFirmaFormViewModel.Capturando)
        fixture.resolverImportacion(0, "ContrasenaIncorrecta", "Contrasena")
        wait(0)
        compare(fixture.eFirma.fase, EFirmaFormViewModel.Capturando) // no se aplica
        compare(fixture.eFirma.errorKey, "")
        compare(dialogo.campoContrasena.text, "")
        compare(fixture.eFirma.nombreCertificado, "")
    }

    function test_validandoCambiarPerfilDescartaRespuestaTardia() {
        const r = paginaCon([
            { rfc: "AAA010101AAA", nombre: "A", activo: true },
            { rfc: "BBB010101BBB", nombre: "B", activo: true },
        ], ["SinCredencial", "SinCredencial"])
        const dialogo = registrarEnValidando(r)
        verify(fixture.perfiles.seleccionar(r.ids[1])) // otro perfil durante la validacion
        tryCompare(dialogo, "visible", false)
        compare(fixture.eFirma.fase, EFirmaFormViewModel.Capturando)
        fixture.resolverImportacion(0, "", "") // exito tardio del perfil A
        wait(0)
        compare(fixture.eFirma.fase, EFirmaFormViewModel.Capturando)
        compare(dialogo.campoContrasena.text, "")
        // El dialogo de B arranca limpio.
        mouseClick(findChild(r.pagina, "botonEFirma"))
        tryCompare(dialogo, "opened", true)
        compare(fixture.eFirma.perfilId, r.ids[1])
        compare(fixture.eFirma.fase, EFirmaFormViewModel.Capturando)
        compare(fixture.eFirma.nombreCertificado, "")
        compare(dialogo.campoContrasena.text, "")
    }
}
