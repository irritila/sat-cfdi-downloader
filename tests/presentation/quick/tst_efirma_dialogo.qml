import QtQuick
import QtTest
import SatCfdiDownloader

// EFirmaDialogo (T005.1): basenames, envio sincrono con limpieza inmediata de
// la contrasena, cancelacion, cierre, foco por origen de error y teclado.
TestCase {
    id: caso
    name: "EFirmaDialogo"
    when: windowShown
    width: 800
    height: 600

    readonly property string perfilId: "6f1c2b3a-4d5e-4f60-8a1b-2c3d4e5f6071"

    Component {
        id: dialogoComp
        EFirmaDialogo {
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

    function abrir(esReemplazo) {
        fixture.eFirma.iniciar(caso.perfilId, "AAA010101AAA", esReemplazo)
        const d = createTemporaryObject(dialogoComp, caso)
        verify(d)
        d.open()
        tryCompare(d, "opened", true)
        return d
    }

    function elegirArchivos() {
        // El selector nativo no se automatiza: se entrega la QUrl al VM.
        fixture.eFirma.seleccionarCertificado(fixture.urlCentinela("mi_efirma.cer"))
        fixture.eFirma.seleccionarLlave(fixture.urlCentinela("mi_efirma.key"))
    }

    function escribirContrasena(d, texto) {
        d.campoContrasena.forceActiveFocus()
        keyClick(Qt.Key_A, Qt.ControlModifier) // reemplaza contenido previo
        keyClick(Qt.Key_Backspace)
        escribir(texto)
        compare(d.campoContrasena.text, texto)
    }

    function textosVisibles(item, acumulado) {
        if (item.text !== undefined && typeof item.text === "string")
            acumulado.push(item.text)
        for (let i = 0; i < item.children.length; ++i)
            textosVisibles(item.children[i], acumulado)
        return acumulado
    }

    function test_basenamesYEnvioLimpiaContrasena() {
        const d = abrir(false)
        elegirArchivos()
        tryCompare(findChild(d.contentItem, "nombreCertificado"), "text", "mi_efirma.cer")
        compare(findChild(d.contentItem, "nombreLlave").text, "mi_efirma.key")
        // Ningun texto visible contiene el directorio (solo basenames).
        const textos = textosVisibles(d.contentItem, [])
        for (let i = 0; i < textos.length; ++i)
            verify(textos[i].indexOf("CENTINELA_DIR") < 0, textos[i])

        escribirContrasena(d, "secreta123")
        const enviar = findChild(d.contentItem, "botonEnviarEFirma")
        verify(enviar.enabled)
        mouseClick(enviar)
        // Mismo manejador: el control queda vacio al regresar de enviar().
        compare(d.campoContrasena.text, "")
        compare(fixture.numRegistros(), 1)
        compare(fixture.largoContrasena(0), 10)
        verify(fixture.registroConRutas(0))
        verify(!fixture.registroEsReemplazo(0))
        compare(fixture.eFirma.fase, EFirmaFormViewModel.Validando)
        verify(!enviar.enabled) // sin doble envio
        verify(findChild(d.contentItem, "estadoValidandoEFirma").visible)

        fixture.resolverImportacion(0, "", "")
        tryCompare(fixture.eFirma, "fase", EFirmaFormViewModel.Exito)
        tryVerify(() => findChild(d.contentItem, "exitoEFirma").visible)
        tryVerify(() => findChild(d.contentItem, "botonCerrarEFirma").activeFocus)
    }

    function test_enterEnContrasenaEnviaYLimpia() {
        const d = abrir(false)
        elegirArchivos()
        escribirContrasena(d, "otra")
        keyClick(Qt.Key_Return)
        compare(d.campoContrasena.text, "")
        compare(fixture.numRegistros(), 1)
        compare(fixture.largoContrasena(0), 4)
    }

    function test_cancelarLimpia() {
        const d = abrir(false)
        elegirArchivos()
        escribirContrasena(d, "secreta")
        mouseClick(findChild(d.contentItem, "botonCancelarEFirma"))
        compare(d.campoContrasena.text, "")
        tryCompare(d, "visible", false)
        compare(fixture.eFirma.nombreCertificado, "")
        compare(fixture.numRegistros(), 0)
    }

    function test_escapeYCierreLimpian() {
        const d = abrir(false)
        escribirContrasena(d, "secreta")
        keyClick(Qt.Key_Escape)
        tryCompare(d, "visible", false)
        compare(d.campoContrasena.text, "")

        d.open()
        tryCompare(d, "opened", true)
        escribirContrasena(d, "secreta2")
        d.close()
        tryCompare(d, "visible", false)
        compare(d.campoContrasena.text, "")
    }

    function test_errorEnfocaCampoPorOrigen_data() {
        return [
            { tag: "contrasena", categoria: "ContrasenaIncorrecta", origen: "Contrasena", foco: "campoContrasena",
              fila: "filaContrasena", error: "errorContrasena" },
            { tag: "certificado", categoria: "FormatoInvalido", origen: "Certificado", foco: "botonElegirCertificado",
              fila: "filaCertificado", error: "errorCertificado" },
            { tag: "llave", categoria: "ArchivoIlegible", origen: "Llave", foco: "botonElegirLlave",
              fila: "filaLlave", error: "errorLlave" },
            { tag: "pareja", categoria: "ParejaIncompatible", origen: "", foco: "botonCancelarEFirma",
              fila: "", error: "errorEFirma" },
        ]
    }

    function test_errorEnfocaCampoPorOrigen(fila) {
        const d = abrir(false)
        elegirArchivos()
        escribirContrasena(d, "mala")
        mouseClick(findChild(d.contentItem, "botonEnviarEFirma"))
        compare(d.campoContrasena.text, "")
        fixture.resolverImportacion(0, fila.categoria, fila.origen)
        tryCompare(fixture.eFirma, "fase", EFirmaFormViewModel.Error)
        // El error va bajo el campo culpable (o en el aviso general si no hay campo).
        const error = findChild(d.contentItem, fila.error)
        tryVerify(() => error.visible)
        verify(error.Accessible.role === Accessible.AlertMessage)
        verify(error.Accessible.name.indexOf("Error") === 0)
        verify(error.Accessible.name.indexOf(fixture.eFirma.errorMessage) > 0)
        verify(error.Accessible.name.indexOf("CENTINELA") < 0)
        if (fila.fila.length > 0) {
            compare(findChild(d.contentItem, fila.fila).error, fixture.eFirma.errorMessage)
            verify(!findChild(d.contentItem, "errorEFirma").visible)
        }
        tryVerify(() => findChild(d.contentItem, fila.foco).activeFocus, 2000, fila.foco)
        // Tras el fallo la seleccion queda vacia y se pide elegir de nuevo.
        const enviar = findChild(d.contentItem, "botonEnviarEFirma")
        compare(fixture.eFirma.nombreCertificado, "")
        compare(fixture.eFirma.nombreLlave, "")
        compare(findChild(d.contentItem, "nombreCertificado").text, "Ningún archivo")
        compare(findChild(d.contentItem, "nombreLlave").text, "Ningún archivo")
        compare(d.campoContrasena.text, "")
        verify(findChild(d.contentItem, "avisoReintentoEFirma").visible)
        verify(!enviar.enabled)

        // Reintento con archivos nuevos.
        elegirArchivos()
        escribirContrasena(d, "buena")
        tryVerify(() => enviar.enabled)
        mouseClick(enviar)
        compare(fixture.numRegistros(), 2)
        compare(fixture.largoContrasena(1), 5)
        compare(fixture.eFirma.fase, EFirmaFormViewModel.Validando)
        verify(!findChild(d.contentItem, "avisoReintentoEFirma").visible)
    }

    function test_registrarExigeContrasena() {
        const d = abrir(false)
        const enviar = findChild(d.contentItem, "botonEnviarEFirma")
        const faltante = findChild(d.contentItem, "faltanteEFirma")
        verify(!enviar.enabled)
        compare(faltante.text, "Elige el certificado (.cer).")
        elegirArchivos()
        verify(!enviar.enabled) // falta la contrasena
        verify(faltante.visible)
        compare(faltante.text, "Escribe la contraseña de la llave privada.")
        escribirContrasena(d, "x")
        verify(enviar.enabled)
        verify(!faltante.visible)
        keyClick(Qt.Key_Backspace)
        compare(d.campoContrasena.text, "")
        verify(!enviar.enabled)
        compare(fixture.numRegistros(), 0)
    }

    function test_reemplazoMensajeAnteriorSigue() {
        const d = abrir(true)
        verify(d.title.indexOf("Reemplazar") === 0)
        elegirArchivos()
        escribirContrasena(d, "secreta")
        mouseClick(findChild(d.contentItem, "botonEnviarEFirma"))
        verify(fixture.registroEsReemplazo(0))
        fixture.resolverReemplazo(0, "RfcNoCoincide")
        tryCompare(fixture.eFirma, "fase", EFirmaFormViewModel.Error)
        verify(fixture.eFirma.errorMessage.indexOf("anterior sigue registrada") >= 0)
    }

    function test_tecladoYAccesibilidad() {
        const d = abrir(false)
        tryVerify(() => findChild(d.contentItem, "botonElegirCertificado").activeFocus)
        keyClick(Qt.Key_Tab)
        tryVerify(() => findChild(d.contentItem, "botonElegirLlave").activeFocus)
        keyClick(Qt.Key_Tab)
        tryVerify(() => d.campoContrasena.activeFocus)
        compare(d.campoContrasena.echoMode, TextInput.Password)
        const campo = findChild(d.contentItem, "campoContrasena")
        verify(campo.Accessible.name.length > 0)
        for (const nombre of ["botonElegirCertificado", "botonElegirLlave", "botonCancelarEFirma",
                              "botonEnviarEFirma"]) {
            verify(findChild(d.contentItem, nombre).Accessible.name.length > 0, nombre)
        }
        verify(d.contentItem.Accessible.name.indexOf("AAA010101AAA") > 0)
        compare(findChild(d.contentItem, "subtituloEFirma").text, "AAA010101AAA")
    }

    function test_destruccionDescartaSeleccion() {
        const d = abrir(false)
        elegirArchivos()
        escribirContrasena(d, "secreta")
        compare(fixture.eFirma.nombreCertificado, "mi_efirma.cer")
        d.destroy()
        wait(0)
        // El VM no conserva seleccion reutilizable: un envio con URLs vacias no envia.
        compare(fixture.eFirma.nombreCertificado, "")
        compare(fixture.eFirma.nombreLlave, "")
        verify(!fixture.eFirma.puedeEnviar)
        verify(!fixture.eFirma.enviar("", "", "x"))
        compare(fixture.numRegistros(), 0)
        compare(fixture.eFirma.campoConError, "certificado")
    }

    function test_cierreDescartaSeleccion() {
        const d = abrir(false)
        elegirArchivos()
        d.close()
        tryCompare(d, "visible", false)
        compare(fixture.eFirma.nombreCertificado, "")
        verify(!fixture.eFirma.enviar("", "", "x"))
        compare(fixture.numRegistros(), 0)
    }
}
