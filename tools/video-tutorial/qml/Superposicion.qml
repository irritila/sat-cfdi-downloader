import QtQuick

// Superposicion del video (T017 D3, D4, D14; revision de motion design
// M1, M2, M8, M9, M10): cursor simulado, onda del clic, titulo del capitulo
// y capa de ilustracion. La crea satcfdi_video_tutorial sobre el contentItem
// de la ventana real. No acepta raton: los eventos sinteticos llegan a la app
// de abajo.
//
// Determinismo: los fundidos y la onda NO usan animaciones de Qt Quick. Una
// animacion que arranca entre dos cuadros recibe una fraccion de tiempo real
// y su fase variaba entre corridas. Aqui cada efecto se calcula con el numero
// de cuadro (`cuadro`, lo fija la herramienta antes de cada captura) y el
// cuadro en que empezo.
// Zona segura de subtitulos (M1): los 65 puntos logicos inferiores (130 px a
// 1080p) quedan libres de overlays para el .srt del reproductor.
Item {
    id: raiz
    objectName: "superposicionVideo"

    property int cuadro: 0
    property point cursor: Qt.point(480, 270)
    property string tituloCapitulo: ""
    property bool mostrarTitulo: false
    // Archivo QML de tools/capturas-manual/qml; vacio = sin ilustracion.
    property url ilustracion: ""
    property var parametrosIlustracion: ({})
    // D14: rotulo propio de la herramienta. Por omision no: las ilustraciones
    // de tools/capturas-manual ya traen el suyo (guion: "rotuloHerramienta").
    property bool rotuloHerramienta: false
    readonly property bool enIlustracion: raiz.ilustracion.toString().length > 0

    // Duraciones en cuadros (30 fps).
    readonly property int kTitulo: 9      // M2
    readonly property int kCapa: 8        // M10
    readonly property int kCambio: 4      // M10: 4 + 4 al cambiar de ilustracion
    readonly property int kCursor: 6      // M9
    readonly property int kOnda: 15       // M8

    function progreso(inicio, duracion) {
        return Math.max(0, Math.min(1, (raiz.cuadro - inicio) / duracion))
    }
    function suave(t) { // Easing.InOutQuad
        return t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2
    }
    function salida(t) { // Easing.OutQuad
        return 1 - (1 - t) * (1 - t)
    }
    function fundido(desde, hasta, inicio, duracion) {
        return desde + (hasta - desde) * raiz.suave(raiz.progreso(inicio, duracion))
    }

    function clic() {
        raiz.inicioOnda = raiz.cuadro
    }

    anchors.fill: parent
    z: 2000000

    // Latido: una animacion siempre activa mantiene en marcha el reloj de
    // animaciones (QUnifiedTimer). Sin ella, tras un periodo sin animaciones
    // el reloj se detiene y la siguiente animacion de la app que arranca
    // recibe de golpe el tiempo transcurrido y salta a su final.
    property real latido: 0
    NumberAnimation on latido {
        from: 0
        to: 1
        duration: 1000
        loops: Animation.Infinite
    }

    // --- Estado de los fundidos ---------------------------------------------
    property real tituloDesde: 0
    property real tituloHasta: 0
    property int inicioTitulo: -1000
    property real capaDesde: 0
    property real capaHasta: 0
    property int inicioCapa: -1000
    property real cursorDesde: 1
    property real cursorHasta: 1
    property int inicioCursor: -1000
    property int inicioCambio: -1000
    property bool cambioPendiente: false
    property int inicioOnda: -1000
    property url fuenteMostrada: ""
    property var parametrosMostrados: ({})

    readonly property real opacidadTitulo: raiz.fundido(raiz.tituloDesde, raiz.tituloHasta, raiz.inicioTitulo, raiz.kTitulo)
    readonly property real opacidadCapa: raiz.fundido(raiz.capaDesde, raiz.capaHasta, raiz.inicioCapa, raiz.kCapa)
    readonly property real opacidadCursor: raiz.fundido(raiz.cursorDesde, raiz.cursorHasta, raiz.inicioCursor, raiz.kCursor)
    // Contenido de la ilustracion: 1 -> 0 en kCambio, cambia, 0 -> 1 en kCambio.
    readonly property real opacidadContenido: {
        const t = raiz.cuadro - raiz.inicioCambio
        if (t < 0 || t >= 2 * raiz.kCambio)
            return 1
        return t < raiz.kCambio ? 1 - raiz.suave(t / raiz.kCambio) : raiz.suave((t - raiz.kCambio) / raiz.kCambio)
    }

    onMostrarTituloChanged: {
        raiz.tituloDesde = raiz.opacidadTitulo
        raiz.tituloHasta = raiz.mostrarTitulo && raiz.tituloCapitulo.length > 0 ? 1 : 0
        raiz.inicioTitulo = raiz.cuadro
    }

    // Se lee `ilustracion` directamente: dentro de este manejador el binding
    // de `enIlustracion` aun puede tener el valor anterior.
    onIlustracionChanged: {
        const hay = raiz.ilustracion.toString().length > 0
        raiz.cursorDesde = raiz.opacidadCursor
        raiz.cursorHasta = hay ? 0 : 1
        raiz.inicioCursor = raiz.cuadro
        if (!hay) {
            raiz.capaDesde = raiz.opacidadCapa
            raiz.capaHasta = 0
            raiz.inicioCapa = raiz.cuadro // la fuente se conserva hasta terminar
        } else if (raiz.fuenteMostrada.toString().length === 0 || raiz.opacidadCapa === 0) {
            raiz.parametrosMostrados = raiz.parametrosIlustracion
            raiz.fuenteMostrada = raiz.ilustracion
            raiz.capaDesde = raiz.opacidadCapa
            raiz.capaHasta = 1
            raiz.inicioCapa = raiz.cuadro
        } else {
            raiz.inicioCambio = raiz.cuadro
            raiz.cambioPendiente = true
        }
    }

    onCuadroChanged: {
        if (raiz.cambioPendiente && raiz.cuadro - raiz.inicioCambio >= raiz.kCambio) {
            raiz.cambioPendiente = false
            raiz.parametrosMostrados = raiz.parametrosIlustracion
            raiz.fuenteMostrada = raiz.ilustracion
        }
        if (raiz.capaHasta === 0 && raiz.progreso(raiz.inicioCapa, raiz.kCapa) >= 1
                && raiz.fuenteMostrada.toString().length > 0 && raiz.ilustracion.toString().length === 0) {
            raiz.fuenteMostrada = ""
        }
    }

    // --- Ilustracion (D10, D14; M10) ----------------------------------------
    Rectangle {
        id: capaIlustracion
        objectName: "capaIlustracion"
        anchors.fill: parent
        color: "#e5e5ea"
        opacity: raiz.opacidadCapa
        visible: opacity > 0

        Loader {
            id: cargador
            anchors.centerIn: parent
            source: raiz.fuenteMostrada
            opacity: raiz.opacidadContenido
            // Las ilustraciones se disenaron para 960x640: se reducen si no caben.
            scale: cargador.item ? Math.min(1, (capaIlustracion.width - 48) / cargador.item.width,
                                            (capaIlustracion.height - 72) / cargador.item.height) : 1
            onLoaded: {
                const p = raiz.parametrosMostrados
                for (const clave in p)
                    cargador.item[clave] = p[clave]
            }
        }

        Rectangle {
            objectName: "rotuloIlustracion"
            visible: raiz.rotuloHerramienta
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 14
            width: textoRotulo.implicitWidth + 20
            height: textoRotulo.implicitHeight + 8
            radius: height / 2
            color: "#d91c1c1e"
            Text {
                id: textoRotulo
                anchors.centerIn: parent
                text: "Ilustración"
                color: "white"
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
        }
    }

    // --- Titulo del capitulo (M1: sobre la zona segura; M2: fundidos de 9) ---
    Rectangle {
        objectName: "tituloCapitulo"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 96
        width: Math.min(parent.width - 64, textoTitulo.implicitWidth + 40)
        height: textoTitulo.implicitHeight + 18
        radius: 10
        color: "#e61c1c1e"
        opacity: raiz.opacidadTitulo
        visible: opacity > 0
        Text {
            id: textoTitulo
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 40)
            elide: Text.ElideRight
            text: raiz.tituloCapitulo
            color: "white"
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
    }

    // --- Cursor y onda: ocultos en ilustraciones (M9) -------------------------
    Item {
        anchors.fill: parent
        opacity: raiz.opacidadCursor
        visible: opacity > 0

        // Onda del clic (M8): 15 cuadros; disco (entra en 2 a 0.35, se apaga en 8).
        Rectangle {
            readonly property int t: raiz.cuadro - raiz.inicioOnda
            width: 24
            height: 24
            radius: 12
            x: raiz.cursor.x - width / 2
            y: raiz.cursor.y - height / 2
            color: "#0062d6"
            opacity: t < 0 || t >= 10 ? 0 : (t < 2 ? 0.35 * t / 2 : 0.35 * (1 - (t - 2) / 8))
            visible: opacity > 0
        }
        // Anillo: escala 0.5 -> 1.6, opacidad 0.9 -> 0 (OutQuad).
        Rectangle {
            readonly property real p: raiz.salida(raiz.progreso(raiz.inicioOnda, raiz.kOnda))
            readonly property bool activa: raiz.cuadro - raiz.inicioOnda >= 0
                                           && raiz.cuadro - raiz.inicioOnda < raiz.kOnda
            width: 40
            height: 40
            radius: 20
            x: raiz.cursor.x - width / 2
            y: raiz.cursor.y - height / 2
            color: "transparent"
            border.width: 3
            border.color: "#0062d6"
            scale: 0.5 + (1.6 - 0.5) * p
            opacity: activa ? 0.9 * (1 - p) : 0
            visible: opacity > 0
        }

        // Cursor simulado (flecha).
        Canvas {
            objectName: "cursorSimulado"
            x: raiz.cursor.x - 1
            y: raiz.cursor.y - 1
            width: 22
            height: 30
            renderStrategy: Canvas.Immediate
            onPaint: {
                const g = getContext("2d")
                g.reset()
                g.beginPath()
                g.moveTo(2, 2)
                g.lineTo(2, 24)
                g.lineTo(7.5, 18.5)
                g.lineTo(11.5, 27)
                g.lineTo(15, 25.5)
                g.lineTo(11, 17.5)
                g.lineTo(18.5, 17.5)
                g.closePath()
                g.lineJoin = "round"
                g.lineWidth = 2
                g.strokeStyle = "white"
                g.fillStyle = "#111111"
                g.stroke()
                g.fill()
            }
        }
    }
}
