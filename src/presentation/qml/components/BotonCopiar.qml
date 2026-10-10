import QtQuick

// T014.1 D6 (SUG-13): boton de icono "Copiar" para un identificador visible.
// Copia `valor` completo con Portapapeles y confirma de forma accesible: el
// icono pasa a checkmark, el ToolTip y el nombre accesible dicen "Copiado"
// durante 2 s y se anuncia `textoConfirmacion` (Accessible.announce).
// - valor: texto a copiar (si esta vacio el boton no se muestra).
// - descripcionValor: que se copia ("Id de solicitud SAT"), para el nombre
//   accesible "Copiar Id de solicitud SAT".
BotonAccion {
    id: boton

    property string valor: ""
    property string descripcionValor: ""
    readonly property bool copiado: temporizador.running
    readonly property string textoConfirmacion: qsTr("%1 copiado al portapapeles.").arg(boton.descripcionValor)

    signal copiadoSolicitado(bool exito)

    visible: boton.valor.length > 0
    variante: "icono"
    compacto: true
    icono: boton.copiado ? "checkmark" : "doc-on-doc"
    nombreAccesible: boton.copiado ? qsTr("Copiado") : qsTr("Copiar %1").arg(boton.descripcionValor)
    descripcion: boton.valor

    onClicked: {
        const exito = Portapapeles.copiar(boton.valor)
        if (exito) {
            temporizador.restart()
            boton.Accessible.announce(boton.textoConfirmacion)
        }
        boton.copiadoSolicitado(exito)
    }

    Timer {
        id: temporizador
        interval: 2000
    }
}
