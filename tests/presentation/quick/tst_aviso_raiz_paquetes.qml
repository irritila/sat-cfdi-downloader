import QtQuick
import QtTest
import SatCfdiDownloader

// T019 D8: FranjaAvisoRaizPaquetes sobre AvisoRaizPaquetesViewModel real
// (fixtureAvisoRaiz). Sin aviso no ocupa espacio; con cada motivo se ve, usa
// la variante y el texto del view model, se anuncia como AlertMessage con
// las rutas ~/... y no ofrece cerrar. Claro y oscuro. Cualquier warning falla.
TestCase {
    id: caso
    name: "AvisoRaizPaquetes"
    when: windowShown
    visible: true
    width: 960
    height: 640

    function init() {
        failOnWarning(/^(?!Populating font family aliases took).*/)
    }

    function cleanup() {
        Theme.oscuro = false
    }

    Component {
        id: franjaComp
        FranjaAvisoRaizPaquetes { width: 800 }
    }

    function crear(motivo, usaNueva) {
        const vm = fixtureAvisoRaiz.crear(motivo, usaNueva === true)
        verify(vm, "sin view model")
        const f = createTemporaryObject(franjaComp, caso, { aviso: vm })
        verify(f, "no se pudo crear la franja")
        return f
    }

    function hijo(raiz, nombre) {
        return findChild(raiz, nombre)
    }

    function test_sinAvisoNoOcupaEspacio() {
        const f = crear(AvisoRaizPaquetesViewModel.Ninguno)
        compare(f.visible, false)
        compare(f.implicitHeight, 0)
    }

    function test_motivos_data() {
        const filas = []
        const motivos = [
            { tag: "conflicto", motivo: AvisoRaizPaquetesViewModel.Conflicto, variante: "advertencia",
              efectiva: "~/SAT-CFDI-Downloader/paquetes" },
            { tag: "error", motivo: AvisoRaizPaquetesViewModel.Error, variante: "advertencia",
              efectiva: "~/SAT-CFDI-Downloader/paquetes" },
            { tag: "error-anterior-no-es-carpeta", motivo: AvisoRaizPaquetesViewModel.Error, usaNueva: true,
              variante: "advertencia", efectiva: "~/Archivo-Fiscal/paquetes" },
            { tag: "raiz-no-disponible", motivo: AvisoRaizPaquetesViewModel.RaizNoDisponible, variante: "error",
              efectiva: "~/Archivo-Fiscal/paquetes" },
        ]
        for (const m of motivos) {
            for (const oscuro of [false, true]) {
                filas.push({ tag: m.tag + (oscuro ? "-oscuro" : "-claro"), motivo: m.motivo,
                             usaNueva: m.usaNueva === true,
                             variante: m.variante, efectiva: m.efectiva, oscuro: oscuro })
            }
        }
        return filas
    }

    function test_motivos(fila) {
        Theme.oscuro = fila.oscuro
        const f = crear(fila.motivo, fila.usaNueva)
        compare(f.visible, true)
        verify(f.implicitHeight > 0)
        compare(f.color, Theme.superficie)

        const aviso = hijo(f, "avisoRaizPaquetes")
        verify(aviso)
        compare(aviso.variante, fila.variante)
        compare(aviso.color, Theme.tono(fila.variante).fondo)
        compare(aviso.titulo, f.aviso.titulo)
        compare(aviso.descripcion, f.aviso.descripcion)
        verify(aviso.titulo.length > 0)

        // Accesible: AlertMessage con titulo y descripcion (rutas ~/...).
        compare(aviso.Accessible.role, Accessible.AlertMessage)
        const nombre = aviso.Accessible.name
        verify(nombre.indexOf(aviso.titulo) === 0, nombre)
        verify(nombre.indexOf("~/Archivo-Fiscal/paquetes") >= 0, nombre)
        verify(nombre.indexOf("Esta sesión usa " + fila.efectiva) >= 0, nombre)
        verify(nombre.indexOf("/Users/") < 0, nombre)
        if (fila.motivo !== AvisoRaizPaquetesViewModel.RaizNoDisponible)
            verify(nombre.indexOf("~/SAT-CFDI-Downloader/paquetes") >= 0, nombre)

        // No se descarta: sin boton (ni de cerrar ni de otra accion).
        compare(aviso.botonAccion.visible, false)
        compare(aviso.textoAccion, "")
    }

    function test_noTomaFoco() {
        const f = crear(AvisoRaizPaquetesViewModel.Conflicto)
        compare(f.activeFocusOnTab, false)
        compare(hijo(f, "avisoRaizPaquetes").activeFocusOnTab, false)
    }
}
