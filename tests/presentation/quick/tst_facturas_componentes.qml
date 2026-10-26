import QtQuick
import QtTest
import SatCfdiDownloader

// Componentes de Facturas y correcciones de espaciado de T023.4: FranjaMes
// con datos sueltos (accion en curso, aviso, motivo), BotonAccion "enlace"
// sin padding, AvisoEnLinea y SelectorSegmentado con tokens de Theme, y
// FilaCfdi con la linea "Completo" alineada y fondo transparente en reposo.
TestCase {
    id: caso
    name: "FacturasComponentes"
    when: windowShown
    visible: true
    width: 960
    height: 640

    Component {
        id: franjaComp
        FranjaMes {
            width: 700
            vm: fixtureFacturas.franjaSuelta
        }
    }
    Component {
        id: botonComp
        BotonAccion { }
    }
    Component {
        id: avisoComp
        AvisoEnLinea { width: 400 }
    }
    Component {
        id: selectorComp
        SelectorSegmentado {
            opciones: [{ clave: "a", texto: "Emitidas (3)" }, { clave: "b", texto: "Recibidas (4)" }]
            valor: "a"
        }
    }
    Component {
        id: filaComp
        FilaCfdi {
            width: 700
            fecha: "3 sep 2026"
            tipoTexto: "Ingreso"
            serieFolio: "A-12"
            emisorRfc: "XEXX010101000"
            emisorNombre: "Cliente Ejemplo"
            total: "$1,160.00"
            moneda: "MXN"
            uuid: "0A1B2C3D-0000-4000-8000-000000000001"
            uuidCorto: "0A1B2C3D"
            estadoParseoTexto: "Completo"
            mostrarReceptor: false
        }
    }

    function init() {
        failOnWarning(/^(?!Populating font family aliases took).*/)
        Theme.oscuro = false
    }

    function cleanup() {
        // T024.1: barrido de vocabulario del estado final (Diagnostico excluido).
        const fallasVocabulario = barrido.revisarVentana(caso)
        verify(fallasVocabulario.length === 0, fallasVocabulario.join("\n"))
        Theme.oscuro = false
    }

    function test_franjaAccionEnCursoAvisoYMotivo_data() {
        return [{ tag: "claro", oscuro: false }, { tag: "oscuro", oscuro: true }]
    }

    function test_franjaAccionEnCursoAvisoYMotivo(d) {
        Theme.oscuro = d.oscuro
        fixtureFacturas.fijarFranja({
            estado: FranjaMesViewModel.SinDescargar, estadoTexto: "Sin descargar", tono: "neutro",
            icono: "circle-dashed", titular: "Las facturas de septiembre 2026 no están en este equipo",
            descripcion: "Descarga del SAT las emitidas y recibidas del mes.",
            accion: FranjaMesViewModel.DescargarFacturas, textoAccion: "Descargar facturas",
            accionHabilitada: true, ejecutando: true, aviso: "El mes cambió mientras se preparaba la descarga."
        })
        const f = createTemporaryObject(franjaComp, caso)
        verify(f)
        const boton = findChild(f, "botonAccionFranja")
        // En curso: indicador en el boton y sin segundo clic.
        verify(boton.cargando)
        verify(!boton.enabled)
        verify(findChild(f, "avisoFranja").visible)
        verify(!findChild(f, "motivoFranja").visible)
        compare(f.Accessible.name, fixtureFacturas.franjaSuelta.nombreAccesible)
        // Deshabilitada con motivo.
        fixtureFacturas.fijarFranja({
            estado: FranjaMesViewModel.Preparando, estadoTexto: "Preparando", tono: "info", icono: "clock",
            titular: "Preparando las facturas de septiembre 2026", ocupado: true,
            etapa: "Esperando a que el SAT prepare las facturas", descripcion: "Esperando a que el SAT prepare las facturas",
            nota: "Puedes cerrar la ventana; te avisaremos.",
            accion: FranjaMesViewModel.DescargarFacturas, textoAccion: "Descargar facturas", accionHabilitada: false,
            motivoDeshabilitada: "Ya se están preparando las facturas de este mes."
        })
        verify(!boton.enabled)
        verify(findChild(f, "motivoFranja").visible)
        verify(findChild(f, "etapaFranja").visible)
        verify(!findChild(f, "descripcionFranja").visible)
        // Sin accion: sin boton.
        fixtureFacturas.fijarFranja({ estado: FranjaMesViewModel.Disponibles, estadoTexto: "Disponibles", tono: "exito",
                                      icono: "check-circle", titular: "Facturas de agosto 2026 completas" })
        verify(!f.conAccion)
        verify(!boton.visible)
    }

    function test_botonEnlaceSinPadding() {
        const enlace = createTemporaryObject(botonComp, caso, { variante: "enlace", text: "Ver detalles técnicos", compacto: true })
        const plano = createTemporaryObject(botonComp, caso, { variante: "plano", text: "Ver detalles técnicos", compacto: true })
        compare(enlace.leftPadding, 0)
        compare(enlace.rightPadding, 0)
        compare(plano.leftPadding, Theme.espacioS)
        compare(enlace.background.color, Qt.color("transparent"))
    }

    function test_avisoYSelectorConTokens() {
        const aviso = createTemporaryObject(avisoComp, caso, { titulo: "Este perfil no tiene e.firma" })
        const titulo = findChild(aviso, "tituloAviso")
        const pos = titulo.mapToItem(aviso, 0, 0)
        // Padding espacioM (el titulo se centra con el icono de 18).
        verify(pos.y >= Theme.espacioM)
        verify(aviso.height >= titulo.height + 2 * Theme.espacioM)
        const selector = createTemporaryObject(selectorComp, caso)
        compare(selector.implicitHeight, Theme.altoControl)
    }

    function test_filaCfdiLineasAlineadasYFondo_data() {
        return [{ tag: "claro", oscuro: false }, { tag: "oscuro", oscuro: true }]
    }

    function test_filaCfdiLineasAlineadasYFondo(d) {
        Theme.oscuro = d.oscuro
        const fila = createTemporaryObject(filaComp, caso)
        verify(fila)
        waitForRendering(fila)
        const y = (nombre) => Math.round(findChild(fila, nombre).mapToItem(fila, 0, 0).y)
        compare(y("estadoParseoCfdi"), y("monedaCfdi"))
        compare(y("estadoParseoCfdi"), y("tipoCfdi"))
        compare(y("uuidCfdi"), y("fechaCfdi"))
        compare(fila.topPadding, Theme.espacioS)
        verify(fila.height >= Theme.altoFila)
        compare(fila.background.color, Qt.color("transparent"))
        fila.seleccionada = true
        compare(fila.background.color, Theme.seleccion)
    }
}
