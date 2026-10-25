// T026.2 (criterio 2, "no hace peticiones de red"): exporta con el renderer
// real un CFDI cuyo texto trae `<b>`, `<img src=http://...>` y `file://`, en un
// proceso que NO enlaza QtNetwork (este ejecutable no usa Qt Pdf, que si la
// carga). Al terminar se comprueba con dyld que ninguna imagen QtNetwork esta
// cargada: sin la pila de red de Qt no existe QNetworkAccessManager ni
// cargador de recursos que pudiera resolver esas URL. Que el texto se dibuje
// literal lo comprueba TestExportacionServicio::pdfTextoHostilSeDibujaLiteral
// extrayendo el texto del PDF con Qt Pdf.

#include "TestExportacionServicioComun.h"

#include "fakes/FakeDestinoExportacion.h"
#include "infrastructure/cfdi/pdf/RenderizadorPdfCfdi.h"

#include <QTest>

#if defined(Q_OS_MACOS)
#include <mach-o/dyld.h>
#endif

using namespace satcfdi;
using namespace exportacionpruebas;

class TestExportacionServicioSinRed : public QObject {
    Q_OBJECT

private slots:
    void textoHostilSinPilaDeRed();
};

void TestExportacionServicioSinRed::textoHostilSinPilaDeRed()
{
#if !defined(Q_OS_MACOS)
    QSKIP("La comprobacion de imagenes cargadas usa dyld (macOS).");
#else
    Banco b;
    QVERIFY(b.preparar());
    QByteArray xml = xml40(7);
    xml.replace("Descripcion=\"Concepto sintetico\"",
                "Descripcion=\"&lt;b&gt;negrita&lt;/b&gt; &lt;img src=http://127.0.0.1:9/x.png&gt; "
                "file:///etc/hosts\"");
    xml.replace("Nombre=\"EMISOR SINTETICO\"", "Nombre=\"&lt;img src=file:///etc/hosts&gt;\"");
    const Banco::Paquete p = b.paquete({xml});
    QVERIFY(!p.id.isEmpty());
    PlanExportacion plan;
    plan.facturas = {b.factura(p, 0, 7)};
    fakes::FakeDestinoExportacion destino;
    ExportacionFacturasService s(
        puertos(destino, b.lector, *b.archivos, [] { return std::make_unique<RenderizadorPdfCfdi>(); }));
    const auto r = esperarFuturo(s.iniciar(plan, QStringLiteral("/destino"), QStringLiteral("hostil.zip"), true));
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
    QCOMPARE(r->pdfExportados, 1);
    const auto zip = leerZipBytes(*destino.archivo(QStringLiteral("/destino"), QStringLiteral("hostil.zip")));
    QVERIFY(zip);
    QVERIFY(zip->value(QStringLiteral("emitidas/2026-09/") + uuidN(7) + QStringLiteral(".pdf")).startsWith("%PDF-"));

    int imagenes = 0;
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const QByteArray nombre(_dyld_get_image_name(i));
        ++imagenes;
        QVERIFY2(!nombre.contains("QtNetwork"), nombre.constData());
    }
    QVERIFY(imagenes > 0);
#endif
}

QTEST_MAIN(TestExportacionServicioSinRed)

#include "TestExportacionServicioSinRed.moc"
