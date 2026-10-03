.pragma library

// Texto visible de presentacion a partir de claves estables recibidas de los
// view models. No contiene reglas de negocio ni texto persistido.

const estadosResumen = {
    "Creada": "Creada",
    "Enviando": "Enviando",
    "Enviada": "Enviada",
    "EnvioFallido": "Envio fallido",
    "EnvioIncierto": "Envio incierto",
    "Aceptada": "Aceptada por SAT",
    "EnProceso": "En proceso SAT",
    "Terminada": "Terminada",
    "ErrorSat": "Error SAT",
    "Rechazada": "Rechazada por SAT",
    "Vencida": "Vencida"
}

const estadosLocales = {
    "Creada": "Creada",
    "Enviando": "Enviando",
    "Enviada": "Enviada",
    "EnvioFallido": "Envio fallido",
    "EnvioIncierto": "Envio incierto"
}

const estadosSat = {
    "Aceptada": "Aceptada",
    "EnProceso": "En proceso",
    "Terminada": "Terminada",
    "Error": "Error",
    "Rechazada": "Rechazada",
    "Vencida": "Vencida"
}

const estadosDescarga = {
    "Disponible": "Disponible",
    "Descargando": "Descargando",
    "Descargado": "Descargado",
    "Error": "Error de descarga",
    "Vencido": "Vencido"
}

const tiposDescarga = {
    "Emitidos": "Emitidos",
    "Recibidos": "Recibidos"
}

function texto(tabla, clave, siVacio) {
    if (clave === null || clave === undefined || clave === "")
        return siVacio
    const valor = tabla[clave]
    return valor !== undefined ? valor : clave
}

function estadoResumen(clave) { return texto(estadosResumen, clave, "Sin estado") }
function estadoLocal(clave) { return texto(estadosLocales, clave, "Sin estado") }
function estadoSat(clave) { return texto(estadosSat, clave, "Sin respuesta del SAT") }
function estadoDescarga(clave) { return texto(estadosDescarga, clave, "Sin estado") }
function tipoDescarga(clave) { return texto(tiposDescarga, clave, "") }

// Categoria visual (solo refuerza el texto; nunca lo sustituye).
function tono(clave) {
    switch (clave) {
    case "Terminada":
    case "Descargado":
        return "exito"
    case "EnvioFallido":
    case "ErrorSat":
    case "Error":
    case "Rechazada":
        return "error"
    case "EnvioIncierto":
    case "Vencida":
    case "Vencido":
        return "advertencia"
    case "Enviando":
    case "Aceptada":
    case "EnProceso":
    case "Descargando":
    case "Disponible":
        return "progreso"
    default:
        return "neutro"
    }
}

function fechaHora(valor) {
    if (valor === null || valor === undefined)
        return "-"
    const d = new Date(valor)
    if (isNaN(d.getTime()))
        return "-"
    return Qt.formatDateTime(d, "yyyy-MM-dd HH:mm")
}
