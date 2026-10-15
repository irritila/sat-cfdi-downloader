// satcfdi_visor_pdf_tests (T015.4): VisorPdfCfdiViewModel y su conexion con
// ExploradorCfdiViewModel sobre fakes de promesas manuales
// (PrevisualizacionFake, FinderVistaPreviaFake y ConsultaCatalogoFake): estados
// generando / listo / error por tipo, la ultima peticion gana, Guardar PDF y
// Abrir en Vista Previa con exito y fallo. Sin QML (la pagina se prueba en
// satcfdi_presentation_quicktests). Rutas y datos ficticios.

#include "ConsultaCatalogoFake.h"
#include "PrevisualizacionFake.h"

#include "CfdiListModel.h"
#include "ExploradorCfdiViewModel.h"
#include "VisorPdfCfdiViewModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace satcfdi;
using Estado = VisorPdfCfdiViewModel::Estado;
using Motivo = MotivoPrevisualizacion;
using EstadoFinder = ResultadoAccionFinder::Estado;

Q_DECLARE_METATYPE(satcfdi::MotivoPrevisualizacion)

namespace {

const QString kId = QStringLiteral("00000000-0000-4000-8000-000000000003");
const QString kOtroId = QStringLiteral("00000000-0000-4000-8000-000000000004");

void procesarEventos()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
}

ResumenIndiceSolicitud resumen(int total)
{
    ResumenIndiceSolicitud r;
    r.paquetesDescargados = 1;
    r.paquetesIndexados = 1;
    r.cfdiTotal = total;
    return r;
}

// Explorador abierto (kId) con 3 CFDI, previsualizacion y Finder fakes.
struct Escenario {
    explicit Escenario(bool conServicio = true)
    {
        explorador.setConsulta(&consulta);
        explorador.setIntervaloBusqueda(0);
        explorador.setAccionesFinder(&finder);
        if (conServicio) {
            explorador.setPrevisualizacion(&servicio);
        }
    }
    void abrir()
    {
        QVERIFY(explorador.abrir(kId));
        consulta.resolverResumen(consulta.resumenes.size() - 1, resumen(3));
        consulta.resolverLista(consulta.listas.size() - 1, fakes::cfdis(3));
        QTRY_COMPARE(explorador.estado(), ExploradorCfdiViewModel::Estado::ConDatos);
    }
    VisorPdfCfdiViewModel& visor() { return *explorador.visor(); }
    // Abre el visor de la fila y entrega su PDF.
    void listo(int fila = 0)
    {
        QVERIFY(explorador.previsualizar(fila));
        servicio.resolverPdf(servicio.generaciones.size() - 1);
        QTRY_COMPARE(visor().estado(), Estado::Listo);
    }

    fakes::ConsultaCatalogoFake consulta;
    fakes::PrevisualizacionFake servicio;
    fakes::FinderVistaPreviaFake finder;
    ExploradorCfdiViewModel explorador;
};

} // namespace

class TestVisorPdfCfdi : public QObject {
    Q_OBJECT

private slots:
    void sinServicioSoloIntencion();
    void generandoListoYCerrar();
    void erroresPorTipo_data();
    void erroresPorTipo();
    void futureCanceladoEsFallo();
    void ultimaPeticionGana();
    void otraSolicitudYSalirCierranElVisor();
    void documentoIlegibleOfreceReintentarYVistaPrevia();
    void guardarExito();
    void guardarFallo();
    void guardarRutaNoLocal();
    void guardadoTardioSeIgnora();
    void vistaPreviaExitoYFallo();
};

void TestVisorPdfCfdi::sinServicioSoloIntencion()
{
    Escenario e(false);
    e.abrir();
    QSignalSpy intencion(&e.explorador, &ExploradorCfdiViewModel::previsualizacionSolicitada);
    QVERIFY(!e.visor().disponible());
    QVERIFY(e.explorador.previsualizar(1));
    QCOMPARE(intencion.size(), 1);
    QCOMPARE(intencion.constFirst().at(1).toString(), QStringLiteral("PAQ_01"));
    QCOMPARE(intencion.constFirst().at(2).toInt(), 1);
    QVERIFY(!e.visor().abierto());
    QCOMPARE(e.visor().estado(), Estado::Inactivo);
    QVERIFY(!e.explorador.previsualizar(7)); // fila inexistente
}

void TestVisorPdfCfdi::generandoListoYCerrar()
{
    Escenario e;
    e.abrir();
    QVERIFY(e.visor().disponible());
    QSignalSpy estado(&e.visor(), &VisorPdfCfdiViewModel::estadoChanged);
    QVERIFY(e.explorador.previsualizar(1));
    QCOMPARE(e.visor().estado(), Estado::Generando);
    QVERIFY(e.visor().abierto());
    QCOMPARE(e.servicio.pedidos, QStringList{QStringLiteral("PAQ_01:1")});
    QCOMPARE(e.servicio.solicitudes.constFirst().texto(), kId);
    QCOMPARE(e.visor().titulo(), QStringLiteral("Ingreso A-101"));
    QCOMPARE(e.visor().subtitulo(), QStringLiteral("XAXX010101000 · 3 sep 2026 · 1160.00 MXN"));
    QVERIFY(!e.visor().nombreAccesibleCfdi().isEmpty());
    QVERIFY(e.visor().fuente().isEmpty());
    QVERIFY(!e.visor().puedeGuardar());
    QVERIFY(!e.visor().puedeAbrirVistaPrevia());
    QVERIFY(e.visor().errorMessage().isEmpty());

    e.servicio.resolverPdf(0, QStringLiteral("/tmp/cache/cfdi-pdf/x.pdf"), QStringLiteral("CFDI-A101-0A1B2C3D.pdf"));
    QTRY_COMPARE(e.visor().estado(), Estado::Listo);
    QCOMPARE(e.visor().fuente(), QUrl::fromLocalFile(QStringLiteral("/tmp/cache/cfdi-pdf/x.pdf")));
    QCOMPARE(e.visor().nombreSugerido(), QStringLiteral("CFDI-A101-0A1B2C3D.pdf"));
    QVERIFY(e.visor().puedeGuardar());
    QVERIFY(e.visor().puedeAbrirVistaPrevia());
    QVERIFY(!e.visor().puedeReintentar());
    const QString documentos = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!documentos.isEmpty()) {
        QCOMPARE(e.visor().carpetaGuardado(), QUrl::fromLocalFile(documentos));
        QCOMPARE(e.visor().archivoSugerido(),
                 QUrl::fromLocalFile(QDir(documentos).filePath(QStringLiteral("CFDI-A101-0A1B2C3D.pdf"))));
    }

    e.visor().cerrar();
    QCOMPARE(e.visor().estado(), Estado::Inactivo);
    QVERIFY(!e.visor().abierto());
    QVERIFY(e.visor().fuente().isEmpty());
    QVERIFY(!e.visor().puedeGuardar());
    QVERIFY(estado.size() >= 3);
}

void TestVisorPdfCfdi::erroresPorTipo_data()
{
    QTest::addColumn<Motivo>("motivo");
    QTest::addColumn<QString>("clave");
    QTest::addColumn<QString>("mensaje");
    QTest::addColumn<bool>("reintentar");
    QTest::newRow("origen") << Motivo::OrigenNoDisponible << QStringLiteral("origenNoDisponible")
                            << QStringLiteral("El ZIP de esta factura no está disponible.") << true;
    QTest::newRow("version") << Motivo::VersionNoSoportada << QStringLiteral("versionNoSoportada")
                             << QStringLiteral("La app no representa esta versión de CFDI.") << false;
    QTest::newRow("ilegible") << Motivo::Ilegible << QStringLiteral("ilegible")
                              << QStringLiteral("No se pudo leer el XML de esta factura.") << false;
    QTest::newRow("cancelada") << Motivo::Cancelada << QStringLiteral("cancelada")
                               << QStringLiteral("Se canceló la generación del PDF.") << true;
    QTest::newRow("fallo") << Motivo::Fallo << QStringLiteral("fallo")
                           << QStringLiteral("No se pudo generar el PDF.") << true;
}

void TestVisorPdfCfdi::erroresPorTipo()
{
    QFETCH(Motivo, motivo);
    QFETCH(QString, clave);
    QFETCH(QString, mensaje);
    QFETCH(bool, reintentar);
    Escenario e;
    e.abrir();
    QVERIFY(e.explorador.previsualizar(0));
    e.servicio.fallarPdf(0, motivo);
    QTRY_COMPARE(e.visor().estado(), Estado::Error);
    QCOMPARE(e.visor().motivo(), clave);
    QCOMPARE(e.visor().errorMessage(), mensaje);
    QVERIFY(!e.visor().errorDescripcion().isEmpty());
    QCOMPARE(e.visor().puedeReintentar(), reintentar);
    // Sin PDF: ni guardar ni Vista Previa.
    QVERIFY(e.visor().fuente().isEmpty());
    QVERIFY(!e.visor().puedeGuardar());
    QVERIFY(!e.visor().puedeAbrirVistaPrevia());

    e.visor().reintentar();
    if (reintentar) {
        QCOMPARE(e.servicio.generaciones.size(), 2);
        QCOMPARE(e.visor().estado(), Estado::Generando);
        QVERIFY(e.visor().errorMessage().isEmpty());
        e.servicio.resolverPdf(1);
        QTRY_COMPARE(e.visor().estado(), Estado::Listo);
    } else {
        QCOMPARE(e.servicio.generaciones.size(), 1);
        QCOMPARE(e.visor().estado(), Estado::Error);
    }
}

void TestVisorPdfCfdi::futureCanceladoEsFallo()
{
    Escenario e;
    e.abrir();
    QVERIFY(e.explorador.previsualizar(0));
    e.servicio.generaciones.cancelar(0);
    QTRY_COMPARE(e.visor().estado(), Estado::Error);
    QCOMPARE(e.visor().motivo(), QStringLiteral("fallo"));
    QVERIFY(e.visor().puedeReintentar());
}

void TestVisorPdfCfdi::ultimaPeticionGana()
{
    Escenario e;
    e.abrir();
    QVERIFY(e.explorador.previsualizar(0));
    QVERIFY(e.explorador.previsualizar(2));
    QCOMPARE(e.servicio.pedidos, (QStringList{QStringLiteral("PAQ_01:0"), QStringLiteral("PAQ_01:2")}));
    QCOMPARE(e.visor().titulo(), QStringLiteral("Ingreso A-102"));
    // La segunda llega primero; la primera (tardia) se ignora, con exito o error.
    e.servicio.resolverPdf(1, QStringLiteral("/tmp/cache/cfdi-pdf/dos.pdf"));
    QTRY_COMPARE(e.visor().estado(), Estado::Listo);
    e.servicio.fallarPdf(0, Motivo::Fallo);
    procesarEventos();
    QCOMPARE(e.visor().estado(), Estado::Listo);
    QCOMPARE(e.visor().fuente(), QUrl::fromLocalFile(QStringLiteral("/tmp/cache/cfdi-pdf/dos.pdf")));

    // Una respuesta tras cerrar tampoco reabre el visor.
    QVERIFY(e.explorador.previsualizar(1));
    e.visor().cerrar();
    e.servicio.resolverPdf(2);
    procesarEventos();
    QCOMPARE(e.visor().estado(), Estado::Inactivo);

    // Reintentar invalida la respuesta anterior del mismo CFDI.
    QVERIFY(e.explorador.previsualizar(1));
    e.servicio.fallarPdf(3, Motivo::OrigenNoDisponible);
    QTRY_COMPARE(e.visor().estado(), Estado::Error);
    e.visor().reintentar();
    QVERIFY(e.explorador.previsualizar(0)); // y otra fila gana sobre el reintento
    e.servicio.fallarPdf(4, Motivo::Fallo);
    procesarEventos();
    QCOMPARE(e.visor().estado(), Estado::Generando);
    QCOMPARE(e.visor().titulo(), QStringLiteral("Ingreso A-100"));
    e.servicio.resolverPdf(5);
    QTRY_COMPARE(e.visor().estado(), Estado::Listo);
}

void TestVisorPdfCfdi::otraSolicitudYSalirCierranElVisor()
{
    Escenario e;
    e.abrir();
    QVERIFY(e.explorador.previsualizar(0));
    // Reabrir la misma solicitud no cierra el visor.
    QVERIFY(e.explorador.abrir(kId));
    QVERIFY(e.visor().abierto());
    // Otra solicitud lo cierra e ignora el PDF en curso.
    QVERIFY(e.explorador.abrir(kOtroId));
    QVERIFY(!e.visor().abierto());
    e.servicio.resolverPdf(0);
    procesarEventos();
    QCOMPARE(e.visor().estado(), Estado::Inactivo);

    // Salir de la pagina (desactivar) tambien lo cierra.
    e.consulta.resolverResumen(e.consulta.resumenes.size() - 1, resumen(3));
    e.consulta.resolverLista(e.consulta.listas.size() - 1, fakes::cfdis(3));
    QTRY_COMPARE(e.explorador.estado(), ExploradorCfdiViewModel::Estado::ConDatos);
    e.listo(1);
    e.explorador.desactivar();
    QVERIFY(!e.visor().abierto());
}

void TestVisorPdfCfdi::documentoIlegibleOfreceReintentarYVistaPrevia()
{
    Escenario e;
    e.abrir();
    e.listo();
    e.visor().documentoIlegible();
    QCOMPARE(e.visor().estado(), Estado::Error);
    QCOMPARE(e.visor().motivo(), QStringLiteral("documento"));
    QCOMPARE(e.visor().errorMessage(), QStringLiteral("No se pudo mostrar el PDF."));
    QVERIFY(e.visor().puedeReintentar());
    QVERIFY(e.visor().puedeAbrirVistaPrevia());
    QVERIFY(e.visor().puedeGuardar());
    e.visor().reintentar();
    QCOMPARE(e.visor().estado(), Estado::Generando);
    QCOMPARE(e.servicio.generaciones.size(), 2);
    // Solo aplica con el documento listo.
    e.visor().documentoIlegible();
    QCOMPARE(e.visor().estado(), Estado::Generando);
}

void TestVisorPdfCfdi::guardarExito()
{
    Escenario e;
    e.abrir();
    e.listo();
    const QString destino = QDir::tempPath() + QStringLiteral("/facturas/Mi factura.pdf");
    e.visor().guardarEn(QUrl::fromLocalFile(destino));
    QCOMPARE(e.servicio.guardados,
             QStringList{QStringLiteral("/tmp/cache/cfdi-pdf/a.pdf -> ") + QDir::cleanPath(destino)});
    QVERIFY(e.visor().guardando());
    QVERIFY(!e.visor().puedeGuardar()); // sin guardados simultaneos
    e.visor().guardarEn(QUrl::fromLocalFile(destino));
    QCOMPARE(e.servicio.guardados.size(), 1);
    e.servicio.resolverGuardado(0);
    QTRY_VERIFY(!e.visor().guardando());
    QCOMPARE(e.visor().mensaje(), QStringLiteral("PDF guardado como “Mi factura.pdf”."));
    QVERIFY(!e.visor().mensajeEsError());
    QCOMPARE(e.visor().estado(), Estado::Listo);
    // La siguiente vez el dialogo abre en esa carpeta.
    QCOMPARE(e.visor().carpetaGuardado(), QUrl::fromLocalFile(QDir::tempPath() + QStringLiteral("/facturas")));
    QCOMPARE(e.visor().archivoSugerido().toLocalFile(),
             QDir::tempPath() + QStringLiteral("/facturas/CFDI-A12-0A1B2C3D.pdf"));
}

void TestVisorPdfCfdi::guardarFallo()
{
    Escenario e;
    e.abrir();
    e.listo();
    e.visor().guardarEn(QUrl::fromLocalFile(QStringLiteral("/Volumes/sin-permiso/a.pdf")));
    e.servicio.fallarGuardado(0);
    QTRY_VERIFY(!e.visor().guardando());
    QVERIFY(e.visor().mensajeEsError());
    QCOMPARE(e.visor().mensaje(),
             QStringLiteral("No se pudo guardar el PDF. Revisa que la carpeta exista y tengas permiso de escritura."));
    // Nada mas cambia: el PDF sigue visible y se puede reintentar.
    QCOMPARE(e.visor().estado(), Estado::Listo);
    QVERIFY(!e.visor().fuente().isEmpty());
    QVERIFY(e.visor().puedeGuardar());
    // Un future cancelado (p. ej. al cerrar la app) se reporta como fallo.
    e.visor().guardarEn(QUrl::fromLocalFile(QStringLiteral("/Volumes/x/b.pdf")));
    QVERIFY(e.visor().mensaje().isEmpty()); // la accion nueva limpia el mensaje
    e.servicio.copias.cancelar(1);
    QTRY_VERIFY(!e.visor().guardando());
    QVERIFY(e.visor().mensajeEsError());
}

void TestVisorPdfCfdi::guardarRutaNoLocal()
{
    Escenario e;
    e.abrir();
    e.listo();
    e.visor().guardarEn(QUrl(QStringLiteral("https://example.invalid/a.pdf")));
    QVERIFY(e.servicio.guardados.isEmpty());
    QVERIFY(e.visor().mensajeEsError());
    QVERIFY(!e.visor().mensaje().isEmpty());
    e.visor().guardarEn(QUrl());
    QVERIFY(e.servicio.guardados.isEmpty());
}

void TestVisorPdfCfdi::guardadoTardioSeIgnora()
{
    Escenario e;
    e.abrir();
    e.listo();
    e.visor().guardarEn(QUrl::fromLocalFile(QStringLiteral("/tmp/a.pdf")));
    QVERIFY(e.explorador.previsualizar(2)); // otra factura
    QVERIFY(!e.visor().guardando());
    e.servicio.resolverGuardado(0);
    procesarEventos();
    QVERIFY(e.visor().mensaje().isEmpty());
    QCOMPARE(e.visor().estado(), Estado::Generando);
}

void TestVisorPdfCfdi::vistaPreviaExitoYFallo()
{
    Escenario e;
    e.abrir();
    e.listo();
    e.visor().abrirEnVistaPrevia();
    QCOMPARE(e.finder.rutas, QStringList{QStringLiteral("/tmp/cache/cfdi-pdf/a.pdf")});
    QVERIFY(e.visor().abriendoVistaPrevia());
    QVERIFY(!e.visor().puedeAbrirVistaPrevia());
    e.finder.resolver(0, EstadoFinder::Mostrado);
    QTRY_VERIFY(!e.visor().abriendoVistaPrevia());
    QVERIFY(e.visor().mensaje().isEmpty());

    // Fallo con el mensaje D7 de AccesoFinder.
    e.visor().abrirEnVistaPrevia();
    e.finder.resolver(1, EstadoFinder::NoEncontrado, QStringLiteral("Archivo no encontrado"));
    QTRY_VERIFY(!e.visor().abriendoVistaPrevia());
    QVERIFY(e.visor().mensajeEsError());
    QCOMPARE(e.visor().mensaje(), QStringLiteral("Archivo no encontrado"));
    QCOMPARE(e.visor().estado(), Estado::Listo);

    // Fallo sin mensaje y future cancelado: texto generico.
    e.visor().abrirEnVistaPrevia();
    e.finder.resolver(2, EstadoFinder::Fallido);
    QTRY_VERIFY(!e.visor().abriendoVistaPrevia());
    QCOMPARE(e.visor().mensaje(), QStringLiteral("No se pudo abrir el PDF en Vista Previa."));
    e.visor().abrirEnVistaPrevia();
    e.finder.vistasPrevias.cancelar(3);
    QTRY_VERIFY(!e.visor().abriendoVistaPrevia());
    QCOMPARE(e.visor().mensaje(), QStringLiteral("No se pudo abrir el PDF en Vista Previa."));

    // Una respuesta tardia tras cambiar de factura se ignora.
    e.visor().abrirEnVistaPrevia();
    QVERIFY(e.explorador.previsualizar(1));
    e.finder.resolver(4, EstadoFinder::Fallido, QStringLiteral("tarde"));
    procesarEventos();
    QVERIFY(e.visor().mensaje().isEmpty());
}

QTEST_MAIN(TestVisorPdfCfdi)
#include "TestVisorPdfCfdi.moc"
