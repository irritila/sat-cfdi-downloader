import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import SatCfdiDownloader

// Componentes T013 (corte 2): carga, estados y nombre accesible de cada
// componente nuevo y de los actualizados, en claro y en oscuro (Theme.oscuro
// forzado). Cualquier warning falla la prueba.
TestCase {
    id: caso
    name: "Componentes"
    when: windowShown
    visible: true
    width: 960
    height: 640

    function init() {
        failOnWarning(/^(?!Populating font family aliases took).*/)
    }

    function cleanup() {
        Theme.oscuro = false
    }

    function temas() {
        return [{ tag: "claro", oscuro: false }, { tag: "oscuro", oscuro: true }]
    }

    function crear(componente, propiedades) {
        const o = createTemporaryObject(componente, caso, propiedades || {})
        verify(o, "no se pudo crear el componente")
        return o
    }

    Component { id: botonComp; BotonAccion { text: "Accion" } }
    Component { id: badgeComp; EstadoBadge { } }
    Component { id: avisoComp; AvisoEnLinea { width: 400 } }
    Component { id: archivoComp; CampoArchivo { width: 360 } }
    Component { id: textoComp; CampoTexto { width: 200 } }
    Component { id: comboComp; CampoCombo { width: 200; model: ["Emitidos", "Recibidos"] } }
    Component {
        id: formularioComp
        CampoFormulario {
            width: 500
            etiqueta: "RFC"
            ayuda: "Doce o trece caracteres."
            CampoTexto { objectName: "control"; Layout.fillWidth: true }
        }
    }
    Component { id: vacioComp; EstadoVacio { width: 400 } }
    Component { id: eventoComp; EventoHistorial { width: 600 } }
    Component { id: paqueteComp; FilaPaquete { width: 800 } }
    Component { id: perfilComp; FilaPerfil { width: 340 } }
    Component { id: solicitudComp; FilaSolicitud { width: 900 } }
    Component { id: resumenComp; ResumenEstado { width: 800 } }
    Component { id: selectorComp; SelectorSegmentado { } }
    Component { id: detalleComp; CampoDetalle { width: 500 } }
    Component { id: encabezadoComp; EncabezadoPagina { width: 800 } }
    Component {
        id: dialogoComp
        DialogoConfirmacion { title: "Eliminar solicitud"; mensaje: "Se eliminara."; prefijoNombre: "dlg" }
    }
    SignalSpy { id: espia }

    // ---- BotonAccion -------------------------------------------------
    function test_botonVariantes_data() { return temas() }
    function test_botonVariantes(fila) {
        Theme.oscuro = fila.oscuro
        const primario = crear(botonComp, { variante: "primario" })
        compare(primario.background.color, Theme.acento)
        compare(primario.colorTexto, Theme.textoSobreAcento)
        compare(primario.implicitHeight, Theme.altoControl)
        compare(primario.Accessible.name, "Accion")

        const compat = crear(botonComp, { highlighted: true })
        compare(compat.variante, "primario")

        const secundario = crear(botonComp)
        compare(secundario.background.color, Theme.controlSecundario)
        const destructivo = crear(botonComp, { variante: "destructivo" })
        compare(destructivo.colorTexto, Theme.error)
        const navegacion = crear(botonComp, { variante: "navegacion" })
        compare(navegacion.colorTexto, Theme.acentoTexto)
        compare(navegacion.icono, "chevron-left")

        const icono = crear(botonComp, { variante: "icono", icono: "trash", nombreAccesible: "Eliminar solicitud" })
        compare(icono.width, icono.height)
        compare(icono.Accessible.name, "Eliminar solicitud")

        const sm = crear(botonComp, { compacto: true })
        compare(sm.implicitHeight, 24)

        const deshabilitado = crear(botonComp, { enabled: false })
        compare(deshabilitado.opacity, 0.45)
    }

    function test_botonFocoYActivacion() {
        const boton = crear(botonComp)
        const anillo = boton.background.children[0]
        verify(!anillo.visible)
        boton.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(() => anillo.visible)
        compare(anillo.border.color, Theme.foco)
        compare(anillo.border.width, 2)
        espia.target = boton
        espia.signalName = "clicked"
        espia.clear()
        keyClick(Qt.Key_Return)
        keyClick(Qt.Key_Space)
        compare(espia.count, 2)
    }

    // ---- EstadoBadge: icono y texto para cada estado ------------------
    function test_estadoBadgeTodosLosEstados_data() { return temas() }
    function test_estadoBadgeTodosLosEstados(fila) {
        Theme.oscuro = fila.oscuro
        const claves = Object.keys(Theme.alias).concat(["Aceptada", "EnProceso"])
        for (const clave of claves) {
            const b = crear(badgeComp, { clave: clave })
            const icono = findChild(b, "iconoBadge")
            const texto = findChild(b, "textoBadge")
            compare(icono.nombre, Theme.estado(clave).icono, clave)
            verify(icono.visible, clave)
            verify(texto.text.length > 0, clave)
            compare(texto.color, Theme.estado(clave).texto, clave)
            compare(b.color, Theme.estado(clave).fondo, clave)
            compare(b.implicitHeight, Theme.altoBadge)
            verify(b.Accessible.name.indexOf("Estado: ") === 0, clave)
        }
        for (const prep of ["Verificando", "SinCredencial", "Lista", "Vencida", "NoVigenteAun",
                            "MaterialFaltante", "MaterialDanado", "EstadoNoDisponible"]) {
            const b = crear(badgeComp, { clave: prep, eFirma: true, texto: prep, contexto: "e.firma" })
            compare(findChild(b, "iconoBadge").nombre, Theme.estadoEFirma(prep).icono, prep)
            compare(b.Accessible.name, "e.firma: " + prep)
        }
        const grande = crear(badgeComp, { clave: "Terminada", grande: true })
        compare(grande.implicitHeight, 26)
        compare(findChild(grande, "iconoBadge").tamano, 16)
    }

    // ---- AvisoEnLinea --------------------------------------------------
    function test_avisoEnLinea_data() { return temas() }
    function test_avisoEnLinea(fila) {
        Theme.oscuro = fila.oscuro
        for (const v of ["progreso", "exito", "advertencia", "error", "neutro"]) {
            const a = crear(avisoComp, { variante: v, titulo: "Titulo", descripcion: "Detalle" })
            compare(a.color, Theme.tono(v).fondo, v)
            compare(a.border.color, Theme.tono(v).borde, v)
            compare(findChild(a, "tituloAviso").color, Theme.tono(v).texto, v)
        }
        const a = crear(avisoComp, { variante: "error", titulo: "No se pudo", descripcion: "Intenta de nuevo", textoAccion: "Reintentar" })
        compare(a.Accessible.role, Accessible.AlertMessage)
        compare(a.Accessible.name, "No se pudo. Intenta de nuevo")
        espia.target = a
        espia.signalName = "accionSolicitada"
        espia.clear()
        mouseClick(findChild(a, "accionAviso"))
        compare(espia.count, 1)
        const silencioso = crear(avisoComp, { anunciar: false, titulo: "Hecho" })
        compare(silencioso.Accessible.role, Accessible.StaticText)
    }

    // ---- CampoArchivo, CampoTexto, CampoCombo y CampoFormulario -------
    function test_campoArchivo_data() { return temas() }
    function test_campoArchivo(fila) {
        Theme.oscuro = fila.oscuro
        const c = crear(archivoComp, { objectNameNombre: "nombre", objectNameBoton: "boton",
                                       nombreBoton: "Elegir certificado (.cer)" })
        const nombre = findChild(c, "nombre")
        compare(nombre.text, c.textoVacio)
        compare(nombre.color, Theme.textoSecundario)
        const boton = findChild(c, "boton")
        compare(boton.Accessible.name, "Elegir certificado (.cer)")
        compare(boton.Accessible.description, c.textoVacio)
        c.nombreArchivo = "demo.cer"
        compare(nombre.text, "demo.cer")
        compare(nombre.color, Theme.texto)
        compare(boton.Accessible.description, "demo.cer")
        espia.target = c
        espia.signalName = "elegirSolicitado"
        espia.clear()
        mouseClick(boton)
        compare(espia.count, 1)
        c.conError = true
        compare(c.children[0].border.color, Theme.error)
        c.enabled = false
        compare(c.opacity, 0.5)
    }

    function test_campoTextoYCombo_data() { return temas() }
    function test_campoTextoYCombo(fila) {
        Theme.oscuro = fila.oscuro
        const t = crear(textoComp)
        compare(t.implicitHeight, Theme.altoControl)
        compare(t.background.color, Theme.controlFondo)
        compare(t.background.border.color, Theme.borde)
        t.forceActiveFocus()
        tryCompare(t.background.border, "color", Theme.foco)
        t.conError = true
        compare(t.background.border.color, Theme.error)
        compare(t.background.border.width, 2)
        const lectura = crear(textoComp, { soloLectura: true, text: "XAXX010101000" })
        verify(lectura.readOnly)
        compare(lectura.background.color, Theme.superficieSeccion)

        const combo = crear(comboComp)
        compare(combo.displayText, "Emitidos")
        compare(combo.indicator.nombre, "chevron-up-down")
        compare(combo.background.color, Theme.controlFondo)
    }

    function test_campoFormulario() {
        const f = crear(formularioComp)
        verify(findChild(f, "ayudaCampo").visible)
        verify(!findChild(f, "errorCampo").visible)
        compare(f.descripcionAccesible, "Doce o trece caracteres.")
        f.error = "El RFC no es válido."
        verify(!findChild(f, "ayudaCampo").visible)
        const error = findChild(f, "errorCampo")
        verify(error.visible)
        compare(error.Accessible.role, Accessible.AlertMessage)
        compare(error.Accessible.name, "Error en RFC: El RFC no es válido.")
        compare(f.descripcionAccesible, "El RFC no es válido.")
        verify(findChild(f, "control") !== null)
        compare(findChild(f, "etiquetaCampo").text, "RFC")
    }

    // ---- EstadoVacio ---------------------------------------------------
    function test_estadoVacio_data() { return temas() }
    function test_estadoVacio(fila) {
        Theme.oscuro = fila.oscuro
        const v = crear(vacioComp, { titulo: "No hay solicitudes", descripcion: "Crea una nueva solicitud." })
        compare(findChild(v, "tituloVacio").Accessible.role, Accessible.Heading)
        compare(v.Accessible.name, "No hay solicitudes. Crea una nueva solicitud.")
        verify(!findChild(v, "accionVacio").visible)
        const conAccion = crear(vacioComp, { variante: "error", titulo: "Error", textoAccion: "Reintentar", visible: false })
        conAccion.visible = true
        tryVerify(() => findChild(conAccion, "accionVacio").activeFocus)
        espia.target = conAccion
        espia.signalName = "accionSolicitada"
        espia.clear()
        keyClick(Qt.Key_Space)
        compare(espia.count, 1)
    }

    // ---- EventoHistorial -----------------------------------------------
    function test_eventoHistorial() {
        const instante = new Date(Date.UTC(2026, 9, 1, 16, 35))
        const usuario = crear(eventoComp, { fechaHora: instante, descripcion: "Solicitud creada", origen: "usuario" })
        compare(findChild(usuario, "fechaEvento").text, FormatoFechas.fechaHora(instante))
        compare(findChild(usuario, "origenEvento").text, "Usuario")
        compare(usuario.Accessible.name,
                FormatoFechas.fechaHora(instante).replace(",", "") + ", Solicitud creada, origen Usuario")
        const worker = crear(eventoComp, { fechaHora: instante, descripcion: "Verificación realizada", origen: "worker" })
        compare(findChild(worker, "origenEvento").text, "Automático")
    }

    // ---- FilaPaquete ---------------------------------------------------
    function test_filaPaquete_data() { return temas() }
    function test_filaPaquete(fila) {
        Theme.oscuro = fila.oscuro
        const p = crear(paqueteComp, { idPaquete: "0F1E2D3C-4B5A-4978-8796-A5B4C3D2E103_01", estado: "Descargado",
                                       metadatos: "Descargado 1 oct 2026", mensaje: "Archivo local presente",
                                       tonoMensaje: "exito", finderHabilitado: true, objectNameFinder: "finder" })
        compare(p.Accessible.name, "Paquete _01, Descargado, Archivo local presente")
        compare(findChild(p, "mensajePaquete").color, Theme.exito)
        compare(findChild(p, "nombrePaquete").font.family, Theme.familiaMono)
        const finder = findChild(p, "finder")
        verify(finder.visible && finder.enabled)
        espia.target = p
        espia.signalName = "mostrarEnFinder"
        espia.clear()
        mouseClick(finder)
        compare(espia.count, 1)

        p.finderHabilitado = false
        verify(!finder.enabled)
        p.estado = "Descargando"
        verify(!finder.visible)
        verify(findChild(p, "progresoPaquete").visible)
        p.estado = "Error"
        p.tonoMensaje = "error"
        compare(findChild(p, "mensajePaquete").color, Theme.error)
        compare(findChild(p, "badgePaquete").texto, "Error de descarga")
    }

    // T014.2: "Reintentar" por paquete y "Reintento pendiente".
    function test_filaPaqueteReintento_data() { return temas() }
    function test_filaPaqueteReintento(fila) {
        Theme.oscuro = fila.oscuro
        const p = crear(paqueteComp, { idPaquete: "ABC_01", estado: "Error", tonoMensaje: "error",
                                       mensaje: "No se pudo descargar el paquete. Puedes reintentar." })
        const boton = findChild(p, "botonReintentarPaquete")
        const pendiente = findChild(p, "reintentoPendientePaquete")
        verify(!boton.visible)
        verify(!pendiente.visible)
        p.puedeReintentar = true
        verify(boton.visible)
        compare(boton.Accessible.name, "Reintentar paquete ABC_01")
        compare(boton.implicitHeight, 24)
        espia.target = p
        espia.signalName = "reintentarSolicitado"
        espia.clear()
        mouseClick(boton)
        compare(espia.count, 1)
        p.reintentoPendiente = true
        verify(!boton.visible)
        verify(pendiente.visible)
        verify(p.Accessible.name.indexOf("Reintento pendiente") > 0)
    }

    // ---- FilaPerfil ----------------------------------------------------
    function test_filaPerfil_data() { return temas() }
    function test_filaPerfil(fila) {
        Theme.oscuro = fila.oscuro
        const f = crear(perfilComp, { rfc: "XAXX010101000", nombre: "Contribuyente de ejemplo",
                                      preparacion: "Lista", estadoTexto: "e.firma lista",
                                      disponibilidad: "Disponible para solicitudes", listo: true,
                                      vigenteHasta: new Date(2029, 0, 1), objectNameReintentar: "reintentar" })
        compare(f.Accessible.name, "XAXX010101000, Contribuyente de ejemplo, e.firma lista, Disponible para solicitudes, Vigente hasta 2029-01-01")
        compare(findChild(f, "badgePerfil").icono, "check-shield")
        verify(!findChild(f, "reintentar").visible)
        compare(f.background.color, Theme.superficie)
        f.seleccionada = true
        compare(f.background.color, Theme.seleccion)
        f.preparacion = "EstadoNoDisponible"
        const reintentar = findChild(f, "reintentar")
        verify(reintentar.visible)
        compare(reintentar.Accessible.name, "Reintentar estado de XAXX010101000")
        espia.target = f
        espia.signalName = "reintentarSolicitado"
        espia.clear()
        mouseClick(reintentar)
        compare(espia.count, 1)
        f.activo = false
        verify(findChild(f, "disponibilidadPerfil").font.italic)
    }

    // T014.3 D2: badge "Vence en N días" (advertencia, con icono y texto).
    function test_filaPerfilVencimiento_data() { return temas() }
    function test_filaPerfilVencimiento(fila) {
        Theme.oscuro = fila.oscuro
        const f = crear(perfilComp, { rfc: "XAXX010101000", preparacion: "Lista", estadoTexto: "e.firma lista",
                                      listo: true, vigenteHasta: new Date(2026, 9, 17) })
        const badge = findChild(f, "badgeVencimientoPerfil")
        verify(!badge.visible)
        verify(f.Accessible.name.indexOf("Vence") < 0)
        f.diasParaVencer = 7
        verify(badge.visible)
        compare(badge.texto, "Vence en 7 días")
        compare(badge.tono, "advertencia")
        compare(badge.icono, "calendar-exclamation")
        compare(badge.color, Theme.tonoAdvertenciaFondo)
        compare(findChild(badge, "iconoBadge").color, Theme.tonoAdvertenciaTexto)
        verify(f.Accessible.name.indexOf("Vence en 7 días") > 0)
        f.diasParaVencer = 1
        compare(badge.texto, "Vence en 1 día")
        f.diasParaVencer = 0
        compare(badge.texto, "Vence hoy")
        f.diasParaVencer = -1
        verify(!badge.visible)
    }

    // ---- FilaSolicitud -------------------------------------------------
    function test_filaSolicitud_data() { return temas() }
    function test_filaSolicitud(fila) {
        Theme.oscuro = fila.oscuro
        const creada = new Date(Date.UTC(2026, 9, 1, 16, 3))
        const s = crear(solicitudComp, { estado: "Terminada", rfc: "XAXX010101000", nombrePerfil: "Contribuyente de ejemplo",
                                         tipoDescarga: "Recibidos", fechaInicial: "2026-09-03", fechaFinal: "2026-09-03",
                                         totalPaquetes: 3, creadaEn: creada })
        compare(s.Accessible.name, "Terminada, XAXX010101000 Contribuyente de ejemplo, Recibidos, 3 sep 2026, 3 paquetes, 0 de 3 descargados, creada "
                + FormatoFechas.fechaHora(creada).replace(",", ""))
        compare(findChild(s, "periodoFila").text, "3 sep 2026")
        compare(findChild(s, "paquetesFila").text, "0/3")
        verify(s.height >= Theme.altoFila)
        compare(s.background.color, Theme.superficie)
        s.seleccionada = true
        compare(s.background.color, Theme.seleccion)
        s.totalPaquetes = 1
        verify(s.Accessible.name.indexOf("1 paquete,") > 0)
        s.fechaInicial = "2026-09-01"
        s.fechaFinal = "2026-09-30"
        compare(findChild(s, "periodoFila").text, "1–30 sep 2026")
    }

    // T014.1 D2: conteo de paquetes con barra, check y "Pendiente de descarga".
    function test_filaSolicitudProgreso_data() { return temas() }
    function test_filaSolicitudProgreso(fila) {
        Theme.oscuro = fila.oscuro
        const s = crear(solicitudComp, { estado: "Terminada", rfc: "XAXX010101000", tipoDescarga: "Emitidos",
                                         fechaInicial: "2026-09-01", fechaFinal: "2026-09-30", totalPaquetes: 3,
                                         paquetesDescargados: 0, paquetesPendientes: 0 })
        const texto = findChild(s, "paquetesFila")
        const barra = findChild(s, "barraPaquetesFila")
        const check = findChild(s, "checkPaquetesFila")
        const pendiente = findChild(s, "pendienteFila")
        // 0/3 sin paquetes Disponible ni Error (p. ej. Descargando o Vencido).
        compare(texto.text, "0/3")
        verify(barra.visible)
        compare(barra.children[0].width, 0)
        verify(!check.visible)
        verify(!pendiente.visible)
        // 2/3 con uno Disponible o Error.
        s.paquetesDescargados = 2
        s.paquetesPendientes = 1
        compare(texto.text, "2/3")
        verify(barra.visible)
        fuzzyCompare(barra.children[0].width, barra.width * 2 / 3, 0.5)
        verify(!check.visible)
        verify(pendiente.visible)
        verify(s.Accessible.name.indexOf("Terminada, Pendiente de descarga,") === 0)
        verify(s.Accessible.name.indexOf("3 paquetes, 2 de 3 descargados") > 0)
        // 3/3: check en lugar de barra.
        s.paquetesDescargados = 3
        s.paquetesPendientes = 0
        compare(texto.text, "3")
        verify(!barra.visible)
        verify(check.visible)
        verify(!pendiente.visible)
        verify(s.Accessible.name.indexOf("todos descargados") > 0)
        // Sin paquetes y sin terminar: guion.
        s.totalPaquetes = 0
        s.paquetesDescargados = 0
        s.estado = "EnProceso"
        compare(texto.text, "—")
        verify(!barra.visible)
        verify(!check.visible)
    }

    // ---- BotonCopiar (T014.1 D6) ---------------------------------------
    Component { id: copiarComp; BotonCopiar { } }
    function test_botonCopiar_data() { return temas() }
    function test_botonCopiar(fila) {
        Theme.oscuro = fila.oscuro
        const b = crear(copiarComp, { valor: "4e3b2a1c-0000-4000-8000-000000000001",
                                      descripcionValor: "Id de solicitud SAT" })
        verify(b.visible)
        compare(b.Accessible.name, "Copiar Id de solicitud SAT")
        compare(b.icono, "doc-on-doc")
        espia.target = b
        espia.signalName = "copiadoSolicitado"
        espia.clear()
        mouseClick(b)
        compare(espia.count, 1)
        compare(espia.signalArguments[0][0], true)
        verify(b.copiado)
        compare(b.icono, "checkmark")
        compare(b.Accessible.name, "Copiado")
        compare(b.textoConfirmacion, "Id de solicitud SAT copiado al portapapeles.")
        tryVerify(() => !b.copiado, 3000)
        compare(b.Accessible.name, "Copiar Id de solicitud SAT")
        b.valor = ""
        verify(!b.visible)
    }

    // ---- DialogoConfirmacion con alternativa (T014.1 D4) ---------------
    function test_dialogoAlternativa_data() { return temas() }
    function test_dialogoAlternativa(fila) {
        Theme.oscuro = fila.oscuro
        const d = crear(dialogoComp, { textoAlternativa: "Ver solicitud existente" })
        d.open()
        tryVerify(() => d.opened)
        const alternativa = findChild(d.contentItem, "dlgAlternativa")
        verify(alternativa.visible)
        compare(alternativa.Accessible.name, "Ver solicitud existente")
        tryVerify(() => findChild(d.contentItem, "dlgCancelar").activeFocus)
        // Tab recorre Cancelar -> Confirmar -> Ver solicitud existente -> Cancelar.
        keyClick(Qt.Key_Tab)
        verify(findChild(d.contentItem, "dlgConfirmar").activeFocus)
        keyClick(Qt.Key_Tab)
        verify(alternativa.activeFocus)
        keyClick(Qt.Key_Tab)
        verify(findChild(d.contentItem, "dlgCancelar").activeFocus)
        const confirmado = createTemporaryObject(spyComp, caso, { target: d, signalName: "confirmado" })
        const cancelado = createTemporaryObject(spyComp, caso, { target: d, signalName: "cancelado" })
        const alterno = createTemporaryObject(spyComp, caso, { target: d, signalName: "alternativaSolicitada" })
        mouseClick(alternativa)
        compare(alterno.count, 1)
        tryVerify(() => !d.visible)
        compare(confirmado.count, 0)
        compare(cancelado.count, 0)

        const sin = crear(dialogoComp)
        sin.open()
        tryVerify(() => sin.opened)
        verify(!findChild(sin.contentItem, "dlgAlternativa").visible)
        sin.close()
    }
    Component { id: spyComp; SignalSpy { } }

    // ---- ResumenEstado -------------------------------------------------
    function test_resumenEstado_data() { return temas() }
    function test_resumenEstado(fila) {
        Theme.oscuro = fila.oscuro
        const r = crear(resumenComp, { clave: "Terminada", titular: "Terminada: 1 de 3 paquetes descargados",
                                       descripcion: "Faltan 2 paquetes.", textoAccion: "Reintentar descarga",
                                       datos: [{ etiqueta: "Contribuyente", valor: "XAXX010101000" },
                                               { etiqueta: "Tipo · periodo", valor: "Recibidos · 3 sep 2026" },
                                               { etiqueta: "CFDI reportados", valor: "12" },
                                               { etiqueta: "Última verificación", valor: "" }] })
        compare(r.Accessible.name, "Resumen")
        compare(findChild(r, "titularResumen").Accessible.role, Accessible.Heading)
        compare(findChild(r, "badgeResumen").implicitHeight, 26)
        compare(findChild(r, "datoResumen_2").Accessible.name, "CFDI reportados: 12")
        compare(r.color, Theme.superficie)
        const accion = findChild(r, "accionResumen")
        verify(accion.enabled)
        espia.target = r
        espia.signalName = "accionSolicitada"
        espia.clear()
        mouseClick(accion)
        compare(espia.count, 1)
        r.accionHabilitada = false
        r.motivo = "Este perfil no tiene e.firma registrada."
        verify(!accion.enabled)
        verify(findChild(r, "motivoResumen").visible)
        r.mensajeResultado = "Descarga solicitada."
        const resultado = findChild(r, "resultadoResumen")
        verify(resultado.visible)
        compare(resultado.Accessible.role, Accessible.AlertMessage)
    }

    // ---- SelectorSegmentado --------------------------------------------
    function test_selectorSegmentado_data() { return temas() }
    function test_selectorSegmentado(fila) {
        Theme.oscuro = fila.oscuro
        const s = crear(selectorComp, { opciones: [{ clave: "Emitidos", texto: "Emitidos", icono: "arrow-up-right" },
                                                   { clave: "Recibidos", texto: "Recibidos", icono: "arrow-down-left" }],
                                        valor: "Emitidos", nombreAccesible: "Tipo de descarga" })
        compare(s.color, Theme.controlSecundario)
        const emitidos = findChild(s, "segmento_Emitidos")
        const recibidos = findChild(s, "segmento_Recibidos")
        verify(emitidos.Accessible.checked)
        verify(!recibidos.Accessible.checked)
        compare(emitidos.Accessible.role, Accessible.RadioButton)
        compare(emitidos.background.color, Theme.segmentoActivo)
        espia.target = s
        espia.signalName = "activado"
        espia.clear()
        emitidos.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Right)
        compare(s.valor, "Recibidos")
        compare(espia.count, 1)
        compare(espia.signalArguments[0][0], "Recibidos")
        verify(recibidos.activeFocus)
        verify(recibidos.Accessible.checked)
        keyClick(Qt.Key_Left)
        compare(s.valor, "Emitidos")
        mouseClick(recibidos)
        compare(s.valor, "Recibidos")

        const pestanas = crear(selectorComp, { variante: "pestanas", valor: "datos",
                                               opciones: [{ clave: "datos", texto: "Datos" },
                                                          { clave: "paquetes", texto: "Paquetes", contador: 3 }] })
        compare(findChild(pestanas, "segmento_datos").Accessible.role, Accessible.PageTab)
        compare(findChild(pestanas, "segmento_paquetes").Accessible.name, "Paquetes (3)")
    }

    // ---- CampoDetalle y EncabezadoPagina -------------------------------
    function test_campoDetalle() {
        const vacio = crear(detalleComp, { etiqueta: "Ultimo error", valor: "" })
        compare(vacio.Accessible.name, "Ultimo error: —")
        const mono = crear(detalleComp, { etiqueta: "Id solicitud SAT", valor: "4e80345d", mono: true })
        compare(mono.Accessible.name, "Id solicitud SAT: 4e80345d")
        compare(mono.valor, "4e80345d")
    }

    function test_encabezado_data() { return temas() }
    function test_encabezado(fila) {
        Theme.oscuro = fila.oscuro
        const e = crear(encabezadoComp, { titulo: "Solicitudes", contador: "4" })
        compare(e.implicitHeight, Theme.altoEncabezado)
        compare(e.background.color, Theme.fondo)
        verify(!e.botonRegresar.visible)
        compare(findChild(e, "tituloEncabezado").Accessible.role, Accessible.Heading)
        verify(findChild(e, "contadorEncabezado").visible)
        const sec = crear(encabezadoComp, { titulo: "Detalle", subtitulo: "XAXX010101000", mostrarRegresar: true,
                                            textoRegresar: "Solicitudes" })
        verify(sec.botonRegresar.visible)
        compare(sec.botonRegresar.variante, "navegacion")
        compare(findChild(sec, "tituloEncabezado").font.pixelSize, Theme.subtitulo.size)
        verify(findChild(sec, "subtituloEncabezado").visible)
    }

    // ---- DialogoConfirmacion -------------------------------------------
    function test_dialogoConfirmacion_data() { return temas() }
    function test_dialogoConfirmacion(fila) {
        Theme.oscuro = fila.oscuro
        const d = crear(dialogoComp, { variante: "destructivo", consecuencia: "No se modifica nada en el SAT." })
        d.open()
        tryCompare(d, "opened", true)
        compare(d.background.color, Theme.superficieElevada)
        compare(d.contentItem.Accessible.role, Accessible.Dialog)
        compare(d.contentItem.Accessible.name, "Eliminar solicitud")
        const confirmar = findChild(d.contentItem, "dlgConfirmar")
        const cancelar = findChild(d.contentItem, "dlgCancelar")
        compare(confirmar.variante, "destructivo")
        tryVerify(() => cancelar.activeFocus)
        verify(findChild(d.contentItem, "dlgConsecuencia").visible)
        verify(!findChild(d.contentItem, "dlgError").visible)
        d.procesando = true
        verify(!confirmar.enabled && !cancelar.enabled)
        verify(confirmar.cargando)
        d.procesando = false
        d.mensajeError = "No se pudo eliminar la solicitud. Intenta de nuevo."
        verify(findChild(d.contentItem, "dlgError").visible)
        keyClick(Qt.Key_Escape)
        tryCompare(d, "visible", false)
    }
}
