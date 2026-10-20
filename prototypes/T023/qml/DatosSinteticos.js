.pragma library

// Datos sinteticos del prototipo T023.1. Solo RFC genericos (XAXX010101000 y
// XEXX010101000), nombres y montos inventados. Deterministas: la misma clave
// produce siempre los mismos CFDI (las capturas son reproducibles).

// Reloj fijo del prototipo (D19: America/Mexico_City): lunes 19 oct 2026, 10:42.
const hoy = { anio: 2026, mes: 10, dia: 19, hora: "10:42" }
const textoAhora = "19 oct 2026, 10:42"

const meses = ["enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto",
               "septiembre", "octubre", "noviembre", "diciembre"]
const mesesCortos = ["ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"]

function capital(t) { return t.charAt(0).toUpperCase() + t.slice(1) }
function nombreMes(mes) { return capital(meses[mes - 1]) }
function nombreMesAnio(anio, mes) { return meses[mes - 1] + " " + anio }
function tituloMesAnio(anio, mes) { return nombreMes(mes) + " " + anio }
function clave(anio, mes) { return anio + "-" + (mes < 10 ? "0" : "") + mes }

function perfilesIniciales() {
    return [
        { rfc: "XAXX010101000", nombre: "Contribuyente de ejemplo", eFirma: "Lista" },
        { rfc: "XEXX010101000", nombre: "Proveedor de ejemplo", eFirma: "SinCredencial" }
    ]
}

const contrapartes = [
    "Servicios Contables Demo", "Comercializadora Ejemplo", "Papelería Ficticia del Centro",
    "Transportes Muestra", "Consultoría Prueba", "Distribuidora Imaginaria",
    "Arrendamientos Demo", "Telecomunicaciones Ejemplo", "Servicios de Limpieza Demo",
    "Refacciones Ficticias"
]

// Generador congruencial (determinista por semilla).
function generador(semilla) {
    let s = semilla >>> 0
    return function () {
        s = (Math.imul(s, 1664525) + 1013904223) >>> 0
        return s / 4294967296
    }
}

function semillaDe(texto) {
    let h = 2166136261
    for (let i = 0; i < texto.length; ++i) {
        h ^= texto.charCodeAt(i)
        h = Math.imul(h, 16777619) >>> 0
    }
    return h
}

function hex(aleatorio, n) {
    let t = ""
    for (let i = 0; i < n; ++i)
        t += "0123456789ABCDEF".charAt(Math.floor(aleatorio() * 16))
    return t
}

function uuid(aleatorio) {
    return hex(aleatorio, 8) + "-" + hex(aleatorio, 4) + "-4" + hex(aleatorio, 3) + "-8" + hex(aleatorio, 3) + "-"
           + hex(aleatorio, 12)
}

function moneda(valor) {
    const partes = valor.toFixed(2).split(".")
    return "$" + partes[0].replace(/\B(?=(\d{3})+(?!\d))/g, ",") + "." + partes[1]
}

function diasDelMes(anio, mes) { return new Date(anio, mes, 0).getDate() }

// Numero de CFDI del mes por direccion (estable).
function conteo(rfc, anio, mes, direccion) {
    const a = generador(semillaDe(rfc + clave(anio, mes) + direccion))
    return direccion === "emitidas" ? 14 + Math.floor(a() * 22) : 22 + Math.floor(a() * 36)
}

const tipos = [["I", "Ingreso"], ["I", "Ingreso"], ["I", "Ingreso"], ["I", "Ingreso"], ["E", "Egreso"],
               ["P", "Pago"], ["I", "Ingreso"], ["N", "Nómina"]]

// CFDI sinteticos de un mes y direccion, ordenados por fecha descendente.
function cfdis(perfil, anio, mes, direccion, hastaDia) {
    const n = conteo(perfil.rfc, anio, mes, direccion)
    const a = generador(semillaDe(perfil.rfc + clave(anio, mes) + direccion + "cfdi"))
    const otroRfc = perfil.rfc === "XAXX010101000" ? "XEXX010101000" : "XAXX010101000"
    const dias = hastaDia !== undefined ? hastaDia : diasDelMes(anio, mes)
    const lista = []
    for (let i = 0; i < n; ++i) {
        const dia = 1 + Math.floor(a() * dias)
        let tipo = tipos[Math.floor(a() * tipos.length)]
        if (direccion === "recibidas" && tipo[0] === "N")
            tipo = tipos[0]
        const contraparte = contrapartes[Math.floor(a() * contrapartes.length)]
        const total = tipo[0] === "P" ? 0 : 450 + a() * 48000
        const id = uuid(a)
        const emitida = direccion === "emitidas"
        lista.push({
            dia: dia,
            fecha: dia + " " + mesesCortos[mes - 1] + " " + anio,
            tipo: tipo[0],
            tipoTexto: tipo[1],
            serieFolio: (emitida ? "A-" : "F-") + (1000 + Math.floor(a() * 8999)),
            emisorRfc: emitida ? perfil.rfc : otroRfc,
            emisorNombre: emitida ? perfil.nombre : contraparte,
            receptorRfc: emitida ? otroRfc : perfil.rfc,
            receptorNombre: emitida ? contraparte : perfil.nombre,
            total: moneda(total),
            totalNumero: total,
            moneda: "MXN",
            metodoPago: tipo[0] === "P" ? "" : (a() < 0.7 ? "PUE" : "PPD"),
            uuid: id,
            uuidCorto: id.substring(0, 8),
            concepto: tipo[0] === "N" ? "Pago de nómina" : tipo[0] === "P" ? "Pago" : "Servicios profesionales",
            direccion: direccion
        })
    }
    lista.sort((x, y) => y.dia - x.dia)
    return lista
}

function futuro(anio, mes) {
    return anio > hoy.anio || (anio === hoy.anio && mes > hoy.mes)
}

function enCurso(anio, mes) { return anio === hoy.anio && mes === hoy.mes }

// Estado inicial de un mes (lo que "ya hay en el equipo" al abrir el prototipo).
//   estado: SinDescargar | Preparando | Disponibles | Parcial | Atencion | Futuro
//   parcial: enCurso | cerradoIncompleto | unaDireccion
function mesInicial(perfil, anio, mes) {
    const m = { anio: anio, mes: mes, estado: "SinDescargar", emitidas: [], recibidas: [], visE: 0, visR: 0,
                etapa: 0, hasta: "", parcial: "", causa: "", descargadoEn: "", anterior: false }
    if (futuro(anio, mes)) {
        m.estado = "Futuro"
        return m
    }
    const completo = function (fecha) {
        m.estado = "Disponibles"
        m.emitidas = cfdis(perfil, anio, mes, "emitidas")
        m.recibidas = cfdis(perfil, anio, mes, "recibidas")
        m.visE = m.emitidas.length
        m.visR = m.recibidas.length
        m.descargadoEn = fecha
    }
    if (perfil.rfc === "XAXX010101000") {
        if (anio < 2026) {
            completo("8 ene 2026, 09:30")
        } else if (mes <= 3 || mes === 7 || mes === 8) {
            completo(mes === 8 ? "3 sep 2026, 10:03" : "4 " + mesesCortos[mes % 12] + " 2026, 09:12")
        } else if (mes === 4) {
            m.estado = "Parcial"
            m.parcial = "unaDireccion"
            m.emitidas = cfdis(perfil, anio, mes, "emitidas")
            m.visE = m.emitidas.length
            m.recibidas = cfdis(perfil, anio, mes, "recibidas")
            m.visR = 0
            m.anterior = true
        } else if (mes === 5) {
            m.estado = "Parcial"
            m.parcial = "cerradoIncompleto"
            m.hasta = "10 de mayo, 13:05"
            m.emitidas = cfdis(perfil, anio, mes, "emitidas")
            m.recibidas = cfdis(perfil, anio, mes, "recibidas")
            m.visE = m.emitidas.filter(c => c.dia <= 10).length
            m.visR = m.recibidas.filter(c => c.dia <= 10).length
            ordenarHastaDia(m, 10)
        } else if (mes === 6) {
            m.estado = "Atencion"
            m.causa = "archivoDanado"
            m.emitidas = cfdis(perfil, anio, mes, "emitidas")
            m.visE = m.emitidas.length
            m.recibidas = cfdis(perfil, anio, mes, "recibidas")
            m.visR = 0
        } else if (mes === 10) {
            m.estado = "Parcial"
            m.parcial = "enCurso"
            m.hasta = "15 de octubre, 18:20"
            m.emitidas = cfdis(perfil, anio, mes, "emitidas", hoy.dia)
            m.recibidas = cfdis(perfil, anio, mes, "recibidas", hoy.dia)
            m.visE = m.emitidas.filter(c => c.dia <= 15).length
            m.visR = m.recibidas.filter(c => c.dia <= 15).length
            ordenarHastaDia(m, 15)
        }
    } else if (anio === 2026 && mes === 7) {
        completo("5 ago 2026, 17:48")
    }
    return m
}

// Pone primero (dentro de los visibles) los CFDI hasta `dia` y luego el resto,
// para que "llenar" la lista agregue los dias faltantes.
function ordenarHastaDia(m, dia) {
    const separar = function (lista) {
        return lista.filter(c => c.dia <= dia).concat(lista.filter(c => c.dia > dia))
    }
    m.emitidas = separar(m.emitidas)
    m.recibidas = separar(m.recibidas)
}

// Historial de descargas por perfil (mas reciente primero).
//   resultado: Completa | EnPreparacion | Atencion | SinFacturas | Anterior
function historialInicial() {
    const p = perfilesIniciales()[0]
    const fila = function (anio, mes, fecha, resultado, detalle) {
        return { anio: anio, mes: mes, fecha: fecha, resultado: resultado, detalle: detalle || "",
                 emitidas: conteo(p.rfc, anio, mes, "emitidas"), recibidas: conteo(p.rfc, anio, mes, "recibidas") }
    }
    const junio = fila(2026, 6, "2 jul 2026, 11:40", "Atencion", "Un archivo de las recibidas está dañado")
    junio.recibidas = 0
    const mayo = fila(2026, 5, "10 may 2026, 13:05", "Completa", "Hasta el 10 de mayo, 13:05")
    const abril = fila(2026, 4, "2 may 2026, 08:15", "Anterior", "Solo emitidas, hecha con una versión anterior")
    abril.recibidas = 0
    return {
        "XAXX010101000": [
            fila(2026, 10, "15 oct 2026, 18:20", "Completa", "Hasta el 15 de octubre, 18:20"),
            fila(2026, 8, "3 sep 2026, 10:03", "Completa"),
            fila(2026, 7, "4 ago 2026, 09:12", "Completa"),
            junio,
            mayo,
            abril,
            { anio: 2026, mes: 2, fecha: "4 mar 2026, 09:12", resultado: "SinFacturas", detalle: "", emitidas: 0,
              recibidas: 0 },
            fila(2026, 1, "4 feb 2026, 09:12", "Completa")
        ],
        "XEXX010101000": [
            { anio: 2026, mes: 7, fecha: "5 ago 2026, 17:48", resultado: "Anterior", detalle: "Hecha con una versión anterior",
              emitidas: conteo("XEXX010101000", 2026, 7, "emitidas"), recibidas: conteo("XEXX010101000", 2026, 7, "recibidas") }
        ]
    }
}

// Presentacion de los estados de mes: texto, tono e icono (Theme.tono / Icono).
function estadoMes(estado) {
    switch (estado) {
    case "Preparando": return { texto: "Preparando", tono: "progreso", icono: "clock" }
    case "Disponibles": return { texto: "Disponibles", tono: "exito", icono: "check-circle" }
    case "Parcial": return { texto: "Disponibles parcialmente", tono: "advertencia", icono: "calendar-clock" }
    case "Atencion": return { texto: "Requiere atención", tono: "error", icono: "exclamation-triangle" }
    case "Futuro": return { texto: "Aún no inicia", tono: "neutro", icono: "calendar" }
    default: return { texto: "Sin descargar", tono: "neutro", icono: "circle-dashed" }
    }
}

function resultadoHistorial(resultado) {
    switch (resultado) {
    case "EnPreparacion": return { texto: "En preparación", tono: "progreso", icono: "clock" }
    case "Atencion": return { texto: "Requiere atención", tono: "error", icono: "exclamation-triangle" }
    case "SinFacturas": return { texto: "Sin facturas en el SAT", tono: "neutro", icono: "tray" }
    case "Anterior": return { texto: "Descarga anterior", tono: "neutro", icono: "clock-xmark" }
    default: return { texto: "Completa", tono: "exito", icono: "check-circle" }
    }
}

const etapas = [
    "Pidiendo las facturas al SAT.",
    "El SAT está preparando las facturas. Puede tardar; puedes seguir trabajando y te avisaremos cuando estén listas.",
    "Recibiendo facturas y agregándolas a la lista."
]
