pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// T025.6 (D5, D13, C5): restaurar un respaldo.
// 1. Elegir el `.afbak` (FileDialog nativo) y escribir la contrasena o la
//    clave de recuperacion (credencial automatica); "Verificar respaldo".
// 2. Verificacion en segundo plano con fase y progreso; "Cancelar
//    verificación" (Escape tambien cancela).
// 3. Resumen autenticado (fecha, identificador, version, conteos) y aviso de
//    lo que pasa al restaurar, incluido el unico aviso de relanzamiento (C5).
//    "Restaurar y reiniciar…" pide confirmacion; "Descartar" o cerrar el
//    dialogo descarta la preparacion.
// 4. Resultado de la solicitud (Relanzando, SinRelanzamiento,
//    FalloTrasDetener): la app sale enseguida; el dialogo ya no se cierra.
// El secreto se vacia en el mismo manejador que lo envia y al cerrar.
DialogoRespaldoBase {
    id: dialogo
    objectName: "dialogoRestaurar"

    required property RespaldoViewModel respaldo
    // Archivo elegido (solo para enviarlo; se muestra solo su nombre).
    property url archivo
    property string nombreArchivo: ""

    readonly property int estado: dialogo.respaldo.estadoRestauracion
    readonly property bool inactiva: dialogo.estado === RespaldoViewModel.Inactiva
    readonly property bool preparando: dialogo.estado === RespaldoViewModel.Preparando
    readonly property bool preparada: dialogo.estado === RespaldoViewModel.Preparada
    readonly property bool solicitando: dialogo.estado === RespaldoViewModel.Solicitando
    readonly property bool terminada: dialogo.estado === RespaldoViewModel.Relanzando
                                      || dialogo.estado === RespaldoViewModel.SinRelanzamiento
                                      || dialogo.estado === RespaldoViewModel.FalloTrasDetener
    readonly property bool confirmacionAbierta: confirmacion.visible
    readonly property alias campoSecreto: campoSecreto
    readonly property alias dialogoConfirmacion: confirmacion

    title: qsTr("Restaurar un respaldo")
    icono: "arrow-down-circle"
    prefijoNombre: "dialogoRestaurar"
    // Escape durante la verificacion la cancela (no cierra).
    bloqueado: dialogo.preparando || dialogo.solicitando || dialogo.terminada
    descripcionAccesible: qsTr("Restaurar sustituye tus datos actuales por los del respaldo.")

    function limpiarSecreto() {
        campoSecreto.clear()
    }

    function elegirArchivo() {
        // B3: abre en la carpeta del respaldo configurada, si la hay.
        const inicial = dialogo.respaldo.carpetaInicialRestauracion()
        const selector = selectorComp.createObject(dialogo, inicial.toString().length > 0 ? { currentFolder: inicial } : {})
        selector.accepted.connect(() => {
            dialogo.archivo = selector.selectedFile
            const partes = selector.selectedFile.toString().split("/")
            dialogo.nombreArchivo = decodeURIComponent(partes[partes.length - 1])
            selector.destroy()
            Qt.callLater(() => campoSecreto.forceActiveFocus(Qt.OtherFocusReason))
        })
        selector.rejected.connect(() => selector.destroy())
        selector.open()
    }

    function verificar() {
        if (!dialogo.inactiva || dialogo.nombreArchivo.length === 0)
            return
        dialogo.respaldo.prepararRestauracion(dialogo.archivo, campoSecreto.text, RespaldoViewModel.CredencialAutomatica)
        // Mismo manejador: el secreto no sobrevive a la llamada.
        dialogo.limpiarSecreto()
    }

    function enfocarEstado() {
        if (!dialogo.opened)
            return
        if (dialogo.inactiva)
            (dialogo.nombreArchivo.length > 0 ? campoSecreto : botonElegir).forceActiveFocus(Qt.OtherFocusReason)
        else if (dialogo.preparando)
            botonCancelarVerificacion.forceActiveFocus(Qt.OtherFocusReason)
        else if (dialogo.preparada)
            botonRestaurar.forceActiveFocus(Qt.OtherFocusReason)
    }

    onAboutToShow: {
        dialogo.archivo = ""
        dialogo.nombreArchivo = ""
    }
    onOpened: dialogo.enfocarEstado()
    onClosed: {
        dialogo.limpiarSecreto()
        if (dialogo.preparando)
            dialogo.respaldo.cancelarRestauracion()
        else if (dialogo.preparada)
            dialogo.respaldo.descartarRestauracion()
    }
    Component.onDestruction: campoSecreto.clear()
    onEstadoChanged: Qt.callLater(dialogo.enfocarEstado)

    // Escape mientras verifica: cancela la verificacion.
    Shortcut {
        sequence: "Escape"
        enabled: dialogo.opened && dialogo.preparando
        onActivated: dialogo.respaldo.cancelarRestauracion()
    }

    component Etiqueta: Label {
        color: Theme.textoSecundario
        font.family: Theme.familia
        font.pixelSize: Theme.cuerpo.size
        Accessible.ignored: true
    }
    component Valor: Label {
        color: Theme.texto
        font.family: Theme.familia
        font.pixelSize: Theme.cuerpo.size
        wrapMode: Text.WrapAnywhere
        textFormat: Text.PlainText
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
    }

    Component {
        id: selectorComp
        FileDialog {
            fileMode: FileDialog.OpenFile
            title: qsTr("Elegir un respaldo de Archivo Fiscal")
            nameFilters: [qsTr("Respaldo de Archivo Fiscal (*.afbak)")]
        }
    }

    // ---- 1. Archivo y credencial ----
    ColumnLayout {
        visible: dialogo.inactiva
        spacing: Theme.espacioS
        Layout.fillWidth: true

        RowLayout {
            spacing: Theme.espacioS
            Layout.fillWidth: true
            Label {
                objectName: "nombreArchivoRestaurar"
                text: dialogo.nombreArchivo.length > 0 ? dialogo.nombreArchivo : qsTr("Ningún archivo elegido")
                color: dialogo.nombreArchivo.length > 0 ? Theme.texto : Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.cuerpo.size
                elide: Text.ElideMiddle
                Layout.fillWidth: true
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Archivo: %1").arg(text)
            }
            BotonAccion {
                id: botonElegir
                objectName: "botonElegirArchivoRestaurar"
                text: qsTr("Elegir archivo…")
                icono: "folder"
                onClicked: dialogo.elegirArchivo()
            }
        }
        CampoTexto {
            id: campoSecreto
            objectName: "campoSecretoRestaurar"
            Layout.fillWidth: true
            enabled: dialogo.nombreArchivo.length > 0
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            placeholderText: qsTr("Contraseña o clave de recuperación del respaldo")
            Accessible.name: placeholderText
            onAccepted: dialogo.verificar()
        }
        Label {
            text: qsTr("Primero se verifica todo el respaldo; tus datos no cambian hasta que confirmes.")
            color: Theme.textoSecundario
            font.family: Theme.familia
            font.pixelSize: Theme.leyenda.size
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    // ---- 2. Verificando ----
    ColumnLayout {
        objectName: "progresoRestaurar"
        visible: dialogo.preparando || dialogo.solicitando
        spacing: Theme.espacioS
        Layout.fillWidth: true
        Label {
            objectName: "faseRestaurar"
            text: dialogo.solicitando ? qsTr("Preparando el reinicio…")
                  : dialogo.respaldo.textoFaseRestauracion.length > 0 ? dialogo.respaldo.textoFaseRestauracion
                  : qsTr("Verificando el respaldo…")
            color: Theme.texto
            font.family: Theme.familia
            font.pixelSize: Theme.cuerpo.size
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        ProgressBar {
            objectName: "barraRestaurar"
            Layout.fillWidth: true
            from: 0
            to: 1
            indeterminate: dialogo.solicitando || dialogo.respaldo.fraccionRestauracion < 0
            value: Math.max(0, dialogo.respaldo.fraccionRestauracion)
            Accessible.name: qsTr("Progreso de la verificación")
        }
        Label {
            visible: dialogo.preparando
            text: qsTr("Si el archivo está en la nube, puede tardar mientras se descarga.")
            color: Theme.textoSecundario
            font.family: Theme.familia
            font.pixelSize: Theme.leyenda.size
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    // ---- 3. Resumen autenticado ----
    GridLayout {
        objectName: "resumenRestaurar"
        visible: dialogo.preparada
        columns: 2
        columnSpacing: Theme.espacioM
        rowSpacing: Theme.espacioXs
        Layout.fillWidth: true

        Etiqueta { text: qsTr("Fecha del respaldo") }
        Valor {
            objectName: "fechaRestaurar"
            text: dialogo.respaldo.fechaRespaldoRestauracion
            font.weight: Font.DemiBold
            Accessible.name: qsTr("Fecha del respaldo: %1").arg(text)
        }
        Etiqueta { text: qsTr("Identificador") }
        Valor {
            objectName: "idRestaurar"
            text: dialogo.respaldo.idRespaldoRestauracion
            font.family: "Menlo"
            font.pixelSize: Theme.leyenda.size
            Accessible.name: qsTr("Identificador del respaldo: %1").arg(text)
        }
        Etiqueta { text: qsTr("Contenido") }
        Valor {
            objectName: "conteosRestaurar"
            text: dialogo.respaldo.conteosRestauracion
            wrapMode: Text.WordWrap
            Accessible.name: qsTr("Contenido: %1").arg(text)
        }
        Etiqueta {
            visible: dialogo.respaldo.versionAppRestauracion.length > 0
            text: qsTr("Versión")
        }
        Valor {
            visible: dialogo.respaldo.versionAppRestauracion.length > 0
            text: dialogo.respaldo.versionAppRestauracion
            wrapMode: Text.WordWrap
            Accessible.name: text
        }
    }
    AvisoEnLinea {
        objectName: "avisoMigracionRestaurar"
        visible: dialogo.preparada && dialogo.respaldo.avisoMigracionRestauracion.length > 0
        variante: "neutro"
        anunciar: false
        descripcion: dialogo.respaldo.avisoMigracionRestauracion
        Layout.fillWidth: true
    }
    AvisoEnLinea {
        objectName: "avisoConsecuenciasRestaurar"
        visible: dialogo.preparada
        variante: "advertencia"
        anunciar: false
        titulo: qsTr("Restaurar sustituye tus datos actuales")
        descripcion: qsTr("Tus datos actuales se guardan aparte hasta que los descartes. La e.firma no se restaura: "
                          + "tendrás que registrarla de nuevo en cada perfil. La aplicación se cerrará y debería "
                          + "abrirse sola; si no se abre, ábrela de nuevo para terminar la restauración.")
        Layout.fillWidth: true
    }

    // ---- 4. Resultado de la solicitud ----
    AvisoEnLinea {
        objectName: "resultadoSolicitudRestaurar"
        visible: dialogo.terminada
        variante: dialogo.respaldo.varianteSolicitud
        titulo: dialogo.respaldo.tituloSolicitud
        descripcion: dialogo.respaldo.descripcionSolicitud
        Layout.fillWidth: true
    }

    AvisoEnLinea {
        objectName: "errorRestaurar"
        visible: dialogo.respaldo.errorRestauracion.length > 0 && (dialogo.inactiva || dialogo.preparada)
        variante: "error"
        descripcion: dialogo.respaldo.errorRestauracion
        Layout.fillWidth: true
    }

    RowLayout {
        visible: !dialogo.terminada
        spacing: Theme.espacioS
        Layout.fillWidth: true
        Layout.topMargin: Theme.espacioXs
        BotonAccion {
            objectName: "botonDescartarRestaurar"
            visible: dialogo.preparada
            text: qsTr("Descartar")
            onClicked: dialogo.close() // onClosed descarta la preparacion
        }
        Item { Layout.fillWidth: true }
        BotonAccion {
            objectName: "botonCancelarRestaurar"
            visible: dialogo.inactiva
            text: qsTr("Cancelar")
            onClicked: dialogo.close()
        }
        BotonAccion {
            id: botonCancelarVerificacion
            objectName: "botonCancelarVerificacion"
            visible: dialogo.preparando
            text: qsTr("Cancelar verificación")
            onClicked: dialogo.respaldo.cancelarRestauracion()
        }
        BotonAccion {
            objectName: "botonVerificarRestaurar"
            visible: dialogo.inactiva
            text: qsTr("Verificar respaldo")
            variante: "primario"
            enabled: dialogo.nombreArchivo.length > 0
            onClicked: dialogo.verificar()
        }
        BotonAccion {
            id: botonRestaurar
            objectName: "botonRestaurarReiniciar"
            visible: dialogo.preparada || dialogo.solicitando
            text: qsTr("Restaurar y reiniciar…")
            variante: "destructivo"
            enabled: dialogo.preparada
            cargando: dialogo.solicitando
            onClicked: confirmacion.open()
        }
    }

    DialogoConfirmacion {
        id: confirmacion
        objectName: "confirmacionRestaurar"
        prefijoNombre: "confirmacionRestaurar"
        parent: Overlay.overlay
        variante: "destructivo"
        title: qsTr("¿Restaurar y reiniciar?")
        mensaje: qsTr("Tus datos actuales se sustituirán por los del respaldo del %1.")
                 .arg(dialogo.respaldo.fechaRespaldoRestauracion)
        consecuencia: qsTr("La aplicación se cerrará y debería abrirse sola para aplicar la restauración; "
                           + "si no se abre, ábrela de nuevo.")
        textoConfirmar: qsTr("Restaurar y reiniciar")
        onConfirmado: dialogo.respaldo.confirmarRestauracion()
    }
}
