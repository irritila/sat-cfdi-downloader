pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import "Etiquetas.js" as Etiquetas

// Perfiles SAT (T013, traspaso "PerfilesSatPage"; UX-31..UX-34). Solo layout y
// estado visual: reglas, elegibilidad y errores vienen de los view models.
//
// - SplitView: a la izquierda la lista de FilaPerfil (340; 280..420); a la
//   derecha el detalle con scroll y una columna de 640 como maximo.
// - Detalle: grupo "Perfil" (RFC de solo lectura con candado en edicion,
//   nombre descriptivo y, en su pie, Descartar y Guardar) y grupo "e.firma"
//   (badge grande, linea de estado, ayuda y Registrar/Reemplazar e.firma).
// - Reemplazar pide confirmacion ANTES de abrir la captura.
// - Estados: cargando y error en la lista; sin perfiles en el panel derecho.
//   Al cargar con datos se selecciona el primer perfil.
// - La captura de e.firma se descarta al cambiar de perfil, al ocultar la
//   ventana y al salir de la pagina.
//
// Teclado: foco inicial en la lista (o en "Nuevo perfil" si esta vacia);
// flechas para moverse; Enter/Espacio edita el perfil con foco; R o Tab hasta
// "Reintentar" de la fila reintenta el estado; Tab recorre lista, formulario y
// acciones; Escape vuelve a solicitudes.
Page {
    id: pagina
    objectName: "paginaPerfilesSat"

    required property AppViewModel app
    required property PerfilesSatViewModel perfiles
    required property EFirmaFormViewModel eFirma

    readonly property bool hayFormulario: pagina.perfiles.modo !== PerfilesSatViewModel.Ninguno
    readonly property bool enEdicion: pagina.perfiles.modo === PerfilesSatViewModel.Edicion
    readonly property string preparacion: pagina.perfiles.seleccionPreparacion
    // e.firma que conviene renovar o volver a registrar: la accion es primaria.
    readonly property bool eFirmaRequiereAccion: !pagina.perfiles.tieneCredencial
        || pagina.preparacion === "Vencida" || pagina.preparacion === "NoVigenteAun"
        || pagina.preparacion === "MaterialFaltante" || pagina.preparacion === "MaterialDanado"

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Perfiles SAT")

    background: Rectangle {
        color: Theme.fondo
    }

    // T014.1 D5: con un dialogo abierto no actua ningun atajo (Main.qml).
    readonly property bool dialogoAbierto: dialogoEFirma.visible || dialogoReemplazo.visible

    Component.onCompleted: Qt.callLater(pagina.enfocarInicial)

    function enfocarInicial() {
        if (dialogoEFirma.visible || dialogoReemplazo.visible)
            return
        if (lista.visible && lista.count > 0)
            lista.forceActiveFocus(Qt.TabFocusReason)
        else
            botonNuevo.forceActiveFocus(Qt.TabFocusReason)
    }

    function regresar() {
        dialogoEFirma.limpiarCaptura()
        pagina.app.mostrarLista()
    }

    function editar(id) {
        if (id && pagina.perfiles.seleccionar(id))
            Qt.callLater(pagina.enfocarFormulario)
    }

    function nuevoPerfil() {
        pagina.perfiles.nuevo()
        Qt.callLater(pagina.enfocarFormulario)
    }

    function enfocarFormulario() {
        if (pagina.perfiles.rfcEditable)
            campoRfc.forceActiveFocus(Qt.TabFocusReason)
        else
            campoNombre.forceActiveFocus(Qt.TabFocusReason)
    }

    function enfocarCampo(campo) {
        if (campo === "rfc" && pagina.perfiles.rfcEditable)
            campoRfc.forceActiveFocus(Qt.OtherFocusReason)
        else if (campo === "nombre")
            campoNombre.forceActiveFocus(Qt.OtherFocusReason)
    }

    function gestionarEFirma() {
        if (!pagina.perfiles.puedeGestionarEFirma)
            return
        if (pagina.perfiles.tieneCredencial) {
            dialogoReemplazo.open() // confirmacion previa al reemplazo
        } else {
            pagina.eFirma.iniciar(pagina.perfiles.perfilId, pagina.perfiles.rfc, false)
            dialogoEFirma.open()
        }
    }

    function enfocarBotonEFirma() {
        botonEFirma.forceActiveFocus(Qt.TabFocusReason)
    }

    function abrirReemplazo() {
        pagina.eFirma.iniciar(pagina.perfiles.perfilId, pagina.perfiles.rfc, true)
        dialogoEFirma.open()
    }

    // Con la lista cargada y sin formulario abierto, el primer perfil queda
    // seleccionado (sin mover el foco).
    function seleccionarPrimero() {
        if (pagina.perfiles.estadoLista === PerfilesSatViewModel.ConDatos
                && pagina.perfiles.modo === PerfilesSatViewModel.Ninguno
                && pagina.perfiles.perfiles.count > 0)
            pagina.perfiles.seleccionar(pagina.perfiles.perfiles.idEn(0))
    }

    Keys.onEscapePressed: regresar()

    Connections {
        target: pagina.perfiles
        function onEnfocarCampo(campo) { pagina.enfocarCampo(campo) }
        function onListaChanged() { Qt.callLater(pagina.seleccionarPrimero) }
        // Cambiar de perfil descarta la captura de e.firma en curso.
        function onFormularioChanged() {
            if (dialogoEFirma.visible && pagina.perfiles.perfilId !== pagina.eFirma.perfilId)
                dialogoEFirma.abandonar()
        }
    }

    // Ocultar la ventana (cerrar a menu bar) vacia la contrasena.
    Connections {
        target: pagina.Window.window
        function onVisibleChanged() {
            if (pagina.Window.window && !pagina.Window.window.visible && dialogoEFirma.visible)
                dialogoEFirma.abandonar()
        }
    }

    EFirmaDialogo {
        id: dialogoEFirma
        eFirma: pagina.eFirma
        nombrePerfil: pagina.eFirma.perfilId === pagina.perfiles.perfilId ? pagina.perfiles.nombre : ""
        vigenteHasta: pagina.eFirma.perfilId === pagina.perfiles.perfilId ? pagina.perfiles.seleccionVigenteHasta : ""
    }

    DialogoConfirmacion {
        id: dialogoReemplazo
        objectName: "dialogoReemplazoEFirma"
        prefijoNombre: "dialogoReemplazo"
        title: qsTr("¿Reemplazar la e.firma de %1?").arg(pagina.perfiles.rfc)
        mensaje: qsTr("La e.firma registrada se reemplazará solo si la nueva se valida. Si falla, la actual sigue registrada sin cambios.")
        textoConfirmar: qsTr("Continuar")
        onConfirmado: Qt.callLater(pagina.abrirReemplazo)
        onCancelado: Qt.callLater(pagina.enfocarBotonEFirma)
    }

    header: EncabezadoPagina {
        id: encabezado
        titulo: qsTr("Perfiles SAT")
        mostrarRegresar: true
        textoRegresar: qsTr("Solicitudes")
        descripcionRegresar: qsTr("Volver a la lista de solicitudes (Escape)")
        onRegresarSolicitado: pagina.regresar()

        BotonAccion {
            id: botonNuevo
            objectName: "botonNuevoPerfil"
            variante: "primario"
            icono: "plus"
            text: qsTr("Nuevo perfil")
            descripcion: qsTr("Crear un perfil SAT")
            KeyNavigation.tab: lista.visible ? lista : null
            onClicked: pagina.nuevoPerfil()
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        handle: Rectangle {
            implicitWidth: 1
            color: Theme.separador
            // Zona de arrastre mas ancha que la linea.
            containmentMask: Item {
                x: -3
                width: 7
                height: parent ? parent.height : 0
            }
        }

        // ---- Lista ------------------------------------------------------
        Rectangle {
            SplitView.preferredWidth: Theme.anchoListaPerfiles
            SplitView.minimumWidth: 280
            SplitView.maximumWidth: 420
            color: Theme.superficie

            ListView {
                id: lista
                objectName: "listaPerfiles"
                anchors.fill: parent
                clip: true
                visible: pagina.perfiles.estadoLista === PerfilesSatViewModel.ConDatos
                model: pagina.perfiles.perfiles
                currentIndex: count > 0 ? Math.max(0, pagina.perfiles.perfiles.filaDe(pagina.perfiles.perfilId)) : -1
                activeFocusOnTab: true
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Lista de perfiles SAT, %n elemento(s)", "", count)

                ScrollBar.vertical: ScrollBar { }

                Keys.onReturnPressed: pagina.editar(pagina.perfiles.perfiles.idEn(lista.currentIndex))
                Keys.onEnterPressed: pagina.editar(pagina.perfiles.perfiles.idEn(lista.currentIndex))
                Keys.onSpacePressed: pagina.editar(pagina.perfiles.perfiles.idEn(lista.currentIndex))
                // R reintenta el estado del perfil con foco.
                Keys.onPressed: (evento) => {
                    if (evento.key === Qt.Key_R) {
                        pagina.perfiles.reintentarEstado(pagina.perfiles.perfiles.idEn(lista.currentIndex))
                        evento.accepted = true
                    }
                }

                delegate: FilaPerfil {
                    id: fila
                    required property int index
                    required property var model

                    objectName: "filaPerfil_" + index
                    objectNameBadge: "badgePreparacion_" + index
                    objectNameDisponibilidad: "disponibilidad_" + index
                    objectNameReintentar: "botonReintentarEstado_" + index
                    width: ListView.view.width
                    rfc: model.rfc
                    nombre: model.nombre
                    preparacion: model.preparacion
                    estadoTexto: model.estadoTexto
                    activo: model.activo
                    listo: model.listoParaSolicitudes
                    vigenteHasta: model.vigenteHasta
                    diasParaVencer: model.diasParaVencer
                    disponibilidad: !model.activo
                        ? qsTr("Inactivo: no disponible para solicitudes")
                        : (model.listoParaSolicitudes ? qsTr("Disponible para solicitudes")
                                                      : qsTr("No disponible para solicitudes"))
                    seleccionada: model.id === pagina.perfiles.perfilId
                    conFoco: ListView.isCurrentItem && lista.activeFocus

                    onClicked: {
                        lista.currentIndex = index
                        pagina.editar(model.id)
                    }
                    onReintentarSolicitado: pagina.perfiles.reintentarEstado(model.id)
                }
            }

            ColumnLayout {
                objectName: "estadoCargandoPerfiles"
                anchors.centerIn: parent
                visible: pagina.perfiles.estadoLista === PerfilesSatViewModel.Cargando
                spacing: Theme.espacioS
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Cargando perfiles SAT")
                BusyIndicator {
                    running: parent.visible
                    Layout.alignment: Qt.AlignHCenter
                    Accessible.ignored: true
                }
            }

            EstadoVacio {
                objectName: "estadoErrorPerfiles"
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.espacioL
                visible: pagina.perfiles.estadoLista === PerfilesSatViewModel.Error
                variante: "error"
                titulo: pagina.perfiles.errorListaMessage
                textoAccion: qsTr("Reintentar")
                objectNameTitulo: "errorListaPerfiles"
                objectNameAccion: "botonReintentarPerfiles"
                onAccionSolicitada: pagina.perfiles.cargar()
            }
        }

        // ---- Detalle ----------------------------------------------------
        Item {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 360

            EstadoVacio {
                objectName: "estadoVacioPerfiles"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.espacioXxl, 420)
                visible: pagina.perfiles.estadoLista === PerfilesSatViewModel.Vacia && !pagina.hayFormulario
                variante: "sinPerfiles"
                titulo: qsTr("Aún no hay perfiles SAT")
                descripcion: qsTr("Crea un perfil con el RFC del contribuyente y después registra su e.firma.")
                textoAccion: qsTr("Nuevo perfil")
                accionPrimaria: true
                objectNameAccion: "botonNuevoPerfilVacio"
                onAccionSolicitada: pagina.nuevoPerfil()
            }

            ScrollView {
                id: panel
                objectName: "panelPerfil"
                anchors.fill: parent
                visible: pagina.hayFormulario
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    id: columna
                    x: Math.max(Theme.espacioL, (panel.availableWidth - width) / 2)
                    width: Math.min(Theme.anchoFormulario, panel.availableWidth - 2 * Theme.espacioL)
                    spacing: Theme.espacioS

                    Item { implicitHeight: Theme.espacioXl - Theme.espacioS }

                    // Errores sin campo (persistencia, perfil no encontrado...).
                    AvisoEnLinea {
                        objectName: "errorPerfil"
                        visible: pagina.perfiles.errorMessage.length > 0 && pagina.perfiles.campoConError === ""
                        variante: "error"
                        titulo: pagina.perfiles.errorMessage
                        Layout.fillWidth: true
                        Layout.bottomMargin: Theme.espacioS
                    }

                    // ---- Grupo Perfil ----
                    Label {
                        text: pagina.perfiles.modo === PerfilesSatViewModel.Nuevo ? qsTr("Nuevo perfil") : qsTr("Perfil")
                        color: Theme.textoSecundario
                        font.family: Theme.familia
                        font.pixelSize: Theme.etiqueta.size
                        font.weight: Font.DemiBold
                        leftPadding: Theme.espacioM
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: grupoPerfil.implicitHeight
                        radius: Theme.radioTarjeta
                        color: Theme.superficie
                        border.width: 1
                        border.color: Theme.separador

                        ColumnLayout {
                            id: grupoPerfil
                            width: parent.width
                            spacing: 0

                            CampoFormulario {
                                objectName: "filaRfcPerfil"
                                etiqueta: qsTr("RFC")
                                ayuda: pagina.perfiles.rfcEditable ? ""
                                                                   : qsTr("El RFC identifica al perfil y no se puede cambiar.")
                                error: pagina.perfiles.errorRfc
                                objectNameError: "errorRfcPerfil"

                                CampoTexto {
                                    id: campoRfc
                                    objectName: "campoRfcPerfil"
                                    Layout.fillWidth: true
                                    text: pagina.perfiles.rfc
                                    soloLectura: !pagina.perfiles.rfcEditable
                                    conError: pagina.perfiles.errorRfc.length > 0
                                    enabled: !pagina.perfiles.guardando
                                    maximumLength: 13
                                    onTextEdited: pagina.perfiles.rfc = text
                                    onAccepted: pagina.perfiles.guardar()
                                    Accessible.name: pagina.perfiles.rfcEditable ? qsTr("RFC del perfil")
                                                                                 : qsTr("RFC del perfil, no editable")
                                    Accessible.description: pagina.perfiles.errorRfc
                                }
                            }
                            CampoFormulario {
                                objectName: "filaNombrePerfil"
                                etiqueta: qsTr("Nombre descriptivo")
                                error: pagina.perfiles.errorNombre
                                objectNameError: "errorNombrePerfil"

                                CampoTexto {
                                    id: campoNombre
                                    objectName: "campoNombrePerfil"
                                    Layout.fillWidth: true
                                    text: pagina.perfiles.nombre
                                    conError: pagina.perfiles.errorNombre.length > 0
                                    enabled: !pagina.perfiles.guardando
                                    onTextEdited: pagina.perfiles.nombre = text
                                    onAccepted: pagina.perfiles.guardar()
                                    Accessible.name: qsTr("Nombre descriptivo del perfil")
                                    Accessible.description: pagina.perfiles.errorNombre
                                }
                            }

                            // Pie del grupo: Descartar y Guardar.
                            RowLayout {
                                spacing: Theme.espacioS
                                Layout.fillWidth: true
                                Layout.margins: Theme.espacioM

                                Item { Layout.fillWidth: true }
                                BotonAccion {
                                    objectName: "botonDescartarPerfil"
                                    text: qsTr("Descartar")
                                    descripcion: qsTr("Descartar los cambios del perfil")
                                    enabled: pagina.perfiles.sucio && !pagina.perfiles.guardando
                                    onClicked: pagina.perfiles.descartar()
                                }
                                BotonAccion {
                                    objectName: "botonGuardarPerfil"
                                    variante: "primario"
                                    text: qsTr("Guardar")
                                    descripcion: qsTr("Guardar el perfil")
                                    cargando: pagina.perfiles.guardando
                                    enabled: pagina.perfiles.puedeGuardar
                                    onClicked: pagina.perfiles.guardar()
                                }
                            }
                        }
                    }

                    // ---- Grupo e.firma (solo perfil guardado) ----
                    Label {
                        visible: pagina.enEdicion
                        text: qsTr("e.firma")
                        color: Theme.textoSecundario
                        font.family: Theme.familia
                        font.pixelSize: Theme.etiqueta.size
                        font.weight: Font.DemiBold
                        leftPadding: Theme.espacioM
                        Layout.topMargin: Theme.espacioL
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    Rectangle {
                        objectName: "seccionEFirma"
                        visible: pagina.enEdicion
                        Layout.fillWidth: true
                        implicitHeight: grupoEFirma.implicitHeight + 2 * Theme.espacioM
                        radius: Theme.radioTarjeta
                        color: Theme.superficie
                        border.width: 1
                        border.color: Theme.separador
                        Accessible.role: Accessible.Grouping
                        Accessible.name: qsTr("e.firma")

                        ColumnLayout {
                            id: grupoEFirma
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Theme.espacioM
                            spacing: Theme.espacioM

                            RowLayout {
                                spacing: Theme.espacioM
                                Layout.fillWidth: true
                                EstadoBadge {
                                    objectName: "badgeEFirmaSeleccion"
                                    eFirma: true
                                    grande: true
                                    clave: pagina.preparacion
                                    texto: pagina.perfiles.seleccionEstadoTexto
                                    contexto: qsTr("e.firma")
                                }
                                // T014.3 D2: aviso de vencimiento (30 y 7 dias).
                                EstadoBadge {
                                    objectName: "badgeVencimientoSeleccion"
                                    visible: texto.length > 0
                                    grande: true
                                    tonoDirecto: "advertencia"
                                    iconoDirecto: "calendar-exclamation"
                                    texto: Etiquetas.venceEn(pagina.perfiles.seleccionDiasParaVencer)
                                    contexto: qsTr("e.firma")
                                }
                                Label {
                                    objectName: "listoSeleccion"
                                    text: {
                                        if (!pagina.perfiles.seleccionActiva)
                                            return qsTr("Inactivo: no disponible para solicitudes")
                                        if (pagina.preparacion === "SinCredencial")
                                            return qsTr("Este perfil no tiene e.firma registrada.")
                                        if (pagina.perfiles.seleccionListo)
                                            return pagina.perfiles.seleccionVigenteHasta.length > 0
                                                   ? qsTr("Lista para solicitudes · Vigente hasta %1")
                                                         .arg(pagina.perfiles.seleccionVigenteHasta)
                                                   : qsTr("Lista para solicitudes")
                                        return qsTr("No disponible para solicitudes")
                                    }
                                    color: Theme.texto
                                    font.family: Theme.familia
                                    font.pixelSize: Theme.cuerpo.size
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                            Label {
                                objectName: "ayudaEFirma"
                                text: pagina.perfiles.tieneCredencial
                                      ? qsTr("La e.firma se guarda cifrada y su contraseña queda en el llavero de macOS.")
                                      : qsTr("Regístrala para poder usar este perfil en solicitudes.")
                                color: Theme.textoSecundario
                                font.family: Theme.familia
                                font.pixelSize: Theme.cuerpo.size
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Label {
                                objectName: "avisoGuardarAntes"
                                visible: pagina.perfiles.sucio
                                text: qsTr("Guarda el perfil antes de gestionar su e.firma.")
                                color: Theme.tonoAdvertenciaTexto
                                font.family: Theme.familia
                                font.pixelSize: Theme.cuerpo.size
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                spacing: Theme.espacioS
                                BotonAccion {
                                    id: botonEFirma
                                    objectName: "botonEFirma"
                                    variante: pagina.eFirmaRequiereAccion ? "primario" : "secundario"
                                    icono: "key"
                                    text: pagina.perfiles.tieneCredencial ? qsTr("Reemplazar e.firma…")
                                                                          : qsTr("Registrar e.firma…")
                                    descripcion: pagina.perfiles.tieneCredencial
                                                 ? qsTr("Pide confirmación antes de capturar la nueva e.firma")
                                                 : qsTr("Elegir certificado, llave y contraseña")
                                    enabled: pagina.perfiles.puedeGestionarEFirma
                                    onClicked: pagina.gestionarEFirma()
                                }
                                BotonAccion {
                                    objectName: "botonReintentarEstadoSeleccion"
                                    visible: pagina.preparacion === "EstadoNoDisponible"
                                    text: qsTr("Reintentar estado")
                                    descripcion: qsTr("Volver a consultar el estado de la e.firma")
                                    onClicked: pagina.perfiles.reintentarEstado(pagina.perfiles.perfilId)
                                }
                            }
                        }
                    }

                    Item { implicitHeight: Theme.espacioXl }
                }
            }
        }
    }
}
