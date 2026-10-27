import QtQuick
import QtTest
import SatCfdiDownloader

// RespaldoPage (T025.6) sobre RespaldoViewModel real y fakes de los puertos
// (FixtureRespaldo): crear contrasena con clave de recuperacion de unica
// visualizacion (campos vaciados, Escape no la cierra, Copiar e Imprimir…),
// Respaldar ahora con progreso y aviso de copias sin registro, restaurar
// (verificar, cancelar con Escape, resumen autenticado, confirmar, sin
// relanzamiento) y descartar datos previos con confirmacion. Datos
// sinteticos.
TestCase {
    id: caso
    name: "RespaldoPage"
    when: windowShown
    visible: true
    width: 960
    height: 640

    Component {
        id: paginaComp
        RespaldoPage {
            app: fixtureFacturas.app
            respaldo: fixtureRespaldo.respaldo
        }
    }

    // T024.4: pagina en una ventana propia del tamano indicado.
    Component {
        id: ventanaComp
        Window {
            id: ventana
            visible: true
            readonly property alias pagina: paginaEnVentana
            RespaldoPage {
                id: paginaEnVentana
                width: ventana.width
                height: ventana.height
                app: fixtureFacturas.app
                respaldo: fixtureRespaldo.respaldo
            }
        }
    }

    function cleanup() {
        // T024.1: barrido de vocabulario del estado final (Diagnostico excluido).
        const fallasVocabulario = barrido.revisarVentana(caso)
        verify(fallasVocabulario.length === 0, fallasVocabulario.join("\n"))
    }

    function init() {
        failOnWarning(/^(?!Populating font family aliases took).*/)
        wait(0)
    }

    function pagina(configurado, conImpresion) {
        fixtureRespaldo.reiniciar(0, configurado === true, conImpresion === true)
        const p = createTemporaryObject(paginaComp, caso, { width: caso.width, height: caso.height })
        verify(p)
        wait(0)
        return p
    }

    function escribir(campo, texto) {
        campo.forceActiveFocus()
        campo.text = texto
    }

    function test_crearContrasenaYClaveDeUnicaVisualizacion() {
        const p = pagina(false, true)
        const boton = findChild(p, "botonContrasena")
        compare(boton.text, "Crear contraseña…")
        mouseClick(boton)
        const d = p.dialogoContrasena
        tryVerify(() => d.opened)
        verify(d.campoNueva.activeFocus)
        verify(!d.campoVerificacion.visible)
        compare(d.campoNueva.echoMode, TextInput.Password)

        // No coinciden: error y campos vacios.
        escribir(d.campoNueva, "contrasena-sintetica-1")
        escribir(d.campoConfirmacion, "otra")
        mouseClick(findChild(d.contentItem, "dialogoContrasenaConfirmar"))
        compare(d.campoNueva.text, "")
        compare(d.campoConfirmacion.text, "")
        verify(findChild(d.contentItem, "errorContrasena").visible)
        compare(fixtureRespaldo.llamadasContrasena(), 0)

        escribir(d.campoNueva, "contrasena-sintetica-1")
        escribir(d.campoConfirmacion, "contrasena-sintetica-1")
        keyClick(Qt.Key_Return)
        compare(d.campoNueva.text, "")
        compare(d.campoConfirmacion.text, "")
        compare(fixtureRespaldo.llamadasContrasena(), 1)
        verify(fixtureRespaldo.ultimaContrasenaEs("contrasena-sintetica-1"))

        fixtureRespaldo.responderContrasena()
        const clave = p.dialogoClave
        tryVerify(() => clave.opened)
        tryVerify(() => !d.visible)
        compare(findChild(clave.contentItem, "textoClaveRecuperacion").text, "ABCD-EFGH-JKMN-PQRS")
        verify(findChild(clave.contentItem, "avisoCopiasClave").visible)
        // R3: no promete "una sola vez" (se repite hasta confirmar).
        verify(clave.descripcionAccesible.indexOf("No se volverá a mostrar después de que confirmes que la guardaste") >= 0)
        verify(p.dialogoAbierto)

        // B5: nombre accesible deletreado por grupos.
        compare(findChild(clave.contentItem, "textoClaveRecuperacion").Accessible.name,
                "Clave de recuperación: A B C D, E F G H, J K M N, P Q R S")
        const aviso = findChild(clave.contentItem, "avisoCopiasClave").descripcion
        verify(aviso.indexOf("90 segundos") >= 0)
        verify(aviso.indexOf("Guardar como PDF") >= 0)

        // Escape no cierra (la clave no se vuelve a mostrar).
        keyClick(Qt.Key_Escape)
        wait(0)
        verify(clave.opened)

        mouseClick(findChild(clave.contentItem, "botonCopiarClave"))
        compare(fixtureRespaldo.copias, 1)

        const imprimir = findChild(clave.contentItem, "botonImprimirClave")
        verify(imprimir.visible)
        mouseClick(imprimir)
        compare(fixtureRespaldo.impresiones, 1)
        compare(findChild(clave.contentItem, "confirmacionClave").text, "Clave enviada a la impresora.")

        mouseClick(findChild(clave.contentItem, "botonClaveGuardada"))
        tryVerify(() => !clave.visible)
        compare(clave.clave, "")
        // El view model olvido la clave.
        verify(!fixtureRespaldo.respaldo.hayClavePendiente)
        verify(!fixtureRespaldo.respaldo.copiarClaveRecuperacion())
        compare(findChild(p, "botonContrasena").text, "Cambiar contraseña…")
    }

    // D23: minimo 12 caracteres.
    function test_contrasenaCortaSeRechaza() {
        const p = pagina(false, false)
        mouseClick(findChild(p, "botonContrasena"))
        const d = p.dialogoContrasena
        tryVerify(() => d.opened)
        verify(findChild(d.contentItem, "ayudaLongitud").visible)
        escribir(d.campoNueva, "corta")
        escribir(d.campoConfirmacion, "corta")
        keyClick(Qt.Key_Return)
        compare(fixtureRespaldo.llamadasContrasena(), 0)
        compare(findChild(d.contentItem, "errorContrasena").descripcion,
                "La contraseña debe tener al menos 12 caracteres.")
        compare(d.campoNueva.text, "")
        tryVerify(() => d.campoNueva.activeFocus)
        keyClick(Qt.Key_Escape)
        tryVerify(() => !d.visible)
    }

    // H2: la pagina se crea con una clave pendiente: el dialogo se abre solo.
    function test_clavePendienteAlCargarLaPagina() {
        fixtureRespaldo.reiniciar(0, false, false)
        fixtureRespaldo.respaldo.crearContrasena("contrasena-sintetica-1", "contrasena-sintetica-1")
        fixtureRespaldo.responderContrasena()
        tryVerify(() => fixtureRespaldo.respaldo.hayClavePendiente)
        const p = createTemporaryObject(paginaComp, caso, { width: caso.width, height: caso.height })
        tryVerify(() => p.dialogoClave.opened)
        compare(p.dialogoClave.clave, "ABCD-EFGH-JKMN-PQRS")
        mouseClick(findChild(p.dialogoClave.contentItem, "botonClaveGuardada"))
        tryVerify(() => !p.dialogoClave.visible)
        verify(!fixtureRespaldo.respaldo.hayClavePendiente)
    }

    function test_sinImpresionOcultaImprimir() {
        const p = pagina(false, false)
        mouseClick(findChild(p, "botonContrasena"))
        const d = p.dialogoContrasena
        tryVerify(() => d.opened)
        escribir(d.campoNueva, "contrasena-sintetica-1")
        escribir(d.campoConfirmacion, "contrasena-sintetica-1")
        mouseClick(findChild(d.contentItem, "dialogoContrasenaConfirmar"))
        fixtureRespaldo.responderContrasena()
        tryVerify(() => p.dialogoClave.opened)
        verify(!findChild(p.dialogoClave.contentItem, "botonImprimirClave").visible)
        mouseClick(findChild(p.dialogoClave.contentItem, "botonClaveGuardada"))
        tryVerify(() => !p.dialogoClave.visible)
    }

    function test_respaldarAhoraConProgresoYCopiasSinRegistro() {
        const p = pagina(true, false)
        compare(findChild(p, "textoCarpeta").text, "Las copias se guardan en «Respaldos».")
        const boton = findChild(p, "botonRespaldarAhora")
        verify(boton.enabled)
        mouseClick(boton)
        compare(fixtureRespaldo.respaldos(), 1)
        verify(!boton.enabled)
        fixtureRespaldo.progresoRespaldo(25, 100)
        verify(findChild(p, "progresoRespaldo").visible)
        compare(findChild(p, "faseRespaldo").text, "Cifrando y copiando a la carpeta…")
        compare(findChild(p, "barraRespaldo").value, 0.25)

        fixtureRespaldo.terminarRespaldoConCopiasSinRegistro(2)
        tryVerify(() => !findChild(p, "progresoRespaldo").visible)
        const resultado = findChild(p, "resultadoRespaldo")
        verify(resultado.visible)
        verify(resultado.titulo.indexOf("copiado a la carpeta") >= 0)
        verify(resultado.titulo.toLowerCase().indexOf("subid") < 0)
        const copias = findChild(p, "avisoCopiasSinRegistro")
        verify(copias.visible)
        verify(copias.descripcion.indexOf("Hay 2 copias") === 0)
    }

    function test_restaurarVerificarCancelarYConfirmar() {
        const p = pagina(false, false)
        // Al final de la pagina (fuera de la vista en 960x640).
        findChild(p, "botonRestaurar").clicked()
        const d = p.dialogoRestaurar
        tryVerify(() => d.opened)
        const verificar = findChild(d.contentItem, "botonVerificarRestaurar")
        verify(!verificar.enabled) // sin archivo

        // El FileDialog nativo no se usa en offscreen: se fija el archivo.
        d.archivo = "file:///Users/usuario/Respaldos/ArchivoFiscal-prueba.afbak"
        d.nombreArchivo = "ArchivoFiscal-prueba.afbak"
        escribir(d.campoSecreto, "contrasena-sintetica-1")
        keyClick(Qt.Key_Return)
        compare(d.campoSecreto.text, "")
        compare(fixtureRespaldo.preparaciones(), 1)
        verify(fixtureRespaldo.ultimoSecretoEs("contrasena-sintetica-1"))
        tryVerify(() => findChild(d.contentItem, "progresoRestaurar").visible)
        tryVerify(() => findChild(d.contentItem, "botonCancelarVerificacion").activeFocus)

        // Escape cancela la verificacion sin cerrar el dialogo.
        keyClick(Qt.Key_Escape)
        compare(fixtureRespaldo.cancelaciones(), 1)
        tryCompare(d, "estado", RespaldoViewModel.Inactiva)
        verify(d.opened)
        verify(!findChild(d.contentItem, "errorRestaurar").visible)

        escribir(d.campoSecreto, "contrasena-sintetica-1")
        mouseClick(verificar)
        fixtureRespaldo.responderPreparacion()
        tryCompare(d, "estado", RespaldoViewModel.Preparada)
        verify(findChild(d.contentItem, "resumenRestaurar").visible)
        compare(findChild(d.contentItem, "idRestaurar").text, "0011-2233-4455-6677-8899-AABB-CCDD-EEFF")
        verify(findChild(d.contentItem, "fechaRestaurar").text.length > 0)
        verify(findChild(d.contentItem, "avisoConsecuenciasRestaurar").visible)
        const restaurar = findChild(d.contentItem, "botonRestaurarReiniciar")
        tryVerify(() => restaurar.activeFocus)

        mouseClick(restaurar)
        const confirmacion = d.dialogoConfirmacion
        tryVerify(() => confirmacion.opened)
        verify(p.dialogoAbierto)
        mouseClick(findChild(confirmacion.contentItem, "confirmacionRestaurarConfirmar"))
        compare(fixtureRespaldo.solicitudes(), 1)
        tryCompare(d, "estado", RespaldoViewModel.Solicitando)
        fixtureRespaldo.responderSolicitud(1) // SinRelanzamiento
        tryCompare(d, "estado", RespaldoViewModel.SinRelanzamiento)
        const aviso = findChild(d.contentItem, "resultadoSolicitudRestaurar")
        verify(aviso.visible)
        compare(aviso.titulo, "Abre de nuevo la aplicación")
        // Ya no se cierra.
        keyClick(Qt.Key_Escape)
        wait(0)
        verify(d.opened)
    }

    function test_cerrarConPreparacionLaDescarta() {
        const p = pagina(false, false)
        // Al final de la pagina (fuera de la vista en 960x640).
        findChild(p, "botonRestaurar").clicked()
        const d = p.dialogoRestaurar
        tryVerify(() => d.opened)
        d.archivo = "file:///Users/usuario/a.afbak"
        d.nombreArchivo = "a.afbak"
        escribir(d.campoSecreto, "x")
        keyClick(Qt.Key_Return)
        fixtureRespaldo.responderPreparacion()
        tryCompare(d, "estado", RespaldoViewModel.Preparada)
        keyClick(Qt.Key_Escape)
        tryVerify(() => !d.visible)
        compare(fixtureRespaldo.descartesRestauracion(), 1)
        compare(fixtureRespaldo.respaldo.estadoRestauracion, RespaldoViewModel.Inactiva)
    }

    function test_descartarDatosPreviosConConfirmacion() {
        const p = pagina(false, false)
        const seccion = findChild(p, "seccionDatosPrevios")
        verify(!seccion.visible)
        fixtureRespaldo.responderDatosPrevios(true)
        tryVerify(() => seccion.visible)
        mouseClick(findChild(p, "botonDescartarDatosPrevios"))
        const c = p.confirmacionDescarte
        tryVerify(() => c.opened)
        // Cancelar es el foco inicial.
        verify(findChild(c.contentItem, "confirmacionDescarteCancelar").activeFocus)
        mouseClick(findChild(c.contentItem, "confirmacionDescarteConfirmar"))
        compare(fixtureRespaldo.descartesDatosPrevios(), 1)
        verify(findChild(p, "botonDescartarDatosPrevios").cargando)
    }

    // ---- T024.4: alto limitado a la ventana (resumen de restauracion) ----

    function test_restaurarAltoLimitadoALaVentana_data() {
        return [
            // 540 y 420 (minimo de la ventana): el resumen cabe o se limita
            // segun la fuente; 320 fuerza el limite; 1280x800 no lo limita.
            { tag: "960x540", ancho: 960, alto: 540 },
            { tag: "960x420", ancho: 960, alto: 420 },
            { tag: "960x320", ancho: 960, alto: 320, limitado: true },
            { tag: "1280x800", ancho: 1280, alto: 800, limitado: false }
        ]
    }

    function test_restaurarAltoLimitadoALaVentana(fila) {
        fixtureRespaldo.reiniciar(0, false, false)
        const w = createTemporaryObject(ventanaComp, caso, { width: fila.ancho, height: fila.alto })
        verify(w)
        w.requestActivate()
        tryVerify(() => w.active)
        const p = w.pagina
        findChild(p, "botonRestaurar").clicked()
        const d = p.dialogoRestaurar
        tryVerify(() => d.opened)
        d.archivo = "file:///Users/usuario/Respaldos/ArchivoFiscal-prueba.afbak"
        d.nombreArchivo = "ArchivoFiscal-prueba.afbak"
        escribir(d.campoSecreto, "contrasena-sintetica-1")
        d.verificar()
        fixtureRespaldo.responderPreparacion()
        tryCompare(d, "estado", RespaldoViewModel.Preparada)
        const restaurar = findChild(d.contentItem, "botonRestaurarReiniciar")
        tryVerify(() => restaurar.activeFocus)
        waitForRendering(d.contentItem)

        const flick = d.desplazable
        const limitado = d.implicitHeight > w.height - 32
        if (fila.limitado !== undefined)
            compare(limitado, fila.limitado, "limite esperado")
        if (!limitado) {
            compare(d.height, d.implicitHeight)
            verify(!d.desbordado)
            verify(!flick.clip)
            verify(!findChild(d.contentItem, "dialogoRestaurarBarra").visible)
        } else {
            compare(d.height, w.height - 32)
            verify(d.y >= 16 && d.y + d.height <= w.height - 16)
            verify(d.desbordado)
            verify(findChild(d.contentItem, "dialogoRestaurarBarra").visible)
            verify(flick.contentHeight > flick.height)
            mouseWheel(flick, flick.width / 2, flick.height / 2, 0, -240)
            tryVerify(() => flick.contentY > 0)
        }
        // Botones fijos al pie, dentro de la ventana y pulsables.
        const pos = restaurar.mapToItem(null, 0, 0)
        verify(pos.y >= 0 && pos.y + restaurar.height <= w.height,
               "Restaurar dentro de la ventana: " + pos.y + "+" + restaurar.height + " en " + w.height)
        const descartar = findChild(d.contentItem, "botonDescartarRestaurar")
        verify(descartar.mapToItem(null, 0, 0).y + descartar.height <= w.height)
        wait(Qt.styleHints.mouseDoubleClickInterval + 20)
        mouseClick(restaurar)
        const confirmacion = d.dialogoConfirmacion
        tryVerify(() => confirmacion.opened)
        confirmacion.close()
        tryVerify(() => !confirmacion.visible)
        wait(Qt.styleHints.mouseDoubleClickInterval + 20)
        mouseClick(descartar)
        tryVerify(() => !d.visible)
        compare(fixtureRespaldo.descartesRestauracion(), 1)
        w.destroy()
        caso.Window.window.requestActivate()
        tryVerify(() => caso.Window.window.active)
    }
}
