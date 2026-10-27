import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// T025.6 (D2, D16, C3): crear o cambiar la contrasena del respaldo.
// - Crear: contrasena y confirmacion. Al terminar, la pagina abre el dialogo
//   de la clave de recuperacion (RespaldoViewModel.claveRecuperacionLista).
// - Cambiar: contrasena anterior o clave de recuperacion (selector), nueva y
//   confirmacion; la clave de recuperacion no cambia.
// Secretos: los campos se vacian en el mismo manejador que invoca al view
// model, al cancelar y al cerrarse por cualquier via; nunca se enlazan a
// propiedades. Escape cierra (salvo con la operacion en curso). Enter en el
// ultimo campo envia.
DialogoRespaldoBase {
    id: dialogo
    objectName: "dialogoContrasenaRespaldo"

    required property RespaldoViewModel respaldo
    // "crear" o "cambiar".
    property string modo: "crear"
    property string verificacion: "contrasena" // "contrasena" o "clave"

    readonly property bool esCambio: dialogo.modo === "cambiar"
    readonly property bool procesando: dialogo.respaldo.procesandoFormulario
    readonly property alias campoVerificacion: campoVerificacion
    readonly property alias campoNueva: campoNueva
    readonly property alias campoConfirmacion: campoConfirmacion

    title: dialogo.esCambio ? qsTr("Cambiar la contraseña del respaldo") : qsTr("Crear la contraseña del respaldo")
    icono: "key"
    prefijoNombre: "dialogoContrasena"
    bloqueado: dialogo.procesando
    descripcionAccesible: textoExplicacion.text

    function limpiarCampos() {
        campoVerificacion.clear()
        campoNueva.clear()
        campoConfirmacion.clear()
    }

    function enfocarInicial() {
        if (dialogo.esCambio)
            campoVerificacion.forceActiveFocus(Qt.TabFocusReason)
        else
            campoNueva.forceActiveFocus(Qt.TabFocusReason)
    }

    function enviar() {
        if (dialogo.procesando)
            return
        if (dialogo.esCambio)
            dialogo.respaldo.cambiarContrasena(campoVerificacion.text,
                                               dialogo.verificacion === "clave" ? RespaldoViewModel.VerificarConClave
                                                                                : RespaldoViewModel.VerificarConContrasena,
                                               campoNueva.text, campoConfirmacion.text)
        else
            dialogo.respaldo.crearContrasena(campoNueva.text, campoConfirmacion.text)
        // Mismo manejador: los secretos no sobreviven a la llamada.
        dialogo.limpiarCampos()
    }

    onAboutToShow: {
        dialogo.verificacion = "contrasena"
        dialogo.respaldo.limpiarFormulario()
    }
    onOpened: dialogo.enfocarInicial()
    onClosed: {
        dialogo.limpiarCampos()
        dialogo.respaldo.limpiarFormulario()
    }
    Component.onDestruction: dialogo.limpiarCampos()

    Connections {
        target: dialogo.respaldo
        // Error tras enviar: el foco vuelve al primer campo (ya vacio).
        function onFormularioChanged() {
            if (dialogo.opened && !dialogo.procesando && dialogo.respaldo.errorFormulario.length > 0)
                Qt.callLater(dialogo.enfocarInicial)
        }
        function onContrasenaCambiada() {
            if (dialogo.opened)
                dialogo.close()
        }
        function onClaveRecuperacionLista() {
            if (dialogo.opened)
                dialogo.close()
        }
    }

    Label {
        id: textoExplicacion
        objectName: "explicacionContrasena"
        text: dialogo.esCambio
              ? qsTr("Las copias nuevas usarán la contraseña nueva. Tu clave de recuperación sigue siendo la misma "
                     + "y abre todas tus copias.")
              : qsTr("La contraseña cifra tus respaldos. Si la pierdes y también pierdes la clave de recuperación, "
                     + "no podrás abrirlos.")
        color: Theme.textoSecundario
        font.family: Theme.familia
        font.pixelSize: Theme.cuerpo.size
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        Layout.fillWidth: true
        Accessible.ignored: true
    }

    SelectorSegmentado {
        id: selectorVerificacion
        objectName: "selectorVerificacion"
        visible: dialogo.esCambio
        enabled: !dialogo.procesando
        nombreAccesible: qsTr("Verificar con")
        opciones: [
            { clave: "contrasena", texto: qsTr("Contraseña anterior") },
            { clave: "clave", texto: qsTr("Clave de recuperación") }
        ]
        valor: dialogo.verificacion
        onActivado: (clave) => {
            dialogo.verificacion = clave
            campoVerificacion.clear()
        }
    }

    CampoTexto {
        id: campoVerificacion
        objectName: "campoVerificacion"
        visible: dialogo.esCambio
        enabled: !dialogo.procesando
        Layout.fillWidth: true
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        placeholderText: dialogo.verificacion === "clave" ? qsTr("Clave de recuperación") : qsTr("Contraseña anterior")
        Accessible.name: placeholderText
        onAccepted: campoNueva.forceActiveFocus(Qt.TabFocusReason)
    }
    CampoTexto {
        id: campoNueva
        objectName: "campoNueva"
        enabled: !dialogo.procesando
        Layout.fillWidth: true
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        placeholderText: dialogo.esCambio ? qsTr("Contraseña nueva") : qsTr("Contraseña")
        Accessible.name: placeholderText
        Accessible.description: qsTr("Al menos 12 caracteres.")
        onAccepted: campoConfirmacion.forceActiveFocus(Qt.TabFocusReason)
    }
    CampoTexto {
        id: campoConfirmacion
        objectName: "campoConfirmacion"
        enabled: !dialogo.procesando
        Layout.fillWidth: true
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        placeholderText: qsTr("Confirmar la contraseña")
        Accessible.name: placeholderText
        onAccepted: dialogo.enviar()
    }

    Label {
        objectName: "ayudaLongitud"
        text: qsTr("Usa al menos 12 caracteres.")
        color: Theme.textoSecundario
        font.family: Theme.familia
        font.pixelSize: Theme.leyenda.size
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    AvisoEnLinea {
        objectName: "errorContrasena"
        visible: dialogo.respaldo.errorFormulario.length > 0
        variante: "error"
        descripcion: dialogo.respaldo.errorFormulario
        Layout.fillWidth: true
    }

    acciones: [
        Item { Layout.fillWidth: true },
        BotonAccion {
            objectName: "dialogoContrasenaCancelar"
            text: qsTr("Cancelar")
            enabled: !dialogo.procesando
            onClicked: dialogo.close()
        },
        BotonAccion {
            objectName: "dialogoContrasenaConfirmar"
            text: dialogo.esCambio ? qsTr("Cambiar contraseña") : qsTr("Crear contraseña")
            variante: "primario"
            enabled: !dialogo.procesando
            cargando: dialogo.procesando
            onClicked: dialogo.enviar()
        }
    ]
}
