import QtQuick
import QtQuick.Controls

// Boton accesible por teclado: recibe foco con Tab aunque macOS limite el foco
// a campos de texto, y se activa con Espacio, Enter o Return.
Button {
    id: boton

    property string descripcion: ""

    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: boton.text
    Accessible.description: boton.descripcion

    Keys.onReturnPressed: (evento) => { if (boton.enabled) boton.click(); evento.accepted = true }
    Keys.onEnterPressed: (evento) => { if (boton.enabled) boton.click(); evento.accepted = true }
}
