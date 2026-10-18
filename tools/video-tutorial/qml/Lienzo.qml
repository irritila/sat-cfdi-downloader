import QtQuick
import QtQuick.Effects
import QtQuick.Window
import SatCfdiVideo

// Lienzo del video (T017 D8, D21, D22; 04-motion-composicion.md): 1280x720
// logicos (2560x1440 px a 2x). Fondo generativo (ShaderEffect, .qsb), la
// ventana de la app centrada a escala 1 con barra de titulo simulada sin
// texto, esquinas, sombra y borde; titulo de capitulo en el margen superior y
// transcripcion quemada abajo (cue en curso y anterior atenuado).
// Todo se calcula con el numero de cuadro de la salida (`cuadro`) y la misma
// linea de tiempo del .srt (`cues`, `capitulos`): nada depende del reloj.
Window {
    id: lienzo
    objectName: "lienzoVideo"

    // Lo fija la herramienta.
    property int cuadro: 0
    property var cues: []        // [{i, f, e, t}] cuadros de la salida (= .srt); e = fin de su escena
    property var capitulos: []   // [{i, f, titulo}]
    property int cuadrosTitulo: 90
    property int lineasTranscripcion: 2
    property real semilla: 2008
    property real velocidad: 0.02
    property var paleta: ["#052E1C", "#0A5F38", "#15924F", "#0E1F18", "#C9A227"]

    // Para la verificacion de la herramienta: cue vigente segun la linea de
    // tiempo y texto mostrado en la ranura actual.
    readonly property string textoVigente: {
        const k = lienzo.indiceCue(lienzo.cuadro)
        return k >= 0 && lienzo.cuadro < lienzo.cues[k].f ? lienzo.cues[k].t : ""
    }
    readonly property string textoActual: actual.texto

    // Geometria (logicos): ventana 960x540 + barra de 28, centrada en x.
    readonly property real ventanaX: (lienzo.width - 960) / 2
    readonly property real barraY: 52
    readonly property real contenidoY: lienzo.barraY + 28
    readonly property real transcripcionAbajo: 712
    readonly property int kRetencion: 60   // un cue sigue visible 2 s tras terminar
    readonly property int kSubir: 8        // la actual sube a la ranura anterior
    readonly property int kEntrar: 6       // la nueva entra con fundido
    readonly property int kTitulo: 9       // fundidos del titulo
    readonly property int kFase: 60        // interpolacion de fase del fondo

    width: 1280
    height: 720
    minimumWidth: 1280
    maximumWidth: 1280
    minimumHeight: 720
    maximumHeight: 720
    color: "#0E1F18"
    title: "Lienzo"

    function suave(t) { // InOutQuad
        const x = Math.max(0, Math.min(1, t))
        return x < 0.5 ? 2 * x * x : 1 - Math.pow(-2 * x + 2, 2) / 2
    }
    function indiceCue(f) {
        let k = -1
        for (let i = 0; i < lienzo.cues.length && lienzo.cues[i].i <= f; ++i)
            k = i
        return k
    }
    function indiceCapitulo(f) {
        let k = -1
        for (let i = 0; i < lienzo.capitulos.length && lienzo.capitulos[i].i <= f; ++i)
            k = i
        return k
    }

    // --- Fondo (D22) ----------------------------------------------------------
    readonly property int capitulo: lienzo.indiceCapitulo(lienzo.cuadro)
    readonly property real faseFondo: {
        const c = Math.max(0, lienzo.capitulo)
        if (c === 0)
            return 0
        const t = (lienzo.cuadro - lienzo.capitulos[c].i) / lienzo.kFase
        return (c - 1) * 0.35 + 0.35 * lienzo.suave(t)
    }
    ShaderEffect {
        objectName: "fondoGenerativo"
        anchors.fill: parent
        property real tiempo: lienzo.cuadro / 30 * lienzo.velocidad
        property real semilla: lienzo.semilla
        property real fase: lienzo.faseFondo
        property real aspecto: width / height
        property real giro: lienzo.cuadro / 30 / 60 * 2 * Math.PI / 180
        property color c0: lienzo.paleta[0]
        property color c1: lienzo.paleta[1]
        property color c2: lienzo.paleta[2]
        property color c3: lienzo.paleta[3]
        property color acento: lienzo.paleta[4]
        fragmentShader: "qrc:/satcfdi-video/qml/fondo.frag.qsb"
    }

    // --- Ventana (D21) --------------------------------------------------------
    Rectangle {
        id: forma
        x: lienzo.ventanaX
        y: lienzo.barraY
        width: 960
        height: 568
        radius: 12
        color: "#f0f0f2"
        visible: false
    }
    MultiEffect {
        source: forma
        anchors.fill: forma
        autoPaddingEnabled: true
        shadowEnabled: true
        shadowColor: "black"
        shadowOpacity: 0.35
        shadowBlur: 1.0
        blurMax: 24              // ~48 px a 2x
        shadowVerticalOffset: 10 // 20 px
    }
    // Barra de titulo simulada: semaforo, sin texto.
    Rectangle {
        objectName: "barraTitulo"
        x: lienzo.ventanaX
        y: lienzo.barraY
        width: 960
        height: 28
        topLeftRadius: 12
        topRightRadius: 12
        color: "#f0f0f2"
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Repeater {
                model: ["#ff5f57", "#febc2e", "#28c840"]
                Rectangle {
                    required property string modelData
                    width: 12
                    height: 12
                    radius: 6
                    color: modelData
                    border.width: 0.5
                    border.color: Qt.darker(modelData, 1.25)
                }
            }
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 0.5
            color: "#1a000000"
        }
    }
    CuadroApp {
        objectName: "cuadroApp"
        x: lienzo.ventanaX
        y: lienzo.contenidoY
        width: 960
        height: 540
        radio: 12
    }
    // Borde de 1 px para separar del fondo.
    Rectangle {
        x: lienzo.ventanaX
        y: lienzo.barraY
        width: 960
        height: 568
        radius: 12
        color: "transparent"
        border.width: 0.5
        border.color: "#1a000000"
    }

    // --- Titulo de capitulo: margen superior (D21) ----------------------------
    component Pildora: Rectangle {
        id: pildora
        property string texto: ""
        property real tamano: 20
        property bool centrado: true
        property int lineas: 2
        readonly property real anchoMaximo: 800
        width: Math.min(pildora.anchoMaximo, etiqueta.contentWidth + 24)
        height: etiqueta.contentHeight + 8
        radius: 8
        color: "#cc1c1c1e"
        visible: opacity > 0 && texto.length > 0
        Text {
            id: etiqueta
            x: 12
            y: 4
            width: pildora.anchoMaximo - 24
            text: pildora.texto
            color: "white"
            font.pixelSize: pildora.tamano
            font.weight: Font.Medium
            lineHeight: 1.15
            wrapMode: Text.WordWrap
            maximumLineCount: pildora.lineas
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignLeft
        }
    }
    readonly property real tTitulo: lienzo.capitulo >= 0 ? lienzo.cuadro - lienzo.capitulos[lienzo.capitulo].i : 0
    Pildora {
        // Titulo saliente (al 55 %) durante el fundido cruzado.
        objectName: "tituloAnterior"
        x: lienzo.ventanaX
        y: 10
        tamano: 18
        lineas: 1
        texto: lienzo.capitulo > 0 ? lienzo.capitulos[lienzo.capitulo - 1].titulo : ""
        opacity: lienzo.capitulo > 0 && lienzo.tTitulo < lienzo.kTitulo
                 ? 0.55 * (1 - lienzo.suave(lienzo.tTitulo / lienzo.kTitulo)) : 0
    }
    Pildora {
        objectName: "tituloCapitulo"
        x: lienzo.ventanaX
        y: 10
        tamano: 18
        lineas: 1
        texto: lienzo.capitulo >= 0 ? lienzo.capitulos[lienzo.capitulo].titulo : ""
        // Entra en 9 cuadros, se queda al 100 % `cuadrosTitulo` y baja al 55 %.
        opacity: {
            const t = lienzo.tTitulo
            if (lienzo.capitulo < 0)
                return 0
            if (t < lienzo.kTitulo)
                return lienzo.suave(t / lienzo.kTitulo)
            if (t < lienzo.cuadrosTitulo)
                return 1
            return 1 - 0.45 * lienzo.suave((t - lienzo.cuadrosTitulo) / lienzo.kTitulo)
        }
    }

    // --- Transcripcion quemada (D21) -----------------------------------------
    // k: ultimo cue iniciado; ranura actual = k (hasta 2 s despues de su fin,
    // sin pasar del fin de su escena); ranura anterior = k-1 atenuado si es
    // de la misma escena y seguia visible.
    readonly property int k: lienzo.indiceCue(lienzo.cuadro)
    readonly property int tCue: lienzo.k >= 0 ? lienzo.cuadro - lienzo.cues[lienzo.k].i : 0
    function finVisible(i) { // ultimo cuadro (exclusivo) en que el cue i se muestra
        return Math.min(lienzo.cues[i].f + lienzo.kRetencion, lienzo.cues[i].e)
    }
    function encadenado(i) { // el cue i-1 seguia visible cuando empezo el i
        return i > 0 && lienzo.cues[i].i <= lienzo.finVisible(i - 1)
    }
    readonly property real pSubir: lienzo.suave(lienzo.tCue / lienzo.kSubir)
    readonly property real salidaActual: lienzo.k < 0 ? 0
        : 1 - Math.max(0, Math.min(1, (lienzo.cuadro - lienzo.finVisible(lienzo.k) + lienzo.kEntrar) / lienzo.kEntrar))
    readonly property real alfaActual: lienzo.k < 0 ? 0
        : Math.min(Math.min(1, lienzo.tCue / lienzo.kEntrar), lienzo.salidaActual)
    Pildora {
        id: actual
        objectName: "transcripcionActual"
        x: (lienzo.width - width) / 2
        y: lienzo.transcripcionAbajo - height
        tamano: 20
        texto: lienzo.k >= 0 && lienzo.salidaActual > 0 ? lienzo.cues[lienzo.k].t : ""
        opacity: lienzo.alfaActual
    }
    Pildora {
        // Cue anterior: sube desde la ranura actual en 8 cuadros y queda al 45 %.
        objectName: "transcripcionAnterior"
        x: (lienzo.width - width) / 2
        tamano: 20 - 4 * lienzo.pSubir
        lineas: 1
        visible: lienzo.lineasTranscripcion > 1 && opacity > 0 && texto.length > 0
        texto: lienzo.k >= 0 && lienzo.encadenado(lienzo.k) ? lienzo.cues[lienzo.k - 1].t : ""
        y: (lienzo.transcripcionAbajo - height) * (1 - lienzo.pSubir)
           + (actual.y - 3 - height) * lienzo.pSubir
        opacity: (1 - 0.55 * lienzo.pSubir) * lienzo.salidaActual
    }
    Pildora {
        // El anterior del anterior se desvanece mientras el nuevo sube.
        objectName: "transcripcionSaliente"
        x: (lienzo.width - width) / 2
        tamano: 16
        lineas: 1
        visible: lienzo.lineasTranscripcion > 1 && opacity > 0 && texto.length > 0
        texto: lienzo.k >= 1 && lienzo.encadenado(lienzo.k) && lienzo.encadenado(lienzo.k - 1)
               ? lienzo.cues[lienzo.k - 2].t : ""
        y: actual.y - 3 - height - 30 * lienzo.pSubir
        opacity: lienzo.pSubir < 1 ? 0.45 * (1 - lienzo.pSubir) : 0
    }
}
