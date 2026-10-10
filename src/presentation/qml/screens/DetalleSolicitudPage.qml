pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import "Etiquetas.js" as Etiquetas

// Detalle de una solicitud (T013, traspaso "DetalleSolicitudPage";
// UX-22..UX-30, D7). Estados: Cargando, Error, NoEncontrada y ConDatos.
// - Encabezado: "Solicitud" con subtitulo "RFC · Tipo · periodo", "Abrir
//   carpeta de la solicitud" y "Eliminar…" (destructivo, separado).
// - ResumenEstado: un solo estado visible (el resumen), titular y descripcion
//   por estado, 4 datos clave y la accion principal del estado (Enviar,
//   Verificar ahora, Reintentar descarga o Abrir carpeta) con su motivo o
//   resultado debajo.
// - Pestanas Paquetes N / Datos / Historial N (Paquetes por omision si hay
//   paquetes). El estado local y el SAT solo aparecen en Datos (D7).
// Teclado: foco inicial en "Solicitudes"; Tab recorre encabezado, accion
// principal, pestanas y contenido; Escape regresa.
Page {
    id: pagina
    objectName: "paginaDetalleSolicitud"

    required property AppViewModel app
    required property SolicitudDetailViewModel detalle

    readonly property bool sinEstadoSat: pagina.detalle.estadoSat === null
    readonly property bool conDatos: pagina.detalle.cargada
    readonly property bool hayPaquetes: pagina.detalle.totalPaquetes > 0
    readonly property bool anchoAmplio: pagina.width >= 760

    // Pestana activa: "paquetes", "datos" o "historial".
    property string pestana: "datos"

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Detalle de solicitud")

    background: Rectangle {
        color: Theme.fondo
    }

    // Foco inicial diferido: la pagina ya esta en la ventana.
    Component.onCompleted: Qt.callLater(pagina.enfocarInicial)

    // Al cargar otra solicitud, Paquetes si tiene paquetes; si no, Datos.
    Connections {
        target: pagina.detalle
        function onSolicitudIdChanged() { pagina.pestanaInicialPendiente = true }
        function onDatosChanged() {
            if (pagina.pestanaInicialPendiente && pagina.conDatos) {
                pagina.pestana = pagina.hayPaquetes ? "paquetes" : "datos"
                pagina.pestanaInicialPendiente = false
            }
        }
    }
    property bool pestanaInicialPendiente: true
    onConDatosChanged: {
        if (pagina.conDatos && pagina.pestanaInicialPendiente) {
            pagina.pestana = pagina.hayPaquetes ? "paquetes" : "datos"
            pagina.pestanaInicialPendiente = false
        }
    }

    // Diferido: no quita el foco a un dialogo ya abierto.
    function enfocarInicial() {
        if (pagina.conDatos && pagina.pestanaInicialPendiente) {
            pagina.pestana = pagina.hayPaquetes ? "paquetes" : "datos"
            pagina.pestanaInicialPendiente = false
        }
        if (dialogoEliminar.visible)
            return
        encabezado.botonRegresar.forceActiveFocus(Qt.TabFocusReason)
    }

    // T014.1 D5: ⌘R ejecuta la accion principal aplicable (Reintentar descarga
    // o Verificar ahora); ⌘⌫ solo abre la confirmacion de eliminar.
    readonly property bool puedeAccionPrincipal: pagina.conDatos
        && (pagina.detalle.puedeReintentarDescarga || pagina.detalle.puedeVerificar)
    readonly property bool puedePedirEliminar: pagina.conDatos && !pagina.detalle.eliminando
    readonly property string textoAccionPrincipal: pagina.conDatos && pagina.detalle.puedeReintentarDescarga
                                                   ? qsTr("Reintentar descarga") : qsTr("Verificar ahora")
    // Con un dialogo abierto no actua ningun atajo (Main.qml).
    readonly property bool dialogoAbierto: dialogoEliminar.visible

    function accionPrincipal() {
        if (!pagina.conDatos)
            return false
        if (pagina.detalle.puedeReintentarDescarga) {
            pagina.detalle.reintentarDescarga()
            return true
        }
        if (pagina.detalle.puedeVerificar) {
            pagina.detalle.verificarAhora()
            return true
        }
        return false
    }

    function pedirEliminar() {
        if (!pagina.puedePedirEliminar || dialogoEliminar.opened)
            return false
        dialogoEliminar.open()
        return true
    }

    function regresar() {
        pagina.app.mostrarLista()
    }

    function enfocarEliminar() {
        if (botonEliminar.visible)
            botonEliminar.forceActiveFocus(Qt.TabFocusReason)
    }

    // Cambia la pestana visible ("paquetes", "datos" o "historial").
    function mostrarPestana(clave) {
        pagina.pestana = clave
    }

    function textoOpcional(valor) {
        return valor === null || valor === undefined || valor === "" ? "" : String(valor)
    }

    function fechaHora(valor) {
        return valor === null || valor === undefined ? "" : FormatoFechas.fechaHora(valor)
    }

    // Mensaje de una fila de paquete y su tono (FilaPaquete).
    function mensajePaquete(p) {
        if (p.estadoDescarga === "Descargado")
            return Etiquetas.existenciaPaquete(p.existencia)
        if (p.estadoDescarga === "Disponible")
            return qsTr("La app lo descargará automáticamente.")
        return p.mensaje
    }
    function tonoPaquete(p) {
        switch (p.estadoDescarga) {
        case "Descargado":
            return p.existencia === "Presente" ? "exito" : (p.existencia === "Comprobando" ? "neutro" : "advertencia")
        case "Error": return "error"
        case "Vencido": return "advertencia"
        default: return "neutro"
        }
    }
    function metadatosPaquete(p) {
        const partes = [qsTr("Disponible %1").arg(pagina.fechaHora(p.disponibleEn))]
        if (p.descargadoEn !== null)
            partes.push(qsTr("Descargado %1").arg(pagina.fechaHora(p.descargadoEn)))
        if (p.vencidoEn !== null)
            partes.push(qsTr("Venció %1").arg(pagina.fechaHora(p.vencidoEn)))
        if (p.codigoDescargaSat !== "")
            partes.push(qsTr("Código SAT %1").arg(p.codigoDescargaSat))
        return partes.join(" · ")
    }

    readonly property string textoPeriodo: FormatoFechas.rango(pagina.detalle.fechaInicial, pagina.detalle.fechaFinal)

    Keys.onEscapePressed: regresar()

    header: EncabezadoPagina {
        id: encabezado
        titulo: qsTr("Solicitud")
        subtitulo: pagina.conDatos
                   ? [pagina.detalle.perfilRfc, Etiquetas.tipoDescarga(pagina.detalle.tipoDescarga), pagina.textoPeriodo]
                     .filter(t => t.length > 0).join(" · ")
                   : ""
        mostrarRegresar: true
        textoRegresar: qsTr("Solicitudes")
        onRegresarSolicitado: pagina.regresar()

        // T009.1 D1: abre la carpeta de la solicitud en Finder. Con todo
        // descargado pasa a ser la accion principal del resumen.
        BotonAccion {
            objectName: "botonAbrirCarpetaSolicitud"
            visible: pagina.conDatos && pagina.detalle.puedeAbrirCarpeta && !pagina.detalle.todoDescargado
            icono: "folder"
            text: qsTr("Abrir carpeta de la solicitud")
            descripcion: qsTr("Mostrar en Finder la carpeta con los paquetes descargados de esta solicitud")
            onClicked: pagina.detalle.abrirCarpetaSolicitud()
        }
        Rectangle {
            visible: pagina.conDatos
            implicitWidth: 1
            implicitHeight: 24
            color: Theme.separador
        }
        BotonAccion {
            id: botonEliminar
            objectName: "botonEliminarSolicitud"
            visible: pagina.conDatos
            enabled: !pagina.detalle.eliminando
            variante: "destructivo"
            icono: "trash"
            text: qsTr("Eliminar…")
            descripcion: qsTr("Eliminar la solicitud de este equipo; no modifica nada en el SAT")
            atajo: "⌘⌫"
            Accessible.name: qsTr("Eliminar solicitud")
            onClicked: dialogoEliminar.open()
        }
    }

    DialogoConfirmacion {
        id: dialogoEliminar
        objectName: "dialogoEliminar"
        prefijoNombre: "dialogoEliminar"
        variante: "destructivo"
        title: qsTr("¿Eliminar esta solicitud?")
        mensaje: qsTr("La solicitud se eliminará de esta aplicación. No se modifica nada en el SAT.")
        consecuencia: qsTr("Los ZIP ya descargados se quedan en su carpeta.")
        textoConfirmar: qsTr("Eliminar")
        textoCancelar: qsTr("Cancelar")
        onConfirmado: pagina.detalle.eliminar()
        onCancelado: Qt.callLater(pagina.enfocarEliminar)
    }

    // Estado: cargando
    ColumnLayout {
        objectName: "estadoCargandoDetalle"
        anchors.centerIn: parent
        visible: pagina.detalle.estado === SolicitudDetailViewModel.Cargando && !pagina.conDatos
        spacing: Theme.espacioS
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("Cargando solicitud")

        BusyIndicator {
            running: parent.visible
            Layout.alignment: Qt.AlignHCenter
            Accessible.ignored: true
        }
    }

    // Estado: no encontrada
    EstadoVacio {
        objectName: "estadoNoEncontrada"
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
        visible: pagina.detalle.estado === SolicitudDetailViewModel.NoEncontrada
        variante: "vacio"
        titulo: qsTr("Solicitud no encontrada")
        descripcion: qsTr("La solicitud no existe o fue eliminada.")
        textoAccion: qsTr("Solicitudes")
        objectNameAccion: "botonVolverNoEncontrada"
        onAccionSolicitada: pagina.regresar()
    }

    // Estado: error
    EstadoVacio {
        objectName: "estadoErrorDetalle"
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
        visible: pagina.detalle.estado === SolicitudDetailViewModel.Error
        variante: "error"
        titulo: pagina.detalle.errorMessage
        textoAccion: qsTr("Reintentar")
        objectNameTitulo: "errorDetalle"
        objectNameAccion: "botonReintentarDetalle"
        onAccionSolicitada: pagina.detalle.recargar()
    }

    // Estado: con datos
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.espacioL
        spacing: Theme.espacioM
        visible: pagina.conDatos

        // T009.1 D7: aviso accesible de Finder (no encontrado o fallido).
        AvisoEnLinea {
            objectName: "mensajeFinder"
            visible: pagina.detalle.mensajeFinder.length > 0
            variante: "error"
            icono: "folder"
            titulo: pagina.detalle.mensajeFinder
            Layout.fillWidth: true
        }
        AvisoEnLinea {
            objectName: "errorEliminacion"
            visible: pagina.detalle.errorEliminacion.length > 0
            variante: "error"
            titulo: pagina.detalle.errorEliminacion
            Layout.fillWidth: true
        }

        ResumenEstado {
            objectName: "resumenEstado"
            Layout.fillWidth: true
            clave: pagina.detalle.estadoResumen
            titular: pagina.detalle.titularResumen
            descripcion: pagina.detalle.descripcionResumen
            ocupado: pagina.detalle.estadoResumen === "Enviando"
            // T009 D10: por que no se puede enviar.
            accionHabilitada: !pagina.detalle.envioVisible || pagina.detalle.puedeEnviar
            motivo: pagina.detalle.envioVisible && !pagina.detalle.puedeEnviar ? pagina.detalle.motivoEnvio : ""
            objectNameMotivo: "motivoEnvio"
            mensajeResultado: pagina.detalle.accionSolicitada
            objectNameResultado: "accionSolicitada"
            datos: [
                { etiqueta: qsTr("Contribuyente"), valor: pagina.detalle.perfilRfc },
                { etiqueta: qsTr("Tipo · periodo"),
                  valor: Etiquetas.tipoDescarga(pagina.detalle.tipoDescarga) + " · " + pagina.textoPeriodo },
                { etiqueta: qsTr("CFDI reportados"), valor: pagina.textoOpcional(pagina.detalle.numeroCfdi) },
                { etiqueta: qsTr("Última verificación"), valor: pagina.fechaHora(pagina.detalle.ultimaVerificacionEn) }
            ]

            // Una accion principal por estado (normalmente solo una es visible).
            // T009: Enviar una solicitud Creada solo con la credencial Lista.
            BotonAccion {
                objectName: "botonEnviarSolicitud"
                visible: pagina.detalle.envioVisible
                enabled: pagina.detalle.puedeEnviar
                variante: "primario"
                icono: "paperplane"
                text: qsTr("Enviar")
                descripcion: pagina.detalle.puedeEnviar ? qsTr("Enviar la solicitud al SAT")
                                                        : pagina.detalle.motivoEnvio
                onClicked: pagina.detalle.enviar()
            }
            // T007: acciones manuales; solo visibles si aplican al estado actual.
            BotonAccion {
                objectName: "botonReintentarDescarga"
                visible: pagina.detalle.puedeReintentarDescarga
                variante: "primario"
                text: qsTr("Reintentar descarga")
                atajo: "⌘R"
                descripcion: qsTr("Descargar de nuevo los paquetes disponibles o con error; si el monitoreo está pausado, queda pendiente")
                onClicked: pagina.detalle.reintentarDescarga()
            }
            BotonAccion {
                objectName: "botonVerificarAhora"
                visible: pagina.detalle.puedeVerificar
                text: qsTr("Verificar ahora")
                atajo: pagina.detalle.puedeReintentarDescarga ? "" : "⌘R"
                descripcion: qsTr("Consultar ahora el estado de la solicitud en el SAT; si el monitoreo está pausado, queda pendiente")
                onClicked: pagina.detalle.verificarAhora()
            }
            BotonAccion {
                objectName: "accionAbrirCarpeta"
                visible: pagina.detalle.todoDescargado && pagina.detalle.puedeAbrirCarpeta
                variante: "primario"
                icono: "folder"
                text: qsTr("Abrir carpeta de la solicitud")
                descripcion: qsTr("Mostrar en Finder la carpeta con los paquetes descargados de esta solicitud")
                onClicked: pagina.detalle.abrirCarpetaSolicitud()
            }
        }

        // T009 D10: mensaje del catalogo para el estado (cuando no es la descripcion).
        AvisoEnLinea {
            objectName: "mensajeEstado"
            visible: pagina.detalle.mensajeEstado.length > 0
                     && pagina.detalle.descripcionResumen !== pagina.detalle.mensajeEstado
            variante: pagina.detalle.estadoResumen === "EnvioIncierto" || pagina.detalle.estadoResumen === "Enviada"
                      ? "advertencia" : "error"
            titulo: pagina.detalle.mensajeEstado
            anunciar: false
            Layout.fillWidth: true
        }

        SelectorSegmentado {
            objectName: "pestanasDetalle"
            variante: "pestanas"
            nombreAccesible: qsTr("Secciones del detalle")
            valor: pagina.pestana
            opciones: [
                { clave: "paquetes", texto: qsTr("Paquetes"), contador: pagina.detalle.totalPaquetes },
                { clave: "datos", texto: qsTr("Datos") },
                { clave: "historial", texto: qsTr("Historial"), contador: pagina.detalle.logs.length }
            ]
            onActivado: (clave) => { pagina.pestana = clave }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radioTarjeta
            color: Theme.superficie
            border.width: 1
            border.color: Theme.separador
            clip: true

            StackLayout {
                anchors.fill: parent
                anchors.margins: 1
                currentIndex: pagina.pestana === "paquetes" ? 0 : (pagina.pestana === "datos" ? 1 : 2)

                // ---- Paquetes ----
                ScrollView {
                    id: desplazamientoPaquetes
                    objectName: "seccionPaquetes"
                    contentWidth: availableWidth
                    clip: true
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Paquetes (%1)").arg(pagina.detalle.totalPaquetes)

                    ColumnLayout {
                        width: desplazamientoPaquetes.availableWidth
                        spacing: 0

                        EstadoVacio {
                            objectName: "sinPaquetes"
                            visible: !pagina.hayPaquetes
                            variante: "vacio"
                            titulo: pagina.detalle.estadoResumen === "Terminada"
                                    ? qsTr("Esta solicitud no tiene paquetes.")
                                    : qsTr("El SAT aún no entrega paquetes.")
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.espacioXl
                        }

                        Repeater {
                            model: pagina.detalle.paquetes
                            delegate: FilaPaquete {
                                id: paquete
                                required property var modelData
                                Layout.fillWidth: true
                                idPaquete: modelData.idPaqueteSat
                                estado: modelData.estadoDescarga
                                metadatos: pagina.metadatosPaquete(modelData)
                                mensaje: pagina.mensajePaquete(modelData)
                                tonoMensaje: pagina.tonoPaquete(modelData)
                                objectNameCopiar: "botonCopiarPaquete_" + modelData.idPaqueteSat
                                // T008 D11 / T009 D10: existencia del ZIP o mensaje del paquete.
                                objectNameMensaje: modelData.estadoDescarga === "Descargado"
                                                   ? "existenciaPaquete_" + modelData.idPaqueteSat
                                                   : "mensajePaquete_" + modelData.idPaqueteSat
                                // T009.1 D1: solo operable con el archivo local Presente.
                                finderHabilitado: modelData.puedeMostrarFinder === true
                                objectNameFinder: "botonMostrarFinder_" + modelData.idPaqueteSat
                                onMostrarEnFinder: pagina.detalle.mostrarEnFinder(paquete.modelData.idPaqueteSat)
                            }
                        }
                    }
                }

                // ---- Datos (D7: estado local y SAT solo aqui) ----
                ScrollView {
                    id: desplazamientoDatos
                    objectName: "pestanaDatos"
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: desplazamientoDatos.availableWidth
                        spacing: 0

                        component EncabezadoSeccion: Rectangle {
                            id: seccion
                            property string texto: ""
                            Layout.fillWidth: true
                            implicitHeight: 28
                            color: Theme.superficieSeccion
                            Label {
                                x: Theme.espacioL
                                anchors.verticalCenter: parent.verticalCenter
                                text: seccion.texto
                                color: Theme.textoSecundario
                                font.family: Theme.familia
                                font.pixelSize: Theme.etiqueta.size
                                font.weight: Font.DemiBold
                                Accessible.role: Accessible.Heading
                                Accessible.name: text
                            }
                        }

                        // Estado
                        ColumnLayout {
                            objectName: "seccionEstados"
                            spacing: 0
                            Layout.fillWidth: true
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("Estado")

                            EncabezadoSeccion { texto: qsTr("Estado") }
                            Repeater {
                                model: [
                                    { nombre: "badgeEstadoResumen", etiqueta: qsTr("Resumen"), clave: pagina.detalle.estadoResumen,
                                      texto: Etiquetas.estadoResumen(pagina.detalle.estadoResumen), contexto: qsTr("Estado resumido") },
                                    { nombre: "badgeEstadoLocal", etiqueta: qsTr("Estado local"), clave: pagina.detalle.estadoLocal,
                                      texto: Etiquetas.estadoLocal(pagina.detalle.estadoLocal), contexto: qsTr("Estado local") },
                                    // "Error" del SAT es el alias ErrorSat (no el error de paquete).
                                    { nombre: "badgeEstadoSat", etiqueta: qsTr("Estado SAT"),
                                      clave: pagina.sinEstadoSat ? "" : (pagina.detalle.estadoSat === "Error" ? "ErrorSat" : pagina.detalle.estadoSat),
                                      texto: Etiquetas.estadoSat(pagina.detalle.estadoSat), contexto: qsTr("Estado SAT") }
                                ]
                                delegate: Item {
                                    id: filaEstado
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    implicitHeight: Math.max(Theme.altoBadge, etiquetaEstado.implicitHeight) + 14
                                    Rectangle {
                                        visible: filaEstado.index > 0
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        height: 1
                                        color: Theme.separador
                                    }
                                    Label {
                                        id: etiquetaEstado
                                        x: Theme.espacioL
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: filaEstado.modelData.etiqueta
                                        color: Theme.textoSecundario
                                        font.family: Theme.familia
                                        font.pixelSize: Theme.cuerpo.size
                                        Accessible.ignored: true
                                    }
                                    EstadoBadge {
                                        objectName: filaEstado.modelData.nombre
                                        x: 200
                                        anchors.verticalCenter: parent.verticalCenter
                                        clave: filaEstado.modelData.clave
                                        texto: filaEstado.modelData.texto
                                        contexto: filaEstado.modelData.contexto
                                    }
                                }
                            }
                            CampoDetalle { etiqueta: qsTr("Último error"); valor: pagina.detalle.ultimoError }
                        }

                        // SAT
                        EncabezadoSeccion { texto: qsTr("SAT") }
                        CampoDetalle {
                            objectName: "campoIdSolicitudSat"
                            etiqueta: qsTr("Id solicitud SAT")
                            valor: pagina.detalle.idSolicitudSat
                            mono: true
                            primero: true
                            copiable: true
                            descripcionCopia: qsTr("Id de solicitud SAT")
                        }
                        CampoDetalle {
                            objectName: "campoCodigoSolicitud"
                            etiqueta: qsTr("Código de solicitud SAT")
                            valor: pagina.detalle.codEstatusSolicitud
                                   + (pagina.detalle.mensajeSolicitudSat ? " · " + pagina.detalle.mensajeSolicitudSat : "")
                        }
                        CampoDetalle {
                            etiqueta: qsTr("Código de verificación SAT")
                            valor: pagina.detalle.codigoEstadoSolicitud
                                   + (pagina.detalle.mensajeVerificacionSat ? " · " + pagina.detalle.mensajeVerificacionSat : "")
                        }
                        CampoDetalle { etiqueta: qsTr("CFDI reportados"); valor: pagina.textoOpcional(pagina.detalle.numeroCfdi) }
                        CampoDetalle { etiqueta: qsTr("Enviada"); valor: pagina.fechaHora(pagina.detalle.enviadaEn) }
                        CampoDetalle { etiqueta: qsTr("Última verificación"); valor: pagina.fechaHora(pagina.detalle.ultimaVerificacionEn) }

                        // Solicitud
                        ColumnLayout {
                            objectName: "seccionMetadata"
                            spacing: 0
                            Layout.fillWidth: true
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("Solicitud")

                            EncabezadoSeccion { texto: qsTr("Solicitud") }
                            CampoDetalle { etiqueta: qsTr("Identificador local"); valor: pagina.detalle.solicitudId; mono: true; primero: true }
                            CampoDetalle { objectName: "campoPerfilRfc"; etiqueta: qsTr("Perfil"); valor: pagina.detalle.perfilRfc }
                            CampoDetalle { etiqueta: qsTr("Tipo de descarga"); valor: Etiquetas.tipoDescarga(pagina.detalle.tipoDescarga) }
                            CampoDetalle { etiqueta: qsTr("Creada"); valor: pagina.fechaHora(pagina.detalle.creadaEn) }
                        }

                        // Filtros
                        ColumnLayout {
                            objectName: "seccionFiltros"
                            spacing: 0
                            Layout.fillWidth: true
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("Filtros")

                            EncabezadoSeccion { texto: qsTr("Filtros") }
                            CampoDetalle { etiqueta: qsTr("Periodo"); valor: pagina.textoPeriodo; primero: true }
                            CampoDetalle {
                                objectName: "campoContrapartes"
                                etiqueta: qsTr("RFC contraparte")
                                valor: pagina.detalle.rfcContrapartes.length > 0
                                       ? pagina.detalle.rfcContrapartes.join(", ")
                                       : pagina.detalle.rfcContraparte
                            }
                            CampoDetalle {
                                objectName: "campoTipoComprobante"
                                etiqueta: qsTr("Tipo de comprobante")
                                valor: Etiquetas.tipoComprobante(pagina.detalle.tipoComprobante)
                            }
                            CampoDetalle { etiqueta: qsTr("Complemento"); valor: pagina.detalle.complemento }
                        }
                    }
                }

                // ---- Historial ----
                ScrollView {
                    id: desplazamientoHistorial
                    objectName: "seccionHistorial"
                    contentWidth: availableWidth
                    clip: true
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Historial (%1)").arg(pagina.detalle.logs.length)

                    ColumnLayout {
                        width: desplazamientoHistorial.availableWidth
                        spacing: 0

                        Label {
                            visible: pagina.detalle.logs.length === 0
                            text: qsTr("Sin eventos registrados.")
                            color: Theme.textoSecundario
                            padding: Theme.espacioL
                            Layout.fillWidth: true
                        }
                        Repeater {
                            model: pagina.detalle.logs
                            delegate: EventoHistorial {
                                id: evento
                                required property var modelData
                                required property int index
                                objectName: "eventoLog_" + index
                                Layout.fillWidth: true
                                fechaHora: modelData.creadoEn
                                origen: modelData.origen
                                descripcion: {
                                    const l = evento.modelData
                                    let partes = [Etiquetas.eventoLog(l.tipoEvento)]
                                    if (l.codigoSat !== "")
                                        partes.push(qsTr("código SAT %1").arg(l.codigoSat))
                                    if (l.mensajeSat !== "")
                                        partes.push(l.mensajeSat)
                                    return partes.join(" · ")
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
