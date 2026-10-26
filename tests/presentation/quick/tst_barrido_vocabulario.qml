import QtQuick
import QtQuick.Controls
import QtTest
import SatCfdiDownloader

// T024.1 (criterio 1): prueba negativa del barrido de vocabulario del arbol
// de items (BarridoVocabulario, context property "barrido"). Las vistas del
// recorrido normal se barren en el cleanup() de sus propias pruebas
// (tst_facturas_page, tst_facturas_componentes, tst_historial_page,
// tst_perfiles_sat_page, tst_efirma_dialogo, tst_respaldo_page,
// tst_exportar_facturas, tst_visor_pdf y tst_aviso_raiz_paquetes).
TestCase {
    id: caso
    name: "BarridoVocabulario"
    when: windowShown
    visible: true
    width: 640
    height: 480

    Component {
        id: prohibidosComp
        Item {
            Label { text: "Descargar 3 PAQUETES" }
            TextField { placeholderText: "Buscar solicitudes" }
            Item {
                visible: false // oculto: tambien se revisa
                Accessible.name: "Verificación en curso"
            }
            Dialog { title: "Nueva solicitud" } // cerrado: tambien se revisa
            // Excepcion de otro origen (DialogoRestaurarRespaldo.qml): aqui falla.
            Label { text: "Cancelar verificación" }
        }
    }

    // T024.1 (revision): tooltip perezoso con texto dinamico; nunca se muestra.
    Component {
        id: tooltipComp
        Item {
            property int n: 3
            Button {
                text: "Abrir"
                ToolTip.visible: false
                ToolTip.text: "Descargar " + parent.n + " paquetes"
            }
            BotonAccion { text: "Ver"; atajo: "⌘" + "S" + " solicitud" }
        }
    }

    // Literal directo (sin qsTr) en QML.
    Component {
        id: literalComp
        Label { text: "Crear solicitud" }
    }

    Component {
        id: limpioComp
        Item {
            Label { text: "Facturas descargadas" }
            Item { Accessible.name: "Descarga del mes"; Accessible.description: "Archivos del SAT" }
        }
    }

    function test_detectaLiteralesProhibidos() {
        const o = createTemporaryObject(prohibidosComp, caso)
        const fallas = barrido.revisarArbol(o)
        // Cinco literales; los controles internos (TextField, Dialog) repiten
        // algunos con su propio origen.
        verify(fallas.length >= 5, fallas.join("\n"))
        for (const termino of ["paquetes", "solicitudes", "verificacion", "solicitud"])
            verify(fallas.some(f => f.endsWith("-> " + termino)), termino)
        verify(fallas.some(f => f.indexOf("tst_barrido_vocabulario.qml") === 0), fallas[0])
    }

    function test_detectaTooltipDinamico() {
        const o = createTemporaryObject(tooltipComp, caso)
        const fallas = barrido.revisarArbol(o)
        verify(fallas.some(f => f.indexOf("\"Descargar 3 paquetes\"") >= 0), fallas.join("\n"))
        verify(fallas.some(f => f.indexOf("solicitud)\"") >= 0), fallas.join("\n"))
    }

    function test_detectaLiteralDirecto() {
        const o = createTemporaryObject(literalComp, caso)
        const fallas = barrido.revisarArbol(o)
        verify(fallas.some(f => f.endsWith("-> solicitud")), fallas.join("\n"))
    }

    function test_textosPermitidos() {
        const o = createTemporaryObject(limpioComp, caso)
        const antes = barrido.textosRevisados
        compare(barrido.revisarArbol(o), [])
        verify(barrido.textosRevisados >= antes + 3)
        compare(barrido.normalizar("  Verificación  DE Paquetes "), "verificacion de paquetes")
        compare(barrido.revisarTexto("DialogoRestaurarRespaldo.qml", "Cancelar verificación"), [])
        compare(barrido.revisarTexto("FacturasPage.qml", "Cancelar verificación").length, 1)
    }
}
