pragma Singleton

import QtQuick

// Tema visual (T013 D3): TODOS los tokens de docs/design/ui-ux-v2/tokens.json,
// copiados a mano (sin generacion en build). TestTema compara cada valor con el
// JSON en claro y en oscuro; si cambia un token, actualizalo aqui.
//
// - `oscuro` sigue el modo de macOS (Application.styleHints.colorScheme). Asignarlo
//   (p. ej. Theme.oscuro = true) fija el tema e ignora el sistema (D9).
// - Colores: nombres del token en camelCase (tono-neutro-fondo ->
//   tonoNeutroFondo). Los colores con alfa del JSON (#RRGGBBAA) se escriben en
//   notacion QML (#AARRGGBB).
// - estado(clave) -> { fondo, texto, borde, icono, tono } del alias del estado
//   (solicitud, paquete y e.firma). Acepta el alias del token (AceptadaSat,
//   EFirmaLista...) y las claves estables de la app (Aceptada, EnProceso,
//   Error de paquete...). estadoEFirma(preparacion) traduce las claves de
//   PreparacionPerfil (Lista, Vencida, SinCredencial...). `icono` es el nombre
//   del SVG para Icono.qml.
// - Tipografia: objetos { size, weight, lineHeight } (titulo, subtitulo,
//   cuerpo, cuerpoFuerte, leyenda, etiqueta, mono) y familias `familia` y
//   `familiaMono`.
// - Medidas en puntos logicos: espacio*, radio*, alto*, ancho*; sombras como
//   texto CSS del token (sombraTarjeta, sombraDialogo).
QtObject {
    id: tema

    property bool oscuro: Application.styleHints.colorScheme === Qt.ColorScheme.Dark

    // ---- Colores ----
    readonly property color fondo: tema.oscuro ? "#1c1c1e" : "#f0f0f2"
    readonly property color superficie: tema.oscuro ? "#252527" : "#ffffff"
    readonly property color superficieElevada: tema.oscuro ? "#2f2f33" : "#ffffff"
    readonly property color texto: tema.oscuro ? "#f2f2f5" : "#1d1d1f"
    readonly property color textoSecundario: tema.oscuro ? "#a9a9b1" : "#5c5c63"
    readonly property color borde: tema.oscuro ? "#7a7a83" : "#85858c"
    readonly property color acento: tema.oscuro ? "#286cdb" : "#0062d6"
    readonly property color textoSobreAcento: tema.oscuro ? "#ffffff" : "#ffffff"
    readonly property color exito: tema.oscuro ? "#5bc57a" : "#18793a"
    readonly property color advertencia: tema.oscuro ? "#f0aa3e" : "#975300"
    readonly property color error: tema.oscuro ? "#ff7069" : "#c0271f"
    readonly property color info: tema.oscuro ? "#64a8ff" : "#0b5cc0"
    readonly property color separador: tema.oscuro ? "#3a3a3f" : "#e2e2e6"
    readonly property color superficieSeccion: tema.oscuro ? "#2a2a2d" : "#f7f7f9"
    readonly property color seleccion: tema.oscuro ? "#1e3a63" : "#dce9fb"
    readonly property color foco: tema.oscuro ? "#5a9bff" : "#0062d6"
    readonly property color acentoTexto: tema.oscuro ? "#7db4ff" : "#0059c2"
    readonly property color acentoPresionado: tema.oscuro ? "#1d57b8" : "#0050ae"
    readonly property color controlFondo: tema.oscuro ? "#1f1f22" : "#ffffff"
    readonly property color controlSecundario: tema.oscuro ? "#3a3a3f" : "#e9e9ed"
    readonly property color sombra: tema.oscuro ? "#80000000" : "#24000000"
    readonly property color velo: tema.oscuro ? "#80000000" : "#4d000000"
    readonly property color segmentoActivo: tema.oscuro ? "#5e5e66" : "#ffffff"
    readonly property color tonoNeutroFondo: tema.oscuro ? "#36363b" : "#eeeef1"
    readonly property color tonoNeutroTexto: tema.oscuro ? "#e4e4ea" : "#3a3a41"
    readonly property color tonoNeutroBorde: tema.oscuro ? "#8a8a93" : "#9a9aa2"
    readonly property color tonoProgresoFondo: tema.oscuro ? "#1a2c47" : "#e7f0fd"
    readonly property color tonoProgresoTexto: tema.oscuro ? "#b0cfff" : "#0b4ca3"
    readonly property color tonoProgresoBorde: tema.oscuro ? "#4f7dbf" : "#7fa8e3"
    readonly property color tonoExitoFondo: tema.oscuro ? "#16321f" : "#e3f3e7"
    readonly property color tonoExitoTexto: tema.oscuro ? "#a2e0b4" : "#13652b"
    readonly property color tonoExitoBorde: tema.oscuro ? "#428e5b" : "#6bb282"
    readonly property color tonoAdvertenciaFondo: tema.oscuro ? "#3b2b11" : "#fcf0da"
    readonly property color tonoAdvertenciaTexto: tema.oscuro ? "#f6cb82" : "#7f4500"
    readonly property color tonoAdvertenciaBorde: tema.oscuro ? "#a8792c" : "#d29a40"
    readonly property color tonoErrorFondo: tema.oscuro ? "#451a1b" : "#fce7e6"
    readonly property color tonoErrorTexto: tema.oscuro ? "#ffb4b0" : "#a11a16"
    readonly property color tonoErrorBorde: tema.oscuro ? "#b9544f" : "#e08079"

    // ---- Estados: alias -> [tono, icono] ----
    readonly property var alias: ({
        "Creada": ["neutro", "circle-dashed"],
        "Enviando": ["progreso", "arrow-up-circle"],
        "Enviada": ["progreso", "paperplane"],
        "EnvioFallido": ["error", "xmark-octagon"],
        "EnvioIncierto": ["advertencia", "question-diamond"],
        "AceptadaSat": ["progreso", "check-seal"],
        "EnProcesoSat": ["progreso", "clock"],
        "Terminada": ["exito", "check-circle"],
        "ErrorSat": ["error", "exclamation-octagon"],
        "Rechazada": ["error", "nosign"],
        "Vencida": ["advertencia", "calendar-exclamation"],
        "Disponible": ["progreso", "arrow-down-circle"],
        "Descargando": ["progreso", "arrow-down-circle-dotted"],
        "Descargado": ["exito", "check-circle"],
        "Error": ["error", "exclamation-triangle"],
        "Vencido": ["advertencia", "clock-xmark"],
        "EFirmaVerificando": ["neutro", "ellipsis-circle"],
        "SinEFirma": ["neutro", "key-slash"],
        "EFirmaLista": ["exito", "check-shield"],
        "EFirmaVencida": ["advertencia", "calendar-exclamation"],
        "EFirmaNoVigente": ["advertencia", "calendar-clock"],
        "EFirmaIncompleta": ["error", "exclamation-lock"],
        "EFirmaDanada": ["error", "exclamation-lock"],
        "EFirmaEstadoNoDisponible": ["advertencia", "question-circle"]
    })
    // Claves estables de la app que difieren del alias del token.
    readonly property var claveAAlias: ({
        "Aceptada": "AceptadaSat",
        "EnProceso": "EnProcesoSat"
    })
    readonly property var preparacionAAlias: ({
        "Verificando": "EFirmaVerificando",
        "SinCredencial": "SinEFirma",
        "Lista": "EFirmaLista",
        "Vencida": "EFirmaVencida",
        "NoVigenteAun": "EFirmaNoVigente",
        "MaterialFaltante": "EFirmaIncompleta",
        "MaterialDanado": "EFirmaDanada",
        "EstadoNoDisponible": "EFirmaEstadoNoDisponible"
    })

    // Colores de un tono ("neutro", "progreso", "exito", "advertencia", "error").
    function tono(nombre) {
        switch (nombre) {
        case "progreso":
            return { fondo: tema.tonoProgresoFondo, texto: tema.tonoProgresoTexto, borde: tema.tonoProgresoBorde, tono: nombre }
        case "exito":
            return { fondo: tema.tonoExitoFondo, texto: tema.tonoExitoTexto, borde: tema.tonoExitoBorde, tono: nombre }
        case "advertencia":
            return { fondo: tema.tonoAdvertenciaFondo, texto: tema.tonoAdvertenciaTexto, borde: tema.tonoAdvertenciaBorde, tono: nombre }
        case "error":
            return { fondo: tema.tonoErrorFondo, texto: tema.tonoErrorTexto, borde: tema.tonoErrorBorde, tono: nombre }
        default:
            return { fondo: tema.tonoNeutroFondo, texto: tema.tonoNeutroTexto, borde: tema.tonoNeutroBorde, tono: "neutro" }
        }
    }

    // Alias del estado de solicitud o paquete (o alias directo del token).
    // Clave desconocida: tono neutro con icono "circle-dashed".
    function estado(clave) {
        const nombre = tema.claveAAlias[clave] !== undefined ? tema.claveAAlias[clave] : clave
        const a = tema.alias[nombre]
        const colores = tema.tono(a ? a[0] : "neutro")
        return { fondo: colores.fondo, texto: colores.texto, borde: colores.borde, tono: colores.tono,
                 icono: a ? a[1] : "circle-dashed" }
    }

    // Estado de e.firma desde la clave de PreparacionPerfil.
    function estadoEFirma(preparacion) {
        const nombre = tema.preparacionAAlias[preparacion]
        return tema.estado(nombre !== undefined ? nombre : preparacion)
    }

    // ---- Tipografia ----
    readonly property string familia: Application.font.family
    readonly property string familiaMono: Qt.fontFamilies().some(f => f === "SF Mono") ? "SF Mono" : "Menlo"
    readonly property var titulo: ({ size: 20, weight: 600, lineHeight: 26 })
    readonly property var subtitulo: ({ size: 15, weight: 600, lineHeight: 20 })
    readonly property var cuerpo: ({ size: 13, weight: 400, lineHeight: 18 })
    readonly property var cuerpoFuerte: ({ size: 13, weight: 600, lineHeight: 18 })
    readonly property var leyenda: ({ size: 12, weight: 400, lineHeight: 16 })
    readonly property var etiqueta: ({ size: 11, weight: 600, lineHeight: 14 })
    readonly property var mono: ({ size: 12, weight: 400, lineHeight: 16 })

    // ---- Espaciado, radios y medidas (pt) ----
    readonly property int espacioXs: 4
    readonly property int espacioS: 8
    readonly property int espacioM: 12
    readonly property int espacioL: 16
    readonly property int espacioXl: 24
    readonly property int espacioXxl: 32
    readonly property int radioControl: 6
    readonly property int radioTarjeta: 10
    readonly property int radioBadge: 10
    readonly property int radioDialogo: 12
    readonly property int altoControl: 28
    readonly property int altoBadge: 20
    readonly property int altoEncabezado: 52
    readonly property int altoFila: 56
    readonly property int altoPie: 56
    readonly property int anchoListaPerfiles: 340
    readonly property int anchoDialogo: 480
    readonly property int anchoFormulario: 640

    // ---- Sombras (texto CSS del token; "none" = sin sombra) ----
    readonly property string sombraTarjeta: tema.oscuro ? "none" : "0 1px 2px #0000001a"
    readonly property string sombraDialogo: tema.oscuro ? "0 12px 32px #00000099, 0 0 0 1px #ffffff26" : "0 12px 32px #00000033, 0 0 0 1px #0000001f"
}
