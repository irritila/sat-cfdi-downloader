import QtQuick
import QtQuick.Layouts

// T025.6 (C1): franja global de la restauracion, debajo de la del T019 y
// arriba de todas las paginas. Patron de FranjaAvisoRaizPaquetes.
// - Resultado de la restauracion al arrancar (Aplicada, Descartada o
//   Revertida): se cierra con "Entendido" (solo durante la sesion).
// - H2: clave de recuperacion pendiente de guardar (fuera de Respaldo): su
//   accion abre Respaldo, que muestra el dialogo de la clave.
// - Datos previos: persistente mientras existan. Su accion solo navega a
//   Respaldo (revisarSolicitado); el descarte se confirma en la pagina. En la
//   propia pagina (`enRespaldo`) la accion se oculta.
// Sin avisos no ocupa espacio. No toma el foco al aparecer.
Rectangle {
    id: franja
    objectName: "franjaAvisosRespaldo"

    required property RespaldoViewModel respaldo
    property bool enRespaldo: false

    signal revisarSolicitado()

    readonly property bool clavePendiente: franja.respaldo.hayClavePendiente && !franja.enRespaldo

    visible: franja.respaldo.avisoArranqueActivo || franja.respaldo.hayDatosPrevios || franja.clavePendiente
    implicitHeight: franja.visible ? columna.implicitHeight + 2 * Theme.espacioM + separador.height : 0
    color: Theme.superficie

    ColumnLayout {
        id: columna
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.espacioL
        anchors.rightMargin: Theme.espacioL
        anchors.topMargin: Theme.espacioM
        spacing: Theme.espacioS

        AvisoEnLinea {
            objectName: "avisoArranqueRespaldo"
            visible: franja.respaldo.avisoArranqueActivo
            variante: franja.respaldo.varianteArranque
            titulo: franja.respaldo.tituloArranque
            descripcion: franja.respaldo.descripcionArranque
            textoAccion: qsTr("Entendido")
            objectNameAccion: "botonEntendidoArranque"
            Layout.fillWidth: true
            onAccionSolicitada: franja.respaldo.cerrarAvisoArranque()
        }
        AvisoEnLinea {
            objectName: "avisoClavePendiente"
            visible: franja.clavePendiente
            variante: "advertencia"
            titulo: qsTr("Guarda tu clave de recuperación")
            descripcion: qsTr("La contraseña del respaldo ya se creó. Abre Respaldo para ver tu clave de recuperación "
                              + "y guardarla.")
            textoAccion: qsTr("Ver la clave")
            objectNameAccion: "botonVerClavePendiente"
            Layout.fillWidth: true
            onAccionSolicitada: franja.revisarSolicitado()
        }
        AvisoEnLinea {
            objectName: "avisoDatosPrevios"
            visible: franja.respaldo.hayDatosPrevios
            variante: "advertencia"
            titulo: franja.respaldo.tituloDatosPrevios
            descripcion: franja.respaldo.descripcionDatosPrevios
            textoAccion: franja.enRespaldo ? "" : qsTr("Revisar en Respaldo")
            objectNameAccion: "botonRevisarDatosPrevios"
            Layout.fillWidth: true
            onAccionSolicitada: franja.revisarSolicitado()
        }
    }

    Rectangle {
        id: separador
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.separador
    }
}
