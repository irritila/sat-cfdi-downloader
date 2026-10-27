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
    // T024.4 (03-motion 2b): banner de notificacion sobre la app viva.
    // [{titulo, cuerpo}] con los textos de ServicioNotificaciones; lo fijan
    // mostrarBanner (inicioBanner) y ocultarBanner (finBanner).
    property var banners: []
    property int inicioBanner: -1000
    property int finBanner: -1000
    // Icono real de la app (lo fija la herramienta; vacio = sin icono).
    property url iconoApp: ""
    // T024.4 (03-motion 3): keycap de `tecla` con modificadores. Simbolos
    // ("⌘", "4") y cuadro de inicio de la accion (el evento va en el 6).
    property var teclasKeycap: []
    property int inicioKeycap: -1000
    // Portada y cierre (03-motion 4): el cursor se oculta fuera del contenido.
    property bool ocultarCursor: false

    // Duraciones en cuadros (30 fps).
    readonly property int kTitulo: 9      // M2
    readonly property int kCapa: 8        // M10
    readonly property int kCambio: 4      // M10: 4 + 4 al cambiar de ilustracion
    readonly property int kCursor: 6      // M9
    readonly property int kOnda: 15       // M8
    readonly property int kBannerEntrada: 10 // 2b: 0-9 entra (OutQuad)
    readonly property int kBannerSalida: 10  // 2b: sale en 10 (InQuad)
    readonly property int kBannerDesfase: 14 // 2b: el segundo entra en el 14
    readonly property int kKeycap: 36        // 3: total

    function progreso(inicio, duracion) {
        return Math.max(0, Math.min(1, (raiz.cuadro - inicio) / duracion))
    }
    function suave(t) { // Easing.InOutQuad
        return t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2
    }
    function salida(t) { // Easing.OutQuad
        return 1 - (1 - t) * (1 - t)
    }
    function entrada(t) { // Easing.InQuad
        return t * t
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
            // T024.4 (D16): reducido, el texto se muestreaba con filtro lineal
            // desde el atlas de glifos, cuyo orden depende de lo que se dibujo
            // antes; la columna del borde de algunos glifos variaba entre
            // corridas. Con una capa, la ilustracion se dibuja a escala 1
            // (glifos alineados al pixel) y se reduce la textura de la capa.
            layer.enabled: cargador.scale < 1
            layer.smooth: true
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

    // --- Banner de notificacion (T024.4, 03-motion 2b) ----------------------
    // 400x70 en x 544, y 12; el segundo, 82 mas abajo. Sin capa gris: la app
    // sigue visible. Rotulo "Ilustración" bajo la pila con la misma opacidad.
    Item {
        id: pilaBanners
        objectName: "bannerNotificacion"
        readonly property real salidaBanner: raiz.entrada(raiz.progreso(raiz.finBanner, raiz.kBannerSalida))
        readonly property bool saliendo: raiz.finBanner >= raiz.inicioBanner && raiz.cuadro >= raiz.finBanner
        readonly property real opacidadPila: pilaBanners.saliendo ? 1 - pilaBanners.salidaBanner : 1
        x: raiz.width - 416 // 544 en 960
        y: 12
        width: 400
        height: raiz.banners.length * 82
        visible: raiz.banners.length > 0 && raiz.cuadro >= raiz.inicioBanner
                 && !(pilaBanners.saliendo && pilaBanners.salidaBanner >= 1)

        Repeater {
            model: raiz.banners
            delegate: Rectangle {
                id: banner
                required property var modelData
                required property int index
                readonly property real p: raiz.salida(raiz.progreso(raiz.inicioBanner + banner.index * raiz.kBannerDesfase,
                                                                    raiz.kBannerEntrada))
                readonly property real opacidadEntrada: raiz.cuadro >= raiz.inicioBanner + banner.index * raiz.kBannerDesfase
                                                        ? banner.p : 0
                x: 48 * (1 - banner.p) + (pilaBanners.saliendo ? 48 * pilaBanners.salidaBanner : 0)
                y: banner.index * 82
                width: 400
                height: 70
                radius: 14
                color: "#f2f2f4"
                border.width: 1
                border.color: "#d4d4d8"
                opacity: banner.opacidadEntrada * pilaBanners.opacidadPila

                Rectangle {
                    z: -1
                    anchors.fill: parent
                    anchors.margins: -2
                    anchors.topMargin: 1
                    radius: 16
                    color: "#26000000"
                }
                Image {
                    id: iconoBanner
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 36
                    height: 36
                    sourceSize: Qt.size(72, 72)
                    source: raiz.iconoApp
                    smooth: true
                }
                Column {
                    anchors.left: iconoBanner.right
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        text: banner.modelData.titulo
                        color: "#1d1d1f"
                        font.pixelSize: 13
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: banner.modelData.cuerpo
                        color: "#3a3a3c"
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Rectangle {
            objectName: "rotuloBanner"
            anchors.right: parent.right
            y: (raiz.banners.length - 1) * 82 + 70 + 6
            width: rotuloBannerTexto.implicitWidth + 16
            height: rotuloBannerTexto.implicitHeight + 6
            radius: height / 2
            color: "#1d1d1f"
            opacity: pilaBanners.opacidadPila * raiz.salida(raiz.progreso(raiz.inicioBanner, raiz.kBannerEntrada))
            Text {
                id: rotuloBannerTexto
                anchors.centerIn: parent
                text: "Ilustración"
                color: "white"
                font.pixelSize: 12
                font.bold: true
            }
        }
    }

    // --- Keycap de `tecla` con modificadores (T024.4, 03-motion 3) -------------
    // 0-5 entra (opacidad y escala 0.9 -> 1, InOutQuad); 6-9 teclas hundidas
    // (el evento sale en el 6); 28-35 sale.
    Rectangle {
        id: keycap
        objectName: "teclaVisible"
        readonly property int t: raiz.cuadro - raiz.inicioKeycap
        readonly property real pEntrada: raiz.suave(Math.max(0, Math.min(1, keycap.t / 6)))
        readonly property real pSalida: raiz.suave(Math.max(0, Math.min(1, (keycap.t - 28) / 8)))
        readonly property bool hundidas: keycap.t >= 6 && keycap.t <= 9
        x: (parent.width - width) / 2
        y: raiz.height - 20 - height // borde inferior en 520 con 540
        width: filaTeclas.implicitWidth + 16
        height: 40 + 16
        radius: 12
        color: "#d91c1c1e"
        scale: 0.9 + 0.1 * keycap.pEntrada
        opacity: keycap.t < 0 || keycap.t >= raiz.kKeycap ? 0 : keycap.pEntrada * (1 - keycap.pSalida)
        visible: opacity > 0 && raiz.teclasKeycap.length > 0

        Row {
            id: filaTeclas
            anchors.centerIn: parent
            spacing: 6
            Repeater {
                model: raiz.teclasKeycap
                delegate: Item {
                    id: tecla
                    required property string modelData
                    width: 40
                    height: 40
                    Rectangle {
                        y: 2
                        width: 40
                        height: 40
                        radius: 8
                        color: "#00000026"
                    }
                    Rectangle {
                        y: keycap.hundidas ? 1 : 0
                        width: 40
                        height: 40
                        radius: 8
                        color: keycap.hundidas ? "#e2e2e6" : "#f7f7f9"
                        border.width: 1
                        border.color: "#c8c8cc"
                        Text {
                            anchors.centerIn: parent
                            text: tecla.modelData
                            color: "#1d1d1f"
                            font.pixelSize: tecla.modelData.length > 1 ? 14 : 18
                        }
                    }
                }
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
        visible: opacity > 0 && !raiz.ocultarCursor

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
