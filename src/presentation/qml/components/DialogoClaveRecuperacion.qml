import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// T025.6 (D6, C3, C4): clave de recuperacion de unica visualizacion.
// - `clave` se pide al view model al abrir (tomarClaveRecuperacion) y se
//   vacia al cerrar o destruir el dialogo. El view model la conserva hasta
//   "Ya la guardé" (olvidarClaveRecuperacion, H2): si la pagina se descarga
//   con el dialogo abierto, al volver se muestra otra vez.
// - "Copiar" (portapapeles solo de este equipo, borrado a los 90 s) e
//   "Imprimir…" (el panel permite Guardar como PDF y la cola puede
//   conservarla), cada uno con su aviso; sin "Guardar como archivo". Cada
//   boton solo si hay su puerto; mientras el panel de impresion esta abierto
//   no se puede copiar, imprimir ni cerrar.
// - Nombre accesible: la clave caracter por caracter, con pausa entre grupos.
// - No se cierra con Escape ni con clic fuera (la clave no se vuelve a
//   mostrar): solo con "Ya la guardé". Foco inicial en "Copiar".
DialogoRespaldoBase {
    id: dialogo
    objectName: "dialogoClaveRecuperacion"

    required property RespaldoViewModel respaldo
    property string clave: ""
    // Resultado visible de Copiar/Imprimir (sin la clave).
    property string confirmacion: ""
    // B5: "A B C D, E F G H, ..." para VoiceOver.
    readonly property string claveDeletreada: dialogo.clave.split("-")
                                              .map(g => g.split("").join(" ")).join(", ")

    title: qsTr("Guarda tu clave de recuperación")
    icono: "key"
    tono: "advertencia"
    prefijoNombre: "dialogoClave"
    closePolicy: Popup.NoAutoClose
    descripcionAccesible: textoExplicacion.text

    onAboutToShow: {
        dialogo.confirmacion = ""
        dialogo.clave = dialogo.respaldo.tomarClaveRecuperacion()
    }
    onOpened: (botonCopiar.visible ? botonCopiar : botonGuardada).forceActiveFocus(Qt.TabFocusReason)
    // Cerrar o destruir solo vacia el texto; el view model olvida la clave
    // unicamente con "Ya la guardé".
    onClosed: {
        dialogo.clave = ""
        dialogo.confirmacion = ""
    }
    Component.onDestruction: dialogo.clave = ""

    function confirmarGuardada() {
        if (dialogo.respaldo.imprimiendo)
            return
        dialogo.respaldo.olvidarClaveRecuperacion()
        dialogo.close()
    }

    Label {
        id: textoExplicacion
        text: qsTr("Con esta clave puedes abrir tus respaldos si olvidas la contraseña. No se volverá a mostrar "
                   + "después de que confirmes que la guardaste: anótala o imprímela y guárdala en un lugar seguro.")
        color: Theme.texto
        font.family: Theme.familia
        font.pixelSize: Theme.cuerpo.size
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        Layout.fillWidth: true
        Accessible.ignored: true
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: textoClave.implicitHeight + 2 * Theme.espacioM
        radius: Theme.radioControl
        color: Theme.superficieSeccion
        border.width: 1
        border.color: Theme.separador
        Label {
            id: textoClave
            objectName: "textoClaveRecuperacion"
            anchors.fill: parent
            anchors.margins: Theme.espacioM
            text: dialogo.clave
            color: Theme.texto
            font.family: "Menlo"
            font.pixelSize: Theme.subtitulo.size
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WrapAnywhere
            textFormat: Text.PlainText
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Clave de recuperación: %1").arg(dialogo.claveDeletreada)
        }
    }

    AvisoEnLinea {
        objectName: "avisoCopiasClave"
        variante: "advertencia"
        anunciar: false
        descripcion: [
            dialogo.respaldo.puedeCopiar
                ? qsTr("Copiar la deja en el portapapeles de este equipo durante 90 segundos; no pasa a tus otros "
                       + "dispositivos.") : "",
            dialogo.respaldo.puedeImprimir
                ? qsTr("Al imprimir, el panel de macOS también permite Guardar como PDF o abrirla en Vista Previa, "
                       + "y la cola de impresión puede conservar una copia.") : "",
            qsTr("Archivo Fiscal no la guarda.")
        ].filter(t => t.length > 0).join(" ")
        Layout.fillWidth: true
    }

    Label {
        objectName: "confirmacionClave"
        visible: text.length > 0
        text: dialogo.confirmacion
        color: Theme.textoSecundario
        font.family: Theme.familia
        font.pixelSize: Theme.leyenda.size
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    RowLayout {
        spacing: Theme.espacioS
        Layout.fillWidth: true
        Layout.topMargin: Theme.espacioXs
        BotonAccion {
            id: botonCopiar
            objectName: "botonCopiarClave"
            visible: dialogo.respaldo.puedeCopiar
            text: qsTr("Copiar")
            icono: "doc-on-doc"
            enabled: !dialogo.respaldo.imprimiendo && dialogo.clave.length > 0
            onClicked: {
                dialogo.confirmacion = dialogo.respaldo.copiarClaveRecuperacion()
                        ? qsTr("Clave copiada al portapapeles; se borrará en 90 segundos.")
                        : qsTr("No se pudo copiar la clave.")
                botonCopiar.Accessible.announce(dialogo.confirmacion)
            }
        }
        BotonAccion {
            id: botonImprimir
            objectName: "botonImprimirClave"
            visible: dialogo.respaldo.puedeImprimir
            text: qsTr("Imprimir…")
            enabled: !dialogo.respaldo.imprimiendo && dialogo.clave.length > 0
            cargando: dialogo.respaldo.imprimiendo
            onClicked: {
                // Panel sincrono y app-modal; false = cancelado o fallo (neutro).
                dialogo.confirmacion = dialogo.respaldo.imprimirClaveRecuperacion()
                        ? qsTr("Clave enviada a la impresora.") : qsTr("No se imprimió la clave.")
                botonImprimir.Accessible.announce(dialogo.confirmacion)
            }
        }
        Item { Layout.fillWidth: true }
        BotonAccion {
            id: botonGuardada
            objectName: "botonClaveGuardada"
            text: qsTr("Ya la guardé")
            variante: "primario"
            enabled: !dialogo.respaldo.imprimiendo
            onClicked: dialogo.confirmarGuardada()
        }
    }
}
