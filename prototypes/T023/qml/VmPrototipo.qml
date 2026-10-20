import QtQuick

import "DatosSinteticos.js" as Datos

// View model FALSO del prototipo T023.1 (no es codigo productivo). Imita lo
// que en T023.2-T023.5 daran los view models C++: perfil y mes elegidos,
// estado del mes con su franja, CFDI por direccion, historial y diagnostico.
// Las transiciones (Preparando -> lista llenandose -> Disponibles) se simulan
// con un Timer. QML de pantalla solo lee propiedades y llama funciones.
QtObject {
    id: vm

    // ---- Estado de datos ----
    property int revision: 0
    property var perfiles: Datos.perfilesIniciales()
    property int perfilIndice: 0
    property int anio: 2026
    property int mes: 9 // D7: la app abre en el mes anterior al actual
    property var catalogo: ({})
    property var historial: Datos.historialInicial()
    property var ultimoMes: ({}) // D16: ultimo mes visto por perfil en la sesion

    // ---- Estado de UI que los escenarios pueden fijar ----
    property string pagina: "facturas" // facturas | historial | perfiles | diagnostico
    property string direccion: "emitidas"
    property string busqueda: ""
    property string filtroTipo: "" // "", I, E, P, N
    property var pdf: null // CFDI con vista previa abierta
    property var diagnosticoFoco: null // { rfc, anio, mes } o null (lista general)
    property string paginaPrevia: "facturas"
    property var notificacion: null // { titulo, cuerpo, rfc, anio, mes }

    // Simulacion en curso: { rfc, anio, mes, tipo, paso }
    property var simulacion: null

    // ---- Estado derivado ----
    // Se recalcula de forma IMPERATIVA en refrescar() despues de cada cambio
    // (los datos son objetos JS mutables: las bindings no ven sus cambios).
    property bool hayPerfiles: true
    property var perfil: null
    property bool eFirmaLista: false
    property var mesInfo: null
    property string estadoMes: ""
    property var estadoMesActual: Datos.estadoMes("")
    property var franja: ({ titular: "", descripcion: "", textoAccion: "", accion: "", habilitada: true, motivo: "",
                            ocupado: false, enlace: "" })
    property var emitidasVisibles: []
    property var recibidasVisibles: []
    property var mesesDelAnio: []
    property var historialPerfil: []
    property var diagnostico: []
    readonly property string tituloMes: Datos.tituloMesAnio(vm.anio, vm.mes)

    readonly property var listaDireccion: vm.direccion === "emitidas" ? vm.emitidasVisibles : vm.recibidasVisibles
    readonly property var cfdisFiltrados: {
        const q = vm.busqueda.trim().toLowerCase()
        return vm.listaDireccion.filter(c => (vm.filtroTipo === "" || c.tipo === vm.filtroTipo)
            && (q.length === 0 || [c.emisorRfc, c.emisorNombre, c.receptorRfc, c.receptorNombre, c.uuid, c.serieFolio]
                .some(t => t.toLowerCase().indexOf(q) >= 0)))
    }

    onPerfilesChanged: vm.refrescar()
    onPerfilIndiceChanged: vm.refrescar()
    onAnioChanged: vm.refrescar()
    onMesChanged: vm.refrescar()
    onDiagnosticoFocoChanged: vm.refrescar()
    Component.onCompleted: vm.refrescar()

    function refrescar() {
        vm.revision++
        vm.hayPerfiles = vm.perfiles.length > 0
        vm.perfil = vm.hayPerfiles ? vm.perfiles[Math.min(vm.perfilIndice, vm.perfiles.length - 1)] : null
        vm.eFirmaLista = vm.perfil !== null && vm.perfil.eFirma === "Lista"
        const m = vm.mesActual()
        vm.mesInfo = m
        vm.estadoMes = m !== null ? m.estado : ""
        vm.estadoMesActual = Datos.estadoMes(vm.estadoMes)
        vm.emitidasVisibles = m !== null ? m.emitidas.slice(0, m.visE) : []
        vm.recibidasVisibles = m !== null ? m.recibidas.slice(0, m.visR) : []
        vm.franja = vm.calcularFranja()
        vm.mesesDelAnio = vm.calcularMeses()
        vm.historialPerfil = vm.perfil !== null && vm.historial[vm.perfil.rfc] !== undefined
                             ? vm.historial[vm.perfil.rfc].slice() : []
        vm.diagnostico = vm.calcularDiagnostico()
    }

    // Meses del anio elegido para la lista lateral o el combo (D9).
    function calcularMeses() {
        const lista = []
        if (vm.perfil === null)
            return lista
        for (let m = 1; m <= 12; ++m) {
            const info = vm.mesDe(vm.perfil.rfc, vm.anio, m)
            const e = Datos.estadoMes(info.estado)
            const total = info.visE + info.visR
            lista.push({ mes: m, nombre: Datos.nombreMes(m), estado: info.estado, estadoTexto: e.texto, tono: e.tono,
                         icono: e.icono, total: total, futuro: info.estado === "Futuro",
                         nombreAccesible: Datos.nombreMes(m) + " " + vm.anio + ", " + e.texto
                                          + (info.estado === "Futuro" ? "" : ", " + total + " facturas") })
        }
        return lista
    }

    // ---- Consultas ----
    function mesDe(rfc, anio, mes) {
        const clave = Datos.clave(anio, mes)
        if (vm.catalogo[rfc] === undefined)
            vm.catalogo[rfc] = {}
        if (vm.catalogo[rfc][clave] === undefined) {
            const p = vm.perfiles.find(x => x.rfc === rfc) || { rfc: rfc, nombre: "" }
            vm.catalogo[rfc][clave] = Datos.mesInicial(p, anio, mes)
        }
        return vm.catalogo[rfc][clave]
    }

    function mesActual() {
        return vm.perfil !== null ? vm.mesDe(vm.perfil.rfc, vm.anio, vm.mes) : null
    }

    function calcularFranja() {
        const m = vm.mesActual()
        const f = { titular: "", descripcion: "", textoAccion: "", accion: "", habilitada: true, motivo: "",
                    ocupado: false, enlace: "" }
        if (m === null)
            return f
        const nombre = Datos.nombreMesAnio(m.anio, m.mes)
        switch (m.estado) {
        case "SinDescargar":
            f.titular = qsTr("Las facturas de %1 no están en este equipo").arg(nombre)
            f.descripcion = qsTr("Descarga del SAT las emitidas y recibidas del mes. Puede tardar de unos minutos a varias horas.")
            f.textoAccion = qsTr("Descargar facturas")
            f.accion = "descargar"
            f.habilitada = vm.eFirmaLista
            f.motivo = vm.eFirmaLista ? "" : qsTr("Registra la e.firma del perfil para descargar.")
            break
        case "Preparando":
            f.titular = qsTr("Preparando las facturas de %1").arg(nombre)
            f.descripcion = Datos.etapas[Math.min(m.etapa, Datos.etapas.length - 1)]
            f.ocupado = true
            break
        case "Disponibles":
            if (Datos.enCurso(m.anio, m.mes)) {
                f.titular = qsTr("Facturas disponibles hasta el %1").arg(m.hasta)
                f.descripcion = qsTr("El mes sigue en curso. Mañana podrás actualizarlo para traer las facturas nuevas.")
            } else {
                f.titular = qsTr("Facturas de %1 completas").arg(nombre)
                f.descripcion = qsTr("Emitidas y recibidas descargadas el %1.").arg(m.descargadoEn)
            }
            break
        case "Parcial":
            if (m.parcial === "enCurso") {
                f.titular = qsTr("Facturas disponibles hasta el %1").arg(m.hasta)
                f.descripcion = qsTr("El mes sigue en curso. Actualiza para traer las facturas desde esa hora hasta ahora.")
                f.textoAccion = qsTr("Actualizar mes")
                f.accion = "actualizar"
            } else if (m.parcial === "cerradoIncompleto") {
                f.titular = qsTr("Faltan facturas de %1").arg(nombre)
                f.descripcion = qsTr("Hay facturas hasta el %1. Completa el mes para traer las del resto de %2.")
                                    .arg(m.hasta).arg(Datos.meses[m.mes - 1])
                f.textoAccion = qsTr("Completar mes")
                f.accion = "completar"
            } else {
                f.titular = qsTr("Faltan las facturas recibidas de %1").arg(nombre)
                f.descripcion = qsTr("Las emitidas ya están en el equipo. Completa el mes para traer las recibidas.")
                f.textoAccion = qsTr("Completar mes")
                f.accion = "completar"
            }
            f.habilitada = vm.eFirmaLista
            f.motivo = vm.eFirmaLista ? "" : qsTr("Registra la e.firma del perfil para descargar.")
            break
        case "Atencion":
            f.titular = qsTr("No se pudieron obtener las recibidas de %1").arg(nombre)
            f.descripcion = qsTr("Un archivo que entregó el SAT está dañado. Reintenta; si el problema sigue, revisa los detalles técnicos.")
            f.textoAccion = qsTr("Reintentar")
            f.accion = "reintentar"
            f.enlace = qsTr("Ver detalles técnicos")
            f.habilitada = vm.eFirmaLista
            break
        case "Futuro":
            f.titular = qsTr("%1 aún no empieza").arg(Datos.capital(nombre))
            f.descripcion = qsTr("Podrás descargar sus facturas cuando inicie el mes.")
            break
        }
        return f
    }

    // ---- Navegacion ----
    function irA(pagina) {
        if (pagina === "diagnostico")
            vm.paginaPrevia = vm.pagina === "diagnostico" ? vm.paginaPrevia : vm.pagina
        vm.pagina = pagina
    }

    function abrirDiagnostico(foco) {
        vm.diagnosticoFoco = foco
        vm.irA("diagnostico")
    }

    function seleccionarPerfil(indice) {
        if (indice < 0 || indice >= vm.perfiles.length || indice === vm.perfilIndice)
            return
        if (vm.perfil !== null)
            vm.ultimoMes[vm.perfil.rfc] = { anio: vm.anio, mes: vm.mes }
        vm.perfilIndice = indice
        const previo = vm.ultimoMes[vm.perfiles[indice].rfc]
        vm.anio = previo !== undefined ? previo.anio : 2026
        vm.mes = previo !== undefined ? previo.mes : 9
        vm.limpiarVista()
    }

    function seleccionarMes(anio, mes) {
        vm.anio = anio
        vm.mes = mes
        vm.limpiarVista()
    }

    function moverMes(delta) {
        let m = vm.mes + delta
        let a = vm.anio
        if (m < 1) { m = 12; a -= 1 }
        if (m > 12) { m = 1; a += 1 }
        if (a < 2025 || Datos.futuro(a, m))
            return
        vm.seleccionarMes(a, m)
    }

    function cambiarAnio(delta) {
        const a = vm.anio + delta
        if (a < 2025 || a > Datos.hoy.anio)
            return
        vm.anio = a
        if (Datos.futuro(a, vm.mes))
            vm.mes = Datos.hoy.mes
        vm.limpiarVista()
    }

    function limpiarVista() {
        vm.busqueda = ""
        vm.filtroTipo = ""
        vm.pdf = null
    }

    function abrirMes(rfc, anio, mes) {
        const i = vm.perfiles.findIndex(p => p.rfc === rfc)
        if (i >= 0)
            vm.seleccionarPerfil(i)
        vm.seleccionarMes(anio, mes)
        vm.notificacion = null
        vm.pagina = "facturas"
    }

    // ---- Acciones (simuladas) ----
    function accionFranja() {
        const f = vm.franja
        if (f.accion.length === 0 || !f.habilitada)
            return
        vm.iniciarDescarga(f.accion)
    }

    // descargar | completar | actualizar | reintentar. Repetir la accion con
    // trabajo en curso no hace nada (D12).
    function iniciarDescarga(tipo) {
        const m = vm.mesActual()
        if (m === null || m.estado === "Preparando" || m.estado === "Futuro" || !vm.eFirmaLista)
            return
        if (m.emitidas.length === 0)
            m.emitidas = Datos.cfdis(vm.perfil, m.anio, m.mes, "emitidas", Datos.enCurso(m.anio, m.mes) ? Datos.hoy.dia : undefined)
        if (m.recibidas.length === 0)
            m.recibidas = Datos.cfdis(vm.perfil, m.anio, m.mes, "recibidas", Datos.enCurso(m.anio, m.mes) ? Datos.hoy.dia : undefined)
        m.estado = "Preparando"
        m.etapa = 0
        const filas = vm.historial[vm.perfil.rfc] || []
        filas.unshift({ anio: m.anio, mes: m.mes, fecha: Datos.textoAhora, resultado: "EnPreparacion", detalle: "",
                        emitidas: m.visE, recibidas: m.visR })
        vm.historial[vm.perfil.rfc] = filas
        vm.simulacion = { rfc: vm.perfil.rfc, anio: m.anio, mes: m.mes, tipo: tipo, paso: 0 }
        vm.refrescar()
        temporizador.interval = 1600
        temporizador.restart()
    }

    function avanzarSimulacion() {
        const s = vm.simulacion
        if (s === null)
            return
        const m = vm.mesDe(s.rfc, s.anio, s.mes)
        if (m.etapa < 2) {
            m.etapa += 1
            temporizador.interval = m.etapa === 1 ? 2200 : 140
        } else if (m.visE < m.emitidas.length || m.visR < m.recibidas.length) {
            m.visE = Math.min(m.emitidas.length, m.visE + 2)
            m.visR = Math.min(m.recibidas.length, m.visR + 3)
        } else {
            vm.terminarSimulacion(m, s)
            vm.refrescar()
            return
        }
        vm.refrescar()
        temporizador.restart()
    }

    function terminarSimulacion(m, s) {
        m.estado = "Disponibles"
        m.descargadoEn = Datos.textoAhora
        m.parcial = ""
        if (Datos.enCurso(m.anio, m.mes))
            m.hasta = Datos.hoy.dia + " de " + Datos.meses[m.mes - 1] + ", " + Datos.hoy.hora
        const fila = (vm.historial[s.rfc] || []).find(h => h.resultado === "EnPreparacion" && h.anio === m.anio && h.mes === m.mes)
        if (fila !== undefined) {
            fila.resultado = "Completa"
            fila.emitidas = m.emitidas.length
            fila.recibidas = m.recibidas.length
            fila.detalle = Datos.enCurso(m.anio, m.mes) ? qsTr("Hasta el %1").arg(m.hasta) : ""
        }
        vm.simulacion = null
        vm.notificacion = { titulo: qsTr("Facturas de %1 listas").arg(Datos.nombreMesAnio(m.anio, m.mes)),
                            cuerpo: qsTr("***%1 · %2 emitidas, %3 recibidas").arg(s.rfc.slice(-3))
                                        .arg(m.emitidas.length).arg(m.recibidas.length),
                            rfc: s.rfc, anio: m.anio, mes: m.mes }
    }

    function registrarEFirma(rfc) {
        const p = vm.perfiles.find(x => x.rfc === rfc)
        if (p !== undefined)
            p.eFirma = "Lista"
        vm.perfiles = vm.perfiles.slice()
        vm.refrescar()
    }

    function agregarPerfil(rfc, nombre) {
        const r = rfc.trim().toUpperCase()
        if (r.length === 0)
            return
        const lista = vm.perfiles.slice()
        lista.push({ rfc: r, nombre: nombre.trim().length > 0 ? nombre.trim() : r, eFirma: "SinCredencial" })
        vm.perfiles = lista
        vm.perfilIndice = lista.length - 1
        vm.refrescar()
    }

    function crearPerfilesEjemplo() {
        vm.perfiles = Datos.perfilesIniciales()
        vm.perfilIndice = 0
        vm.refrescar()
    }

    // ---- Diagnostico (simulado): unico lugar con terminos del servicio SAT ----
    function solicitudesDe(rfc, anio, mes) {
        const m = vm.mesDe(rfc, anio, mes)
        const id = function (dir) {
            const a = Datos.generador(Datos.semillaDe(rfc + Datos.clave(anio, mes) + dir + "sol"))
            return Datos.uuid(a)
        }
        const rango = "1–" + Datos.diasDelMes(anio, mes) + " " + Datos.mesesCortos[mes - 1] + " " + anio
        const lista = []
        const agregar = function (dir, estado, estadoPaquete, nota, rangoTexto) {
            lista.push({ direccion: dir, id: id(dir), estado: estado, rango: rangoTexto || rango, nota: nota || "",
                         paquete: estadoPaquete.length > 0 ? id(dir).substring(0, 23) + "_01" : "", estadoPaquete: estadoPaquete,
                         rfc: rfc, anio: anio, mes: mes })
        }
        switch (m.estado) {
        case "Disponibles":
            agregar("Emitidos", "Terminada", "Descargado")
            agregar("Recibidos", "Terminada", "Descargado")
            break
        case "Preparando":
            agregar("Emitidos", "EnProceso", "")
            agregar("Recibidos", "Aceptada", "")
            break
        case "Atencion":
            agregar("Emitidos", "Terminada", "Descargado")
            agregar("Recibidos", "Terminada", "Error", qsTr("ZIP ilegible: el paquete descargado no se pudo abrir (CRC incorrecto)."))
            break
        case "Parcial":
            if (m.parcial === "unaDireccion")
                agregar("Emitidos", "Terminada", "Descargado", qsTr("Solicitud anterior a T023 (sin lote de descarga)."))
            else {
                const hasta = m.parcial === "enCurso" ? "15" : "10"
                const r = "1–" + hasta + " " + Datos.mesesCortos[mes - 1] + " " + anio
                agregar("Emitidos", "Terminada", "Descargado", "", r)
                agregar("Recibidos", "Terminada", "Descargado", "", r)
            }
            break
        }
        return lista
    }

    function calcularDiagnostico() {
        if (vm.perfil === null)
            return []
        if (vm.diagnosticoFoco !== null)
            return vm.solicitudesDe(vm.diagnosticoFoco.rfc, vm.diagnosticoFoco.anio, vm.diagnosticoFoco.mes)
        let todas = []
        for (let m = 10; m >= 1; --m)
            todas = todas.concat(vm.solicitudesDe(vm.perfil.rfc, 2026, m))
        return todas
    }

    // ---- Escenarios (panel ⌘⇧E y modo capturas) ----
    function reiniciar() {
        temporizador.stop()
        vm.simulacion = null
        vm.perfiles = Datos.perfilesIniciales()
        vm.perfilIndice = 0
        vm.anio = 2026
        vm.mes = 9
        vm.catalogo = ({})
        vm.historial = Datos.historialInicial()
        vm.ultimoMes = ({})
        vm.pagina = "facturas"
        vm.direccion = "emitidas"
        vm.limpiarVista()
        vm.diagnosticoFoco = null
        vm.notificacion = null
        vm.refrescar()
    }

    property Timer temporizador: Timer {
        id: temporizador
        repeat: false
        onTriggered: vm.avanzarSimulacion()
    }
}
