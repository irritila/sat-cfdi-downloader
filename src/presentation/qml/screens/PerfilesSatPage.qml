pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Perfiles SAT (T005.1): lista de perfiles no eliminados con su preparacion,
// formulario de alta/edicion y gestion de e.firma. Solo layout y estado
// visual: reglas, elegibilidad y errores vienen de los view models.
//
// - Lista: Cargando, Vacia, Error (con reintento) y ConDatos. Cada perfil
//   muestra RFC, nombre y su estado como TEXTO (no solo color); los inactivos
//   se marcan "No disponible para solicitudes"; "Verificando" y "Estado no
//   disponible" (con reintento por perfil).
// - Formulario: RFC editable solo al crear; en edicion es identidad de solo
//   lectura. Un error enfoca su campo (RfcDuplicado -> RFC).
// - e.firma: Registrar (perfil guardado y activo) o Reemplazar, que pide
//   confirmacion ANTES de abrir la captura.
// - La captura se descarta al cambiar de perfil, al ocultar la ventana y al
//   salir de la pagina.
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
    readonly property bool anchoAmplio: pagina.width >= 760

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Perfiles SAT")

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

    Keys.onEscapePressed: regresar()

    Connections {
        target: pagina.perfiles
        function onEnfocarCampo(campo) { pagina.enfocarCampo(campo) }
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
    }

    DialogoConfirmacion {
        id: dialogoReemplazo
        objectName: "dialogoReemplazoEFirma"
        prefijoNombre: "dialogoReemplazo"
        title: qsTr("Reemplazar e.firma")
        mensaje: qsTr("La e.firma registrada de %1 se reemplazara solo si la nueva se valida. Si falla, la actual sigue registrada sin cambios.")
                     .arg(pagina.perfiles.rfc)
        textoConfirmar: qsTr("Continuar")
        onConfirmado: Qt.callLater(pagina.abrirReemplazo)
        onCancelado: Qt.callLater(pagina.enfocarBotonEFirma)
    }

    header: EncabezadoPagina {
        id: encabezado
        titulo: qsTr("Perfiles SAT")
        mostrarRegresar: true
        textoRegresar: qsTr("Volver a solicitudes")
        descripcionRegresar: qsTr("Volver a la lista de solicitudes (Escape)")
        onRegresarSolicitado: pagina.regresar()

        BotonAccion {
            id: botonNuevo
            objectName: "botonNuevoPerfil"
            text: qsTr("Nuevo perfil")
            descripcion: qsTr("Crear un perfil SAT")
            highlighted: true
            KeyNavigation.tab: lista.visible ? lista : null
            onClicked: pagina.nuevoPerfil()
        }
    }

    GridLayout {
        anchors.fill: parent
        anchors.margins: 8
        columns: pagina.anchoAmplio ? 2 : 1
        columnSpacing: 12
        rowSpacing: 12

        // ---- Lista ----------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: pagina.anchoAmplio ? 1 : -1
            Layout.minimumHeight: 160

            ListView {
                id: lista
                objectName: "listaPerfiles"
                anchors.fill: parent
                clip: true
                spacing: 4
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

                delegate: ItemDelegate {
                    id: fila
                    objectName: "filaPerfil_" + index

                    required property int index
                    required property var model

                    readonly property string textoDisponibilidad: !fila.model.activo
                        ? qsTr("Inactivo: no disponible para solicitudes")
                        : (fila.model.listoParaSolicitudes ? qsTr("Disponible para solicitudes")
                                                           : qsTr("No disponible para solicitudes"))

                    width: ListView.view.width
                    focusPolicy: Qt.NoFocus
                    highlighted: ListView.isCurrentItem || fila.model.id === pagina.perfiles.perfilId

                    Accessible.role: Accessible.ListItem
                    Accessible.name: qsTr("Perfil %1, %2, %3, %4")
                        .arg(fila.model.rfc).arg(fila.model.nombre).arg(fila.model.estadoTexto)
                        .arg(fila.textoDisponibilidad)

                    onClicked: {
                        lista.currentIndex = index
                        pagina.editar(fila.model.id)
                    }

                    contentItem: ColumnLayout {
                        spacing: 4

                        RowLayout {
                            spacing: 8
                            Layout.fillWidth: true
                            Label {
                                text: fila.model.rfc
                                font.bold: true
                                Accessible.ignored: true
                            }
                            Label {
                                text: fila.model.nombre
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                Accessible.ignored: true
                            }
                            BusyIndicator {
                                visible: fila.model.verificando
                                running: visible
                                implicitWidth: 18
                                implicitHeight: 18
                                Accessible.ignored: true
                            }
                            EstadoBadge {
                                objectName: "badgePreparacion_" + fila.index
                                clave: fila.model.preparacion
                                texto: fila.model.estadoTexto
                                contexto: qsTr("e.firma")
                            }
                        }
                        RowLayout {
                            spacing: 8
                            Layout.fillWidth: true
                            Label {
                                objectName: "disponibilidad_" + fila.index
                                text: fila.textoDisponibilidad
                                font.italic: !fila.model.activo
                                opacity: 0.8
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                Accessible.ignored: true
                            }
                            Label {
                                visible: fila.model.vigenteHasta !== null
                                text: fila.model.vigenteHasta !== null
                                      ? qsTr("Vigente hasta %1").arg(Qt.formatDate(fila.model.vigenteHasta, "yyyy-MM-dd"))
                                      : ""
                                opacity: 0.8
                                Accessible.ignored: true
                            }
                            // Alcanzable con Tab desde la lista (ademas del atajo R).
                            BotonAccion {
                                objectName: "botonReintentarEstado_" + fila.index
                                visible: fila.model.preparacion === "EstadoNoDisponible"
                                text: qsTr("Reintentar")
                                descripcion: qsTr("Volver a consultar el estado de la e.firma de %1").arg(fila.model.rfc)
                                activeFocusOnTab: true
                                Accessible.name: qsTr("Reintentar estado de %1").arg(fila.model.rfc)
                                onClicked: pagina.perfiles.reintentarEstado(fila.model.id)
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                objectName: "estadoVacioPerfiles"
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 380)
                visible: pagina.perfiles.estadoLista === PerfilesSatViewModel.Vacia
                spacing: 8
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("No hay perfiles SAT. Usa Nuevo perfil para crear uno.")

                Label {
                    text: qsTr("No hay perfiles SAT")
                    font.pixelSize: 18
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }
                Label {
                    text: qsTr("Crea un perfil con el RFC del contribuyente y despues registra su e.firma.")
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }
            }

            ColumnLayout {
                objectName: "estadoCargandoPerfiles"
                anchors.centerIn: parent
                visible: pagina.perfiles.estadoLista === PerfilesSatViewModel.Cargando
                spacing: 8
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Cargando perfiles SAT")
                BusyIndicator {
                    running: parent.visible
                    Layout.alignment: Qt.AlignHCenter
                    Accessible.ignored: true
                }
                Label {
                    text: qsTr("Cargando perfiles SAT...")
                    Accessible.ignored: true
                }
            }

            ColumnLayout {
                objectName: "estadoErrorPerfiles"
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 380)
                visible: pagina.perfiles.estadoLista === PerfilesSatViewModel.Error
                spacing: 8
                Label {
                    objectName: "errorListaPerfiles"
                    text: pagina.perfiles.errorListaMessage
                    color: "#b00020"
                    font.bold: true
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: qsTr("Error: %1").arg(text)
                }
                BotonAccion {
                    objectName: "botonReintentarPerfiles"
                    text: qsTr("Reintentar")
                    descripcion: qsTr("Volver a cargar los perfiles SAT")
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: pagina.perfiles.cargar()
                }
            }
        }

        // ---- Formulario ------------------------------------------------
        ScrollView {
            id: panel
            objectName: "panelPerfil"
            visible: pagina.hayFormulario
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: pagina.anchoAmplio ? 1 : -1
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: panel.availableWidth
                spacing: 6

                Label {
                    text: pagina.perfiles.modo === PerfilesSatViewModel.Nuevo ? qsTr("Nuevo perfil")
                                                                              : qsTr("Editar perfil")
                    font.pixelSize: 16
                    font.bold: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                Label { text: qsTr("RFC"); Accessible.ignored: true }
                TextField {
                    id: campoRfc
                    objectName: "campoRfcPerfil"
                    Layout.fillWidth: true
                    text: pagina.perfiles.rfc
                    readOnly: !pagina.perfiles.rfcEditable
                    enabled: !pagina.perfiles.guardando
                    maximumLength: 13
                    onTextEdited: pagina.perfiles.rfc = text
                    onAccepted: pagina.perfiles.guardar()
                    Accessible.name: pagina.perfiles.rfcEditable ? qsTr("RFC del perfil")
                                                                 : qsTr("RFC del perfil, no editable")
                    Accessible.description: pagina.perfiles.errorRfc
                }
                Label {
                    objectName: "errorRfcPerfil"
                    visible: text.length > 0
                    text: pagina.perfiles.errorRfc
                    color: "#b00020"
                    font.bold: true
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: qsTr("Error en RFC: %1").arg(text)
                }
                Label {
                    visible: !pagina.perfiles.rfcEditable
                    text: qsTr("El RFC identifica al perfil y no se puede cambiar.")
                    opacity: 0.75
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Label { text: qsTr("Nombre descriptivo"); Accessible.ignored: true }
                TextField {
                    id: campoNombre
                    objectName: "campoNombrePerfil"
                    Layout.fillWidth: true
                    text: pagina.perfiles.nombre
                    enabled: !pagina.perfiles.guardando
                    onTextEdited: pagina.perfiles.nombre = text
                    onAccepted: pagina.perfiles.guardar()
                    Accessible.name: qsTr("Nombre descriptivo del perfil")
                    Accessible.description: pagina.perfiles.errorNombre
                }
                Label {
                    objectName: "errorNombrePerfil"
                    visible: text.length > 0
                    text: pagina.perfiles.errorNombre
                    color: "#b00020"
                    font.bold: true
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: qsTr("Error en nombre: %1").arg(text)
                }
                Label {
                    objectName: "errorPerfil"
                    visible: text.length > 0 && pagina.perfiles.campoConError === ""
                    text: pagina.perfiles.errorMessage
                    color: "#b00020"
                    font.bold: true
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: qsTr("Error: %1").arg(text)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    BusyIndicator {
                        running: pagina.perfiles.guardando
                        visible: running
                        implicitWidth: 24
                        implicitHeight: 24
                        Accessible.name: qsTr("Guardando")
                    }
                    BotonAccion {
                        objectName: "botonDescartarPerfil"
                        text: qsTr("Descartar")
                        enabled: pagina.perfiles.sucio && !pagina.perfiles.guardando
                        onClicked: pagina.perfiles.descartar()
                    }
                    BotonAccion {
                        objectName: "botonGuardarPerfil"
                        text: qsTr("Guardar")
                        highlighted: true
                        enabled: pagina.perfiles.puedeGuardar
                        onClicked: pagina.perfiles.guardar()
                    }
                }

                // ---- e.firma (solo perfil guardado) ----
                GroupBox {
                    objectName: "seccionEFirma"
                    visible: pagina.perfiles.modo === PerfilesSatViewModel.Edicion
                    title: qsTr("e.firma")
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Grouping
                    Accessible.name: title

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 6

                        RowLayout {
                            spacing: 8
                            Layout.fillWidth: true
                            EstadoBadge {
                                objectName: "badgeEFirmaSeleccion"
                                clave: pagina.perfiles.seleccionPreparacion
                                texto: pagina.perfiles.seleccionEstadoTexto
                                contexto: qsTr("e.firma")
                            }
                            Label {
                                objectName: "listoSeleccion"
                                text: !pagina.perfiles.seleccionActiva
                                      ? qsTr("Inactivo: no disponible para solicitudes")
                                      : (pagina.perfiles.seleccionListo ? qsTr("Listo para solicitudes")
                                                                        : qsTr("No listo para solicitudes"))
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                        Label {
                            visible: pagina.perfiles.sucio
                            text: qsTr("Guarda el perfil antes de gestionar su e.firma.")
                            opacity: 0.75
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            spacing: 8
                            BotonAccion {
                                id: botonEFirma
                                objectName: "botonEFirma"
                                text: pagina.perfiles.tieneCredencial ? qsTr("Reemplazar e.firma")
                                                                      : qsTr("Registrar e.firma")
                                descripcion: pagina.perfiles.tieneCredencial
                                             ? qsTr("Pide confirmacion antes de capturar la nueva e.firma")
                                             : qsTr("Elegir certificado, llave y contrasena")
                                enabled: pagina.perfiles.puedeGestionarEFirma
                                onClicked: pagina.gestionarEFirma()
                            }
                            BotonAccion {
                                objectName: "botonReintentarEstadoSeleccion"
                                visible: pagina.perfiles.seleccionPreparacion === "EstadoNoDisponible"
                                text: qsTr("Reintentar estado")
                                descripcion: qsTr("Volver a consultar el estado de la e.firma")
                                onClicked: pagina.perfiles.reintentarEstado(pagina.perfiles.perfilId)
                            }
                        }
                    }
                }
            }
        }
    }
}
