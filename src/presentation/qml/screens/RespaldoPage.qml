pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// T025.6 (C1-C5): pagina Respaldo (⌘4, secundaria como Historial). Solo
// representa RespaldoViewModel: estados, textos y acciones disponibles vienen
// de C++ (mensajes saneados; nunca "subido", sin rutas ni secretos).
// Secciones: datos previos (con "Descartar datos anteriores…"), carpeta
// (FolderDialog nativo; solo se muestra su nombre), contrasena (crear con
// clave de recuperacion de unica visualizacion, o cambiar), respaldo diario,
// "Respaldar ahora" (fase, progreso, resultado y advertencias, incluido el
// aviso de copias sin registro) y "Restaurar un respaldo…".
// Teclado: Tab recorre las acciones en orden; Escape regresa (si no hay un
// dialogo abierto: cada dialogo atiende su Escape). Foco inicial en la
// primera accion disponible.
Page {
    id: pagina
    objectName: "paginaRespaldo"

    required property AppViewModel app
    required property RespaldoViewModel respaldo

    readonly property bool dialogoAbierto: dialogoContrasena.visible || dialogoClave.visible
                                           || dialogoRestaurar.visible || confirmacionDescarte.visible
                                           || dialogoRestaurar.dialogoConfirmacion.visible
    readonly property alias dialogoContrasena: dialogoContrasena
    readonly property alias dialogoClave: dialogoClave
    readonly property alias dialogoRestaurar: dialogoRestaurar
    readonly property alias confirmacionDescarte: confirmacionDescarte

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Respaldo")

    background: Rectangle {
        color: Theme.fondo
    }

    Component.onCompleted: Qt.callLater(pagina.retomar)

    // H2: al cargarse (tambien al volver desde otra pagina) retoma lo que
    // quedo pendiente: la clave de recuperacion sin confirmar o una
    // restauracion en curso o preparada.
    function retomar() {
        if (pagina.respaldo.hayClavePendiente)
            pagina.abrirClave()
        else if (pagina.respaldo.estadoRestauracion !== RespaldoViewModel.Inactiva)
            dialogoRestaurar.open()
        else
            pagina.enfocarInicial()
    }

    function abrirClave() {
        if (dialogoContrasena.opened)
            dialogoContrasena.close()
        if (!dialogoClave.opened)
            dialogoClave.open()
    }

    function enfocarInicial() {
        if (botonElegirCarpeta.visible && botonElegirCarpeta.enabled)
            botonElegirCarpeta.forceActiveFocus(Qt.OtherFocusReason)
        else
            encabezado.botonRegresar.forceActiveFocus(Qt.OtherFocusReason)
    }

    function regresar() {
        if (!pagina.dialogoAbierto)
            pagina.app.regresar()
    }

    function elegirCarpeta() {
        const selector = selectorCarpetaComp.createObject(pagina)
        selector.accepted.connect(() => {
            pagina.respaldo.elegirCarpeta(selector.selectedFolder)
            selector.destroy()
        })
        selector.rejected.connect(() => selector.destroy())
        selector.open()
    }

    function abrirContrasena(modo) {
        dialogoContrasena.modo = modo
        dialogoContrasena.open()
    }

    Keys.onEscapePressed: pagina.regresar()

    Connections {
        target: pagina.respaldo
        // H2: el dialogo pide la clave al view model al abrirse.
        function onClaveRecuperacionLista() {
            Qt.callLater(pagina.abrirClave)
        }
        function onMensajeChanged() {
            if (pagina.respaldo.mensaje.length > 0)
                pagina.Accessible.announce(pagina.respaldo.mensaje)
        }
        function onResultadoChanged() {
            if (pagina.respaldo.tituloResultado.length > 0)
                pagina.Accessible.announce(pagina.respaldo.tituloResultado)
        }
    }

    Component {
        id: selectorCarpetaComp
        FolderDialog {
            title: qsTr("Elegir la carpeta del respaldo")
        }
    }

    header: EncabezadoPagina {
        id: encabezado
        titulo: qsTr("Respaldo")
        subtitulo: pagina.respaldo.carpetaConfigurada ? qsTr("Carpeta: %1").arg(pagina.respaldo.nombreCarpeta) : ""
        mostrarRegresar: true
        textoRegresar: pagina.app.textoRegresar.length > 0 ? pagina.app.textoRegresar : qsTr("Facturas")
        descripcionRegresar: qsTr("Regresar a %1 (Escape)").arg(encabezado.textoRegresar)
        atajoRegresar: ""
        onRegresarSolicitado: pagina.regresar()
    }

    // Tarjeta de seccion: titulo y contenido.
    component Seccion: Rectangle {
        id: seccion
        property string titulo: ""
        default property alias contenido: cuerpo.data
        Layout.fillWidth: true
        implicitHeight: cuerpo.implicitHeight + 2 * Theme.espacioL
        color: Theme.superficie
        radius: Theme.radioTarjeta
        border.width: 1
        border.color: Theme.separador
        Accessible.role: Accessible.Grouping
        Accessible.name: seccion.titulo
        ColumnLayout {
            id: cuerpo
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.espacioL
            spacing: Theme.espacioS
            Label {
                text: seccion.titulo
                color: Theme.texto
                font.family: Theme.familia
                font.pixelSize: Theme.cuerpoFuerte.size
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
        }
    }
    component Texto: Label {
        color: Theme.textoSecundario
        font.family: Theme.familia
        font.pixelSize: Theme.cuerpo.size
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    ScrollView {
        id: desplazamiento
        objectName: "desplazamientoRespaldo"
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: desplazamiento.availableWidth
            spacing: Theme.espacioL

            Item { implicitHeight: Theme.espacioXs }

            // ---- Datos previos y descarte ----
            Seccion {
                objectName: "seccionDatosPrevios"
                visible: pagina.respaldo.hayDatosPrevios || pagina.respaldo.tituloDescarte.length > 0
                titulo: qsTr("Datos anteriores a una restauración")
                Layout.leftMargin: Theme.espacioL
                Layout.rightMargin: Theme.espacioL
                Texto {
                    visible: pagina.respaldo.hayDatosPrevios
                    text: pagina.respaldo.descripcionDatosPrevios
                }
                AvisoEnLinea {
                    objectName: "resultadoDescarte"
                    visible: pagina.respaldo.tituloDescarte.length > 0
                    variante: pagina.respaldo.varianteDescarte
                    titulo: pagina.respaldo.tituloDescarte
                    descripcion: pagina.respaldo.descripcionDescarte
                    Layout.fillWidth: true
                }
                BotonAccion {
                    objectName: "botonDescartarDatosPrevios"
                    visible: pagina.respaldo.hayDatosPrevios
                    text: qsTr("Descartar datos anteriores…")
                    variante: "destructivo"
                    enabled: !pagina.respaldo.descartandoDatosPrevios
                    cargando: pagina.respaldo.descartandoDatosPrevios
                    onClicked: confirmacionDescarte.open()
                }
            }

            AvisoEnLinea {
                objectName: "noDisponibleRespaldo"
                visible: !pagina.respaldo.disponible && !pagina.respaldo.cargando
                variante: "neutro"
                titulo: qsTr("El respaldo no está disponible en este equipo.")
                Layout.fillWidth: true
                Layout.leftMargin: Theme.espacioL
                Layout.rightMargin: Theme.espacioL
            }
            AvisoEnLinea {
                objectName: "mensajeRespaldo"
                visible: pagina.respaldo.mensaje.length > 0
                variante: pagina.respaldo.varianteMensaje
                descripcion: pagina.respaldo.mensaje
                Layout.fillWidth: true
                Layout.leftMargin: Theme.espacioL
                Layout.rightMargin: Theme.espacioL
            }

            // ---- Carpeta ----
            Seccion {
                objectName: "seccionCarpeta"
                visible: pagina.respaldo.disponible
                titulo: qsTr("Carpeta")
                Layout.leftMargin: Theme.espacioL
                Layout.rightMargin: Theme.espacioL
                Texto {
                    objectName: "textoCarpeta"
                    text: pagina.respaldo.carpetaConfigurada
                          ? qsTr("Las copias se guardan en «%1».").arg(pagina.respaldo.nombreCarpeta)
                          : qsTr("Aún no eliges una carpeta.")
                    color: Theme.texto
                }
                Texto {
                    text: qsTr("Puede ser una carpeta de iCloud Drive, Dropbox u otro servicio. Archivo Fiscal copia "
                               + "ahí la copia cifrada y tu servicio de nube la sincroniza por su cuenta.")
                    font.pixelSize: Theme.leyenda.size
                }
                BotonAccion {
                    id: botonElegirCarpeta
                    objectName: "botonElegirCarpeta"
                    text: pagina.respaldo.carpetaConfigurada ? qsTr("Cambiar carpeta…") : qsTr("Elegir carpeta…")
                    icono: "folder"
                    enabled: !pagina.respaldo.ocupado && !pagina.respaldo.respaldando
                    onClicked: pagina.elegirCarpeta()
                }
            }

            // ---- Contrasena ----
            Seccion {
                objectName: "seccionContrasena"
                visible: pagina.respaldo.disponible
                titulo: qsTr("Contraseña")
                Layout.leftMargin: Theme.espacioL
                Layout.rightMargin: Theme.espacioL
                Texto {
                    objectName: "textoContrasena"
                    text: pagina.respaldo.contrasenaConfigurada
                          ? qsTr("Tus respaldos se cifran con tu contraseña. También se abren con tu clave de recuperación.")
                          : qsTr("Crea una contraseña para cifrar tus respaldos. Recibirás una clave de recuperación.")
                    color: Theme.texto
                }
                BotonAccion {
                    objectName: "botonContrasena"
                    text: pagina.respaldo.contrasenaConfigurada ? qsTr("Cambiar contraseña…") : qsTr("Crear contraseña…")
                    icono: "key"
                    enabled: !pagina.respaldo.ocupado && !pagina.respaldo.cargando
                    onClicked: pagina.abrirContrasena(pagina.respaldo.contrasenaConfigurada ? "cambiar" : "crear")
                }
            }

            // ---- Respaldo diario ----
            Seccion {
                objectName: "seccionDiario"
                visible: pagina.respaldo.disponible
                titulo: qsTr("Respaldo diario")
                Layout.leftMargin: Theme.espacioL
                Layout.rightMargin: Theme.espacioL
                Switch {
                    id: interruptorDiario
                    objectName: "interruptorDiario"
                    text: qsTr("Respaldar una vez al día mientras la aplicación esté abierta")
                    checked: pagina.respaldo.diarioActivado
                    enabled: !pagina.respaldo.ocupado && !pagina.respaldo.cargando
                    font.family: Theme.familia
                    font.pixelSize: Theme.cuerpo.size
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Respaldo diario")
                    Accessible.description: text
                    onToggled: {
                        pagina.respaldo.activarDiario(interruptorDiario.checked)
                        // El estado real lo da el view model.
                        interruptorDiario.checked = Qt.binding(() => pagina.respaldo.diarioActivado)
                    }
                }
                Texto {
                    objectName: "pendienteDiario"
                    visible: text.length > 0
                    text: pagina.respaldo.textoPendienteDiario
                }
            }

            // ---- Respaldar ahora ----
            Seccion {
                objectName: "seccionRespaldarAhora"
                visible: pagina.respaldo.disponible
                titulo: qsTr("Respaldar ahora")
                Layout.leftMargin: Theme.espacioL
                Layout.rightMargin: Theme.espacioL
                Texto {
                    objectName: "ultimoExito"
                    visible: text.length > 0
                    text: pagina.respaldo.textoUltimoExito
                }
                RowLayout {
                    spacing: Theme.espacioS
                    Layout.fillWidth: true
                    BotonAccion {
                        objectName: "botonRespaldarAhora"
                        text: qsTr("Respaldar ahora")
                        variante: "primario"
                        enabled: pagina.respaldo.puedeRespaldar
                        cargando: pagina.respaldo.respaldando
                        onClicked: pagina.respaldo.respaldarAhora()
                    }
                    Texto {
                        visible: !pagina.respaldo.puedeRespaldar && !pagina.respaldo.respaldando
                                 && (!pagina.respaldo.carpetaConfigurada || !pagina.respaldo.contrasenaConfigurada)
                        text: qsTr("Elige una carpeta y crea una contraseña para respaldar.")
                        font.pixelSize: Theme.leyenda.size
                    }
                }
                ColumnLayout {
                    objectName: "progresoRespaldo"
                    visible: pagina.respaldo.respaldando
                    spacing: Theme.espacioXs
                    Layout.fillWidth: true
                    Texto {
                        objectName: "faseRespaldo"
                        text: pagina.respaldo.textoFaseRespaldo.length > 0 ? pagina.respaldo.textoFaseRespaldo
                                                                           : qsTr("Respaldando…")
                        color: Theme.texto
                    }
                    ProgressBar {
                        objectName: "barraRespaldo"
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        indeterminate: pagina.respaldo.fraccionRespaldo < 0
                        value: Math.max(0, pagina.respaldo.fraccionRespaldo)
                        Accessible.name: qsTr("Progreso del respaldo")
                    }
                }
                AvisoEnLinea {
                    objectName: "resultadoRespaldo"
                    visible: pagina.respaldo.hayResultado && !pagina.respaldo.respaldando
                    variante: pagina.respaldo.varianteResultado
                    titulo: pagina.respaldo.tituloResultado
                    descripcion: pagina.respaldo.descripcionResultado
                    anunciar: false
                    Layout.fillWidth: true
                }
                Repeater {
                    model: pagina.respaldo.respaldando ? [] : pagina.respaldo.advertenciasResultado
                    delegate: Texto {
                        required property string modelData
                        objectName: "advertenciaRespaldo"
                        text: "• " + modelData
                        color: Theme.texto
                    }
                }
                AvisoEnLinea {
                    objectName: "avisoCopiasSinRegistro"
                    visible: pagina.respaldo.avisoCopiasSinRegistro.length > 0 && !pagina.respaldo.respaldando
                    variante: "advertencia"
                    titulo: qsTr("Revisa la carpeta del respaldo")
                    descripcion: pagina.respaldo.avisoCopiasSinRegistro
                    Layout.fillWidth: true
                }
            }

            // ---- Restaurar ----
            Seccion {
                objectName: "seccionRestaurar"
                visible: pagina.respaldo.restauracionDisponible
                titulo: qsTr("Restaurar")
                Layout.leftMargin: Theme.espacioL
                Layout.rightMargin: Theme.espacioL
                Texto {
                    text: qsTr("Sustituye los datos de este equipo por los de un respaldo. Primero se verifica todo el "
                               + "archivo; la e.firma no se restaura.")
                    color: Theme.texto
                }
                BotonAccion {
                    objectName: "botonRestaurar"
                    text: qsTr("Restaurar un respaldo…")
                    icono: "arrow-down-circle"
                    onClicked: dialogoRestaurar.open()
                }
            }

            Item { implicitHeight: Theme.espacioL }
        }
    }

    DialogoContrasenaRespaldo {
        id: dialogoContrasena
        respaldo: pagina.respaldo
    }
    DialogoClaveRecuperacion {
        id: dialogoClave
        respaldo: pagina.respaldo
    }
    DialogoRestaurarRespaldo {
        id: dialogoRestaurar
        respaldo: pagina.respaldo
    }
    DialogoConfirmacion {
        id: confirmacionDescarte
        objectName: "confirmacionDescarte"
        prefijoNombre: "confirmacionDescarte"
        parent: Overlay.overlay
        variante: "destructivo"
        title: qsTr("¿Descartar los datos anteriores?")
        mensaje: qsTr("Se borrarán los datos que se guardaron aparte al restaurar. No se puede deshacer.")
        consecuencia: qsTr("Solo se borran las carpetas que creó Archivo Fiscal; cualquier otra carpeta queda "
                           + "intacta. Después se limpian del llavero las e.firmas anteriores.")
        textoConfirmar: qsTr("Descartar")
        onConfirmado: pagina.respaldo.descartarDatosPrevios()
    }
}
