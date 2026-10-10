import QtQuick
import QtQuick.Controls.Basic

import "Etiquetas.js" as Etiquetas

// Insignia de estado (T013, ficha EstadoBadge): siempre icono, texto y tono
// del alias del estado (Theme.estado / Theme.estadoEFirma); el color nunca va
// solo. Variantes: normal (alto 20, icono 13, texto 11/600) y `grande` (alto 26,
// icono 16, texto 13/600).
// - clave: clave estable del estado (solicitud, paquete o, con `eFirma`, la
//   preparacion del perfil). Vacia -> tono neutro.
// - texto: texto visible (por omision el de estadoResumen).
// - contexto: prefijo del nombre accesible ("Estado", "Estado SAT"...).
// - tonoDirecto/iconoDirecto (T014.3): para avisos que no son un estado
//   ("Vence en N días"): tono de Theme.tono() e icono propios.
Rectangle {
    id: badge

    property string clave: ""
    property string texto: Etiquetas.estadoResumen(clave)
    property string contexto: qsTr("Estado")
    property bool eFirma: false
    property bool grande: false
    property string tonoDirecto: ""
    property string iconoDirecto: ""

    readonly property var estilo: badge.tonoDirecto.length > 0
                                  ? Object.assign({ icono: badge.iconoDirecto }, Theme.tono(badge.tonoDirecto))
                                  : badge.eFirma ? Theme.estadoEFirma(badge.clave) : Theme.estado(badge.clave)
    readonly property string tono: badge.estilo.tono
    readonly property string icono: badge.estilo.icono

    // Padding 6/8 (8/10 en grande), icono 13 (16) y gap espacioXs.
    readonly property int padIzq: badge.grande ? 8 : 6
    readonly property int padDer: badge.grande ? 10 : 8
    readonly property int anchoIcono: badge.grande ? 16 : 13

    implicitHeight: badge.grande ? 26 : Theme.altoBadge
    implicitWidth: badge.padIzq + badge.anchoIcono + Theme.espacioXs + etiqueta.implicitWidth + badge.padDer
    radius: height / 2
    color: badge.estilo.fondo
    border.width: 1
    border.color: badge.estilo.borde

    Accessible.role: Accessible.StaticText
    Accessible.name: badge.contexto + ": " + badge.texto

    Row {
        id: fila
        x: badge.padIzq
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.espacioXs

        Icono {
            objectName: "iconoBadge"
            nombre: badge.estilo.icono
            color: badge.estilo.texto
            tamano: badge.anchoIcono
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            id: etiqueta
            objectName: "textoBadge"
            // Se recorta solo si el badge recibe menos ancho que el implicito.
            width: Math.max(0, Math.min(implicitWidth, badge.width - badge.padIzq - badge.padDer
                                                       - badge.anchoIcono - Theme.espacioXs))
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: badge.texto
            color: badge.estilo.texto
            font.family: Theme.familia
            font.pixelSize: badge.grande ? Theme.cuerpo.size : Theme.etiqueta.size
            font.weight: Font.DemiBold
            Accessible.ignored: true
        }
    }
}
