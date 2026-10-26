pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// T026.3 (D3, D8-D10, D15, D16, D22; C4): exportar facturas a un ZIP. Solo
// representa ExportacionFacturasViewModel; las reglas (horizonte, plan,
// saneo del nombre, colisiones) son de C++. Lo abre y cierra el view model
// (dialogoAbierto); cerrar el dialogo llama cerrarDialogo().
// - Aviso D10 siempre visible.
// - Configuracion (Planeando/Dialogo): alcance (este mes, varios meses o
//   elegidas), rango con su error, direccion solo para mes o rango (D8), PDF,
//   conteo previsto (D16: con 0, "No hay facturas para exportar" y Exportar
//   deshabilitado), carpeta (FolderDialog; solo su nombre) y nombre editable.
// - Exportando: fase, barra y conteo; "Cancelar exportación".
// - EsperandoNombre: el nombre ya existe al guardar; otro nombre y "Guardar
//   con este nombre" (mismo archivo preparado) o cancelar.
// - Resultado: exportadas, sin PDF, omitidas, advertencia y el nombre del
//   archivo creado (nombrePublicado solo se muestra aqui).
// Teclado: Tab recorre los controles en orden; Escape cierra en
// Dialogo/Resultado y cancela en Exportando/EsperandoNombre.
DialogoRespaldoBase {
    id: dialogo
    objectName: "dialogoExportar"

    required property ExportacionFacturasViewModel exportacion

    readonly property int estado: dialogo.exportacion.estado
    readonly property bool configurando: dialogo.estado === ExportacionFacturasViewModel.Planeando
                                         || dialogo.estado === ExportacionFacturasViewModel.Dialogo
    readonly property bool planeando: dialogo.estado === ExportacionFacturasViewModel.Planeando
    readonly property bool exportando: dialogo.estado === ExportacionFacturasViewModel.Exportando
    readonly property bool esperandoNombre: dialogo.estado === ExportacionFacturasViewModel.EsperandoNombre
    readonly property bool terminado: dialogo.estado === ExportacionFacturasViewModel.Resultado
    property int estadoAnterior: ExportacionFacturasViewModel.Inactivo

    title: qsTr("Exportar facturas")
    icono: "arrow-down-circle"
    tono: "advertencia"
    prefijoNombre: "dialogoExportar"
    bloqueado: dialogo.exportando || dialogo.esperandoNombre
    descripcionAccesible: dialogo.exportacion.aviso

    function elegirCarpeta() {
        const inicial = dialogo.exportacion.carpetaInicial()
        const selector = selectorComp.createObject(dialogo, inicial.toString().length > 0 ? { currentFolder: inicial } : {})
        selector.accepted.connect(() => {
            dialogo.exportacion.elegirDestino(selector.selectedFolder)
            selector.destroy()
            Qt.callLater(() => campoNombre.forceActiveFocus(Qt.OtherFocusReason))
        })
        selector.rejected.connect(() => selector.destroy())
        selector.open()
    }

    function enfocarEstado() {
        if (!dialogo.opened)
            return
        const anterior = dialogo.estadoAnterior
        dialogo.estadoAnterior = dialogo.estado
        if (dialogo.exportando)
            botonCancelarExportacion.forceActiveFocus(Qt.OtherFocusReason)
        else if (dialogo.esperandoNombre)
            campoNombre.forceActiveFocus(Qt.OtherFocusReason)
        else if (dialogo.terminado)
            botonCerrar.forceActiveFocus(Qt.OtherFocusReason)
        else if (dialogo.configurando && anterior === ExportacionFacturasViewModel.Exportando)
            // Tras un error de destino: el nombre.
            campoNombre.forceActiveFocus(Qt.OtherFocusReason)
        // Al abrir: el alcance, cuando el selector ya esta habilitado (el
        // primer plan puede seguir en curso al abrir).
        if (dialogo.focoInicialPendiente && dialogo.configurando && !dialogo.planeando) {
            dialogo.focoInicialPendiente = false
            selectorModo.enfocar(Qt.OtherFocusReason)
        }
    }

    property bool focoInicialPendiente: false

    onOpened: {
        dialogo.estadoAnterior = ExportacionFacturasViewModel.Inactivo
        dialogo.focoInicialPendiente = true
        dialogo.enfocarEstado()
    }
    onClosed: {
        dialogo.focoInicialPendiente = false
        dialogo.estadoAnterior = ExportacionFacturasViewModel.Inactivo
        if (dialogo.exportacion.dialogoAbierto)
            dialogo.exportacion.cerrarDialogo()
    }
    onEstadoChanged: Qt.callLater(dialogo.enfocarEstado)

    // Escape con un trabajo en curso: cancela (el dialogo no se cierra).
    Shortcut {
        sequence: "Escape"
        enabled: dialogo.opened && dialogo.bloqueado
        onActivated: dialogo.exportacion.cancelar()
    }

    component Texto: Label {
        color: Theme.texto
        font.family: Theme.familia
        font.pixelSize: Theme.cuerpo.size
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
    component Etiqueta: Label {
        color: Theme.textoSecundario
        font.family: Theme.familia
        font.pixelSize: Theme.etiqueta.size
        font.weight: Font.DemiBold
        Accessible.ignored: true
    }

    Component {
        id: selectorComp
        FolderDialog {
            title: qsTr("Elegir dónde guardar la exportación")
        }
    }

    // ---- Aviso D10 (siempre visible) ----
    AvisoEnLinea {
        objectName: "avisoExportar"
        variante: "advertencia"
        icono: "exclamation-lock"
        anunciar: false
        descripcion: dialogo.exportacion.aviso
        Layout.fillWidth: true
    }

    // ---- Configuracion ----
    ColumnLayout {
        objectName: "configuracionExportar"
        visible: dialogo.configurando
        spacing: Theme.espacioS
        Layout.fillWidth: true

        Etiqueta { text: qsTr("Qué exportar") }
        SelectorSegmentado {
            id: selectorModo
            objectName: "selectorModoExportar"
            nombreAccesible: qsTr("Qué exportar")
            enabled: !dialogo.planeando
            opciones: [
                { clave: "modoMes", texto: qsTr("Este mes") },
                { clave: "modoRango", texto: qsTr("Varios meses") },
                { clave: "modoSeleccion", texto: qsTr("Elegidas (%1)").arg(dialogo.exportacion.conteoSeleccionadas) }
            ]
            valor: dialogo.exportacion.modo === ExportacionFacturasViewModel.Rango ? "modoRango"
                   : dialogo.exportacion.modo === ExportacionFacturasViewModel.Seleccion ? "modoSeleccion" : "modoMes"
            onActivado: (clave) => dialogo.exportacion.cambiarModo(
                            clave === "modoRango" ? ExportacionFacturasViewModel.Rango
                            : clave === "modoSeleccion" ? ExportacionFacturasViewModel.Seleccion
                            : ExportacionFacturasViewModel.Mes)
        }
        Texto {
            objectName: "alcanceExportar"
            visible: text.length > 0 && dialogo.exportacion.modo !== ExportacionFacturasViewModel.Rango
            text: dialogo.exportacion.textoAlcance
            font.weight: Font.DemiBold
        }

        // Rango de meses (solo los del horizonte de Facturas).
        GridLayout {
            objectName: "rangoExportar"
            visible: dialogo.exportacion.modo === ExportacionFacturasViewModel.Rango
            columns: 2
            columnSpacing: Theme.espacioS
            rowSpacing: Theme.espacioXs
            Layout.fillWidth: true
            Etiqueta { text: qsTr("Desde") }
            Etiqueta { text: qsTr("Hasta") }
            CampoCombo {
                id: comboDesde
                objectName: "comboDesdeExportar"
                model: dialogo.exportacion.opcionesMeses
                textRole: "texto"
                focusPolicy: Qt.StrongFocus
                enabled: !dialogo.planeando
                conError: dialogo.exportacion.errorRango.length > 0
                currentIndex: dialogo.exportacion.indiceOpcionMes(dialogo.exportacion.desdeAnio, dialogo.exportacion.desdeMes)
                Layout.fillWidth: true
                Accessible.name: qsTr("Desde el mes")
                Accessible.description: displayText
                onActivated: (indice) => {
                    const m = dialogo.exportacion.opcionesMeses[indice]
                    dialogo.exportacion.fijarRango(m.anio, m.mes, dialogo.exportacion.hastaAnio, dialogo.exportacion.hastaMes)
                }
            }
            CampoCombo {
                id: comboHasta
                objectName: "comboHastaExportar"
                model: dialogo.exportacion.opcionesMeses
                textRole: "texto"
                focusPolicy: Qt.StrongFocus
                enabled: !dialogo.planeando
                conError: dialogo.exportacion.errorRango.length > 0
                currentIndex: dialogo.exportacion.indiceOpcionMes(dialogo.exportacion.hastaAnio, dialogo.exportacion.hastaMes)
                Layout.fillWidth: true
                Accessible.name: qsTr("Hasta el mes")
                Accessible.description: displayText
                onActivated: (indice) => {
                    const m = dialogo.exportacion.opcionesMeses[indice]
                    dialogo.exportacion.fijarRango(dialogo.exportacion.desdeAnio, dialogo.exportacion.desdeMes, m.anio, m.mes)
                }
            }
        }
        Texto {
            objectName: "errorRangoExportar"
            visible: text.length > 0
            text: dialogo.exportacion.errorRango
            color: Theme.error
            Accessible.role: Accessible.AlertMessage
        }

        // D8: direccion solo para mes o rango.
        Etiqueta {
            visible: dialogo.exportacion.mostrarDireccion
            text: qsTr("Facturas")
        }
        SelectorSegmentado {
            objectName: "selectorDireccionExportar"
            visible: dialogo.exportacion.mostrarDireccion
            nombreAccesible: qsTr("Facturas a exportar")
            enabled: !dialogo.planeando
            opciones: [
                { clave: "expEmitidas", texto: qsTr("Emitidas") },
                { clave: "expRecibidas", texto: qsTr("Recibidas") },
                { clave: "expAmbas", texto: qsTr("Ambas") }
            ]
            valor: dialogo.exportacion.direccion === ExportacionFacturasViewModel.Recibidas ? "expRecibidas"
                   : dialogo.exportacion.direccion === ExportacionFacturasViewModel.Ambas ? "expAmbas" : "expEmitidas"
            onActivado: (clave) => {
                dialogo.exportacion.direccion = clave === "expRecibidas" ? ExportacionFacturasViewModel.Recibidas
                                              : clave === "expAmbas" ? ExportacionFacturasViewModel.Ambas
                                              : ExportacionFacturasViewModel.Emitidas
            }
        }

        CheckBox {
            id: casillaPdf
            objectName: "casillaPdfExportar"
            text: qsTr("Incluir el PDF de cada factura")
            checked: dialogo.exportacion.conPdf
            font.family: Theme.familia
            font.pixelSize: Theme.cuerpo.size
            Accessible.name: text
            onToggled: {
                dialogo.exportacion.conPdf = casillaPdf.checked
                casillaPdf.checked = Qt.binding(() => dialogo.exportacion.conPdf)
            }
        }

        // D16: conteo previsto.
        RowLayout {
            spacing: Theme.espacioS
            Layout.fillWidth: true
            BusyIndicator {
                visible: dialogo.planeando
                running: visible
                implicitWidth: 18
                implicitHeight: 18
                Accessible.ignored: true
            }
            Texto {
                objectName: "conteoExportar"
                text: dialogo.planeando ? qsTr("Contando las facturas…") : dialogo.exportacion.textoConteo
                font.weight: Font.DemiBold
            }
        }
        Repeater {
            model: dialogo.exportacion.avisosPlan
            delegate: Texto {
                required property string modelData
                text: modelData
                color: Theme.textoSecundario
                font.pixelSize: Theme.leyenda.size
            }
        }
        AvisoEnLinea {
            objectName: "errorPlanExportar"
            visible: dialogo.exportacion.errorPlan.length > 0
            variante: "error"
            descripcion: dialogo.exportacion.errorPlan
            textoAccion: qsTr("Reintentar")
            objectNameAccion: "botonReintentarPlanExportar"
            Layout.fillWidth: true
            onAccionSolicitada: dialogo.exportacion.planear()
        }

        // Destino (D15): carpeta (solo su nombre) y nombre del archivo.
        Etiqueta { text: qsTr("Guardar en") }
        RowLayout {
            spacing: Theme.espacioS
            Layout.fillWidth: true
            Label {
                objectName: "carpetaExportar"
                text: dialogo.exportacion.hayCarpetaDestino ? dialogo.exportacion.nombreCarpetaDestino
                                                            : qsTr("Ninguna carpeta elegida")
                color: dialogo.exportacion.hayCarpetaDestino ? Theme.texto : Theme.textoSecundario
                font.family: Theme.familia
                font.pixelSize: Theme.cuerpo.size
                elide: Text.ElideMiddle
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Carpeta: %1").arg(text)
            }
            BotonAccion {
                id: botonElegirCarpeta
                objectName: "botonElegirCarpetaExportar"
                text: qsTr("Elegir carpeta…")
                icono: "folder"
                onClicked: dialogo.elegirCarpeta()
            }
        }
    }

    // Nombre del archivo: en la configuracion y al esperar otro nombre.
    ColumnLayout {
        visible: dialogo.configurando || dialogo.esperandoNombre
        spacing: Theme.espacioXs
        Layout.fillWidth: true
        Etiqueta { text: qsTr("Nombre del archivo") }
        CampoTexto {
            id: campoNombre
            objectName: "campoNombreExportar"
            Layout.fillWidth: true
            text: dialogo.exportacion.nombreZip
            conError: dialogo.exportacion.errorDestino.length > 0
            inputMethodHints: Qt.ImhNoPredictiveText
            placeholderText: dialogo.exportacion.nombreSugerido
            Accessible.name: qsTr("Nombre del archivo")
            Accessible.description: dialogo.exportacion.errorDestino
            onTextEdited: dialogo.exportacion.nombreZip = text
            onAccepted: {
                if (dialogo.esperandoNombre)
                    dialogo.exportacion.publicarConNombre()
                else if (dialogo.exportacion.puedeIniciar)
                    dialogo.exportacion.iniciar()
            }
        }
    }
    AvisoEnLinea {
        objectName: "errorDestinoExportar"
        visible: dialogo.exportacion.errorDestino.length > 0 && (dialogo.configurando || dialogo.esperandoNombre)
        variante: dialogo.esperandoNombre ? "advertencia" : "error"
        descripcion: dialogo.exportacion.errorDestino
        Layout.fillWidth: true
    }

    // ---- Progreso ----
    ColumnLayout {
        objectName: "progresoExportar"
        visible: dialogo.exportando
        spacing: Theme.espacioS
        Layout.fillWidth: true
        Texto {
            objectName: "faseExportar"
            text: dialogo.exportacion.textoFase.length > 0 ? dialogo.exportacion.textoFase : qsTr("Preparando la exportación…")
        }
        ProgressBar {
            objectName: "barraExportar"
            Layout.fillWidth: true
            from: 0
            to: 1
            indeterminate: dialogo.exportacion.fraccion < 0
            value: Math.max(0, dialogo.exportacion.fraccion)
            Accessible.name: qsTr("Progreso de la exportación")
        }
        Texto {
            objectName: "conteoProgresoExportar"
            visible: dialogo.exportacion.textoProgreso.length > 0
            text: qsTr("%1 · %2 exportadas").arg(dialogo.exportacion.textoProgreso).arg(dialogo.exportacion.exportadasProgreso)
            color: Theme.textoSecundario
            font.pixelSize: Theme.leyenda.size
        }
    }

    // ---- Resultado ----
    AvisoEnLinea {
        objectName: "resultadoExportar"
        visible: dialogo.terminado
        variante: dialogo.exportacion.varianteResultado
        titulo: dialogo.exportacion.tituloResultado
        descripcion: dialogo.exportacion.descripcionResultado
        Layout.fillWidth: true
    }
    Repeater {
        model: dialogo.terminado ? dialogo.exportacion.detallesResultado : []
        delegate: Texto {
            required property string modelData
            text: modelData
        }
    }
    AvisoEnLinea {
        objectName: "advertenciaResultadoExportar"
        visible: dialogo.terminado && dialogo.exportacion.advertenciaResultado.length > 0
        variante: "advertencia"
        anunciar: false
        descripcion: dialogo.exportacion.advertenciaResultado
        Layout.fillWidth: true
    }
    Texto {
        objectName: "archivoCreadoExportar"
        visible: dialogo.terminado && dialogo.exportacion.nombrePublicado.length > 0
        text: qsTr("Archivo creado: %1").arg(dialogo.exportacion.nombrePublicado)
        wrapMode: Text.WrapAnywhere
    }

    // ---- Botones ----
    RowLayout {
        spacing: Theme.espacioS
        Layout.fillWidth: true
        Layout.topMargin: Theme.espacioXs
        Item { Layout.fillWidth: true }
        BotonAccion {
            objectName: "botonCancelarExportar"
            visible: dialogo.configurando
            text: qsTr("Cancelar")
            onClicked: dialogo.close()
        }
        BotonAccion {
            objectName: "botonExportar"
            visible: dialogo.configurando
            text: qsTr("Exportar")
            variante: "primario"
            enabled: dialogo.exportacion.puedeIniciar
            descripcion: dialogo.exportacion.conteoPrevisto === 0 && dialogo.exportacion.hayPlan
                         ? dialogo.exportacion.textoConteo
                         : !dialogo.exportacion.hayCarpetaDestino ? qsTr("Elige una carpeta para guardar el archivo") : ""
            onClicked: dialogo.exportacion.iniciar()
        }
        BotonAccion {
            id: botonCancelarExportacion
            objectName: "botonCancelarExportacion"
            visible: dialogo.exportando || dialogo.esperandoNombre
            text: qsTr("Cancelar exportación")
            onClicked: dialogo.exportacion.cancelar()
        }
        BotonAccion {
            objectName: "botonGuardarConNombre"
            visible: dialogo.esperandoNombre
            text: qsTr("Guardar con este nombre")
            variante: "primario"
            enabled: dialogo.exportacion.nombreZip.trim().length > 0
            onClicked: dialogo.exportacion.publicarConNombre()
        }
        BotonAccion {
            id: botonCerrar
            objectName: "botonCerrarExportar"
            visible: dialogo.terminado
            text: qsTr("Cerrar")
            variante: "primario"
            onClicked: dialogo.close()
        }
    }
}
