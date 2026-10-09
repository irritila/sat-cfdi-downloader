import QtQuick

// Icono de linea de la entrega de diseno (T013 D4), tenido con un token.
//
//   Icono { nombre: "check-circle"; color: Theme.texto; tamano: 16 }
//
// - nombre: archivo de assets/icons/ sin extension (p. ej. el `icono` de
//   Theme.estado(clave)). Vacio = no dibuja nada.
// - color: color exacto del trazo (por omision Theme.texto).
// - tamano: lado en puntos (13 badges, 16 botones, 18 avisos, 48 vacios).
// Es decorativo: el texto que lo acompana lleva el significado.
Item {
    id: icono

    property string nombre: ""
    property color color: Theme.texto
    property int tamano: 16

    implicitWidth: icono.tamano
    implicitHeight: icono.tamano
    Accessible.ignored: true

    IconoSvg {
        objectName: "iconoSvg"
        anchors.fill: parent
        fuente: icono.nombre.length > 0 ? Qt.resolvedUrl("assets/icons/" + icono.nombre + ".svg") : ""
        color: icono.color
    }
}
