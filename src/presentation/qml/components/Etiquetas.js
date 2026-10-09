.pragma library

// Texto visible de presentacion a partir de claves estables recibidas de los
// view models. No contiene reglas de negocio ni texto persistido.

const estadosResumen = {
    "Creada": "Creada",
    "Enviando": "Enviando",
    "Enviada": "Enviada",
    "EnvioFallido": "Envío fallido",
    "EnvioIncierto": "Envío incierto",
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
    "EnvioFallido": "Envío fallido",
    "EnvioIncierto": "Envío incierto"
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

const tiposComprobante = {
    "I": "Ingreso",
    "E": "Egreso",
    "T": "Traslado",
    "N": "Nómina",
    "P": "Pago"
}

const eventosLog = {
    "solicitud_creada": "Solicitud creada",
    "duplicado_confirmado": "Duplicado confirmado por el usuario",
    "envio_iniciado": "Envío iniciado",
    "solicitud_enviada": "Solicitud enviada",
    "envio_fallido": "Envío fallido",
    "envio_incierto": "Envío incierto",
    "verificacion_realizada": "Verificación realizada",
    "verificacion_fallida": "Verificación fallida",
    "paquetes_registrados": "Paquetes registrados",
    "descarga_iniciada": "Descarga iniciada",
    "paquete_descargado": "Paquete descargado",
    "descarga_fallida": "Descarga fallida",
    "descarga_interrumpida": "Descarga interrumpida",
    "paquete_reconciliado": "Paquete reconciliado",
    "paquete_vencido": "Paquete vencido",
    "archivo_huerfano": "Archivo huérfano",
    "accion_pendiente_registrada": "Acción pendiente registrada",
    "accion_pendiente_descartada": "Acción pendiente descartada",
    "envio_no_iniciado": "Envío no iniciado; puede reenviarse manualmente",
    "verificacion_suspendida": "Verificación automática suspendida; use Verificar ahora"
}

const origenesLog = {
    "worker": "Automático",
    "usuario": "Usuario",
    "recuperacion": "Recuperación"
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
function tipoComprobante(clave) { return texto(tiposComprobante, clave, "Todos") }
function eventoLog(clave) { return texto(eventosLog, clave, "Evento") }
// T008 D11: existencia del ZIP local de un paquete Descargado ("" = no aplica).
const existenciasPaquete = {
    "Comprobando": "Comprobando archivo local…",
    "Presente": "Archivo local presente",
    "NoEncontrado": "Archivo local no encontrado: el ZIP se movió o se borró fuera de la app.",
    "ErrorComprobacion": "No se pudo comprobar el archivo local"
}
function existenciaPaquete(clave) { return texto(existenciasPaquete, clave, "") }
function origenLog(clave) { return texto(origenesLog, clave, "") }

// Categoria visual (solo refuerza el texto; nunca lo sustituye).
function tono(clave) {
    switch (clave) {
    case "Terminada":
    case "Descargado":
    case "Lista":
        return "exito"
    case "EnvioFallido":
    case "MaterialDanado":
    case "MaterialFaltante":
    case "ErrorSat":
    case "Error":
    case "Rechazada":
        return "error"
    case "EnvioIncierto":
    case "NoVigenteAun":
    case "EstadoNoDisponible":
    case "Vencida":
    case "Vencido":
        return "advertencia"
    case "Enviando":
    case "Verificando":
    case "Aceptada":
    case "EnProceso":
    case "Descargando":
    case "Disponible":
        return "progreso"
    default:
        return "neutro"
    }
}
