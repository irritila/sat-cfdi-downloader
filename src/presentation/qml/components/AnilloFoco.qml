import QtQuick

// Anillo de foco del diseno (T013): 2 px en Theme.foco, separado 2 px del
// control (o 2 px hacia dentro con `interior`, para filas). Se coloca dentro
// del fondo del control y se enlaza `visible` a su foco de teclado.
Rectangle {
    id: anillo

    // Radio del control al que rodea.
    property real radioBase: 0
    property bool interior: false

    anchors.fill: parent
    anchors.margins: anillo.interior ? 2 : -4
    radius: anillo.interior ? Math.max(0, anillo.radioBase - 2) : anillo.radioBase + 4
    color: "transparent"
    border.width: 2
    border.color: Theme.foco
    visible: false
}
