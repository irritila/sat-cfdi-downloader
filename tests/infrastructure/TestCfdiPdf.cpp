// T015.4: URL del QR (vector del Anexo 20), RenderizadorPdfCfdi y
// AlmacenPdfCfdiFs. El PDF se verifica con Qt Pdf (QPdfDocument): texto
// extraido por pagina, numero de paginas y rasterizado de la primera pagina
// para medir el QR en puntos y comparar cada modulo con qrcodegen.

#include "CfdiPdfPruebasComun.h"

#include "infrastructure/cfdi/pdf/AlmacenPdfCfdiFs.h"
#include "infrastructure/cfdi/pdf/RenderizadorPdfCfdi.h"
#include "infrastructure/cfdi/pdf/UrlVerificacionCfdi.h"
#include "infrastructure/cfdi/xml/ParserCfdi.h"

#include <qrcodegen.hpp>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QMutex>
#include <QPdfDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>
#include <sys/time.h>

using namespace satcfdi;
using namespace pruebaspdf;

namespace {

QStringList g_logs;
QMutex g_mutexLogs;
QtMessageHandler g_anterior = nullptr;

void capturar(QtMsgType tipo, const QMessageLogContext& contexto, const QString& mensaje)
{
    {
        QMutexLocker b(&g_mutexLogs);
        g_logs.append(mensaje);
    }
    if (g_anterior) {
        g_anterior(tipo, contexto, mensaje);
    }
}

struct DocumentoPdf {
    QByteArray bytes;
    std::unique_ptr<QBuffer> buffer;
    std::unique_ptr<QPdfDocument> doc;

    bool cargar(QByteArray datos)
    {
        bytes = std::move(datos);
        buffer = std::make_unique<QBuffer>(&bytes);
        if (!buffer->open(QIODevice::ReadOnly)) {
            return false;
        }
        doc = std::make_unique<QPdfDocument>();
        doc->load(buffer.get());
        return QTest::qWaitFor([this] { return doc->status() == QPdfDocument::Status::Ready
                                              || doc->status() == QPdfDocument::Status::Error; },
                               5000)
               && doc->status() == QPdfDocument::Status::Ready;
    }

    // Texto de la pagina con espacios colapsados (el extractor parte lineas).
    QString texto(int pagina) const
    {
        return doc->getAllText(pagina).text().simplified();
    }

    // Texto de todas las paginas.
    QString todo() const
    {
        QString t;
        for (int p = 0; p < doc->pageCount(); ++p) {
            t += texto(p) + QLatin1Char(' ');
        }
        return t;
    }
};

ResultadoParseoCfdi parsear(const OpcionesCfdi& o)
{
    return parsearCfdi(xmlCfdi(o));
}

PdfRenderizado renderizar(const ResultadoParseoCfdi& r)
{
    RenderizadorPdfCfdi renderer;
    auto pdf = renderer.renderizar(r);
    if (!pdf.esExito()) {
        return {};
    }
    return pdf.valor();
}

ClavePdfCfdi clave(int n)
{
    return ClavePdfCfdi{QStringLiteral("5A1B2C3D-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')),
                        QString(64, QLatin1Char('a' + (n % 6))), 1};
}

void fijarMtime(const QString& ruta, qint64 segundos)
{
    struct timeval tiempos[2];
    tiempos[0].tv_sec = static_cast<time_t>(segundos);
    tiempos[0].tv_usec = 0;
    tiempos[1] = tiempos[0];
    ::utimes(QFile::encodeName(ruta).constData(), tiempos);
}

} // namespace

class TestCfdiPdf : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { g_anterior = qInstallMessageHandler(capturar); }
    void cleanupTestCase() { qInstallMessageHandler(g_anterior); }

    // URL
    void urlReproduceElEjemploDelAnexo20();
    void totalParaQr_data();
    void totalParaQr();
    void urlSinDatosNoSeConstruye();
    void urlRfcConAmpersand();

    // Render
    void ingresoConVariosConceptos();
    void conceptosEnVariasPaginas();
    void qrMideAlMenos78PtYCodificaLaUrl();
    void pagoMuestraComplementoDePagos();
    void nominaMuestraComplementoDeNomina();
    void complementoNoRepresentadoSeNombra();
    void noSoportadoEIlegibleNoGeneranPdf();
    void acentosYEnieDelXmlAlPdf();

    // Cache
    void cacheAciertoYMiss();
    void cacheExpulsaLosMenosUsados();
    void cacheNoEscribibleUsaTemporal();
    void copiarEsAtomicoYFallaSinTocarDestino();
    void copiarRechazaOrigenAjeno();

    void logsSinDatosFiscales();
};

void TestCfdiPdf::urlReproduceElEjemploDelAnexo20()
{
    // Valores conocidos del ejemplo del Anexo 20 (re, rr, total, fe); el UUID
    // completo del ejemplo no esta en el repositorio: se usa uno sintetico.
    const auto url = urlVerificacionCfdi(DatosQrCfdi{kUuid, QStringLiteral("VISM990101474"),
                                                     QStringLiteral("XAXX010101000"), QStringLiteral("14300"), kSello});
    QVERIFY(url);
    QCOMPARE(*url, QStringLiteral("https://verificacfdi.facturaelectronica.sat.gob.mx/default.aspx?id=") + kUuid
                       + QStringLiteral("&re=VISM990101474&rr=XAXX010101000&tt=0000014300.000000&fe=rH8/bw=="));
}

void TestCfdiPdf::totalParaQr_data()
{
    QTest::addColumn<QString>("total");
    QTest::addColumn<QString>("esperado");
    QTest::newRow("entero") << "14300" << "0000014300.000000";
    QTest::newRow("dos decimales") << "1101.42" << "0000001101.420000";
    QTest::newRow("cero") << "0" << "0000000000.000000";
    QTest::newRow("cero con decimales") << "0.00" << "0000000000.000000";
    QTest::newRow("ceros superfluos") << "0014300.500" << "0000014300.500000";
    QTest::newRow("seis decimales") << "1.123456" << "0000000001.123456";
    QTest::newRow("cero final no cuenta") << "1.1234560" << "0000000001.123456";
    QTest::newRow("diez enteros") << "9999999999.999999" << "9999999999.999999";
    QTest::newRow("sin decimal tras punto") << "5." << "0000000005.000000";
    QTest::newRow("once enteros") << "10000000000" << "";
    QTest::newRow("siete decimales") << "1.1234567" << "";
    QTest::newRow("negativo") << "-1" << "";
    QTest::newRow("exponente") << "1e3" << "";
    QTest::newRow("vacio") << "" << "";
}

void TestCfdiPdf::totalParaQr()
{
    QFETCH(QString, total);
    QFETCH(QString, esperado);
    const auto tt = satcfdi::totalParaQr(total);
    if (esperado.isEmpty()) {
        QVERIFY(!tt);
    } else {
        QVERIFY(tt);
        QCOMPARE(*tt, esperado);
    }
}

void TestCfdiPdf::urlSinDatosNoSeConstruye()
{
    const DatosQrCfdi base{kUuid, kRfcEmisor, kRfcReceptor, QStringLiteral("1"), kSello};
    QVERIFY(urlVerificacionCfdi(base));
    DatosQrCfdi d = base;
    d.uuid.clear();
    QVERIFY(!urlVerificacionCfdi(d));
    d = base;
    d.rfcReceptor.clear();
    QVERIFY(!urlVerificacionCfdi(d));
    d = base;
    d.sello.clear();
    QVERIFY(!urlVerificacionCfdi(d));
    d = base;
    d.totalTexto = QStringLiteral("abc");
    QVERIFY(!urlVerificacionCfdi(d));
    // Sello mas corto que 8: se usa completo.
    d = base;
    d.sello = QStringLiteral("ab/c=");
    QVERIFY(urlVerificacionCfdi(d)->endsWith(QStringLiteral("&fe=ab/c=")));
}

void TestCfdiPdf::urlRfcConAmpersand()
{
    const auto url = urlVerificacionCfdi(
        DatosQrCfdi{kUuid, QStringLiteral("A&B010101AAA"), kRfcReceptor, QStringLiteral("1"), kSello});
    QVERIFY(url);
    QVERIFY(url->contains(QStringLiteral("&re=A%26B010101AAA&rr=")));
}

void TestCfdiPdf::ingresoConVariosConceptos()
{
    const auto r = parsear(OpcionesCfdi{});
    QCOMPARE(r.estado(), EstadoParseo::Completo);
    const PdfRenderizado pdf = renderizar(r);
    QVERIFY(pdf.contenido.startsWith("%PDF-"));
    DocumentoPdf doc;
    QVERIFY(doc.cargar(pdf.contenido));
    QCOMPARE(doc.doc->pageCount(), pdf.paginas);
    // Carta: 612 x 792 pt.
    QCOMPARE(doc.doc->pagePointSize(0).toSize(), QSize(612, 792));
    const QString t = doc.todo();
    for (const QString& esperado : {
             QStringLiteral("Comprobante Fiscal Digital por Internet"), QStringLiteral("Tipo Ingreso"),
             kRfcEmisor, QStringLiteral("EMISORA SINTETICA DE PRUEBA"), kRfcReceptor,
             QStringLiteral("RECEPTOR SINTETICO"), QStringLiteral("G03"), QStringLiteral("64000"),
             QStringLiteral("2026-09-15 10:20:30"), QStringLiteral("A/B-77"), QStringLiteral("PUE"),
             QStringLiteral("Forma de pago"), QStringLiteral("MXN"),
             // Montos exactos del XML (texto, nunca double).
             QStringLiteral("100.000001"), QStringLiteral("3000.01"), QStringLiteral("12327.59"),
             QStringLiteral("1972.41"), QStringLiteral("0.160000"), QStringLiteral("14300 MXN"),
             QStringLiteral("001 ISR"), QStringLiteral("002 IVA"), QStringLiteral("Articulo sintetico numero 3"),
             // Relacionados, timbre y sello.
             QStringLiteral("5A1B2C3D-0000-4000-8000-0000000000CD"), kUuid, QStringLiteral("2026-09-15 10:21:00"),
             QStringLiteral("00001000000000000002"), QStringLiteral("rH8/bw=="),
             RenderizadorPdfCfdi::leyenda(), QStringLiteral("Página 1 de %1").arg(pdf.paginas)}) {
        QVERIFY2(t.contains(esperado), qPrintable(esperado));
    }
    // Sin el sello completo (solo sus ultimos 8 caracteres).
    QVERIFY(!t.contains(kSello.left(20)));
    QCOMPARE(RenderizadorPdfCfdi::leyenda(),
             QStringLiteral("Visualización generada localmente a partir del XML del CFDI. No es la representación "
                            "impresa emitida por el contribuyente ni valida sello, vigencia o cancelación. El XML es "
                            "el comprobante fiscal."));
}

void TestCfdiPdf::conceptosEnVariasPaginas()
{
    OpcionesCfdi o;
    o.conceptos = 90;
    const PdfRenderizado pdf = renderizar(parsear(o));
    QVERIFY(pdf.paginas >= 3);
    DocumentoPdf doc;
    QVERIFY(doc.cargar(pdf.contenido));
    QCOMPARE(doc.doc->pageCount(), pdf.paginas);
    for (int p = 0; p < pdf.paginas; ++p) {
        const QString t = doc.texto(p);
        QVERIFY2(t.contains(RenderizadorPdfCfdi::leyenda()), qPrintable(QString::number(p)));
        QVERIFY(t.contains(QStringLiteral("Página %1 de %2").arg(p + 1).arg(pdf.paginas)));
    }
    // El encabezado de la tabla se repite en la segunda pagina.
    QVERIFY(doc.texto(1).contains(QStringLiteral("Valor unitario")));
    // Cada concepto aparece una sola vez en el documento (ninguno se pierde).
    const QString todo = doc.todo();
    for (int i = 1; i <= 90; ++i) {
        QCOMPARE(todo.count(QStringLiteral("SKU-%1 ").arg(i)), 1);
    }
    QVERIFY(todo.contains(QStringLiteral("Conceptos (90)")));
    QVERIFY(doc.texto(pdf.paginas - 1).contains(QStringLiteral("Timbre fiscal digital")));
}

void TestCfdiPdf::qrMideAlMenos78PtYCodificaLaUrl()
{
    const auto r = parsear(OpcionesCfdi{});
    DocumentoPdf doc;
    QVERIFY(doc.cargar(renderizar(r).contenido));

    const QRectF rect = RenderizadorPdfCfdi::rectanguloQrPt();
    QVERIFY(rect.width() >= 78.0 && rect.height() >= 78.0); // 2.75 cm
    QVERIFY(rect.right() <= 612.0 - 36.0 && rect.top() >= 36.0);

    // Rasterizado de la primera pagina a 8 px por punto.
    constexpr int kEscala = 8;
    const QImage pagina = doc.doc->render(0, QSize(612 * kEscala, 792 * kEscala));
    QVERIFY(!pagina.isNull());
    // El render es transparente donde no hay tinta: se compone sobre blanco.
    QImage img(pagina.size(), QImage::Format_RGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        p.drawImage(0, 0, pagina);
    }
    const auto oscuro = [&img](int x, int y) { return qGray(img.pixel(x, y)) < 128; };

    // Caja de pixeles oscuros alrededor del QR, incluida su zona de silencio
    // (4 modulos, ~8 pt, que debe estar en blanco): mide el simbolo en el PDF.
    const QRect zona = QRectF(rect.x() - 8, rect.y() - 8, rect.width() + 16, rect.height() + 16)
                           .toRect();
    int minX = INT_MAX, minY = INT_MAX, maxX = -1, maxY = -1;
    for (int y = zona.top() * kEscala; y < zona.bottom() * kEscala; ++y) {
        for (int x = zona.left() * kEscala; x < zona.right() * kEscala; ++x) {
            if (oscuro(x, y)) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    const qreal anchoPt = (maxX - minX + 1) / qreal(kEscala);
    const qreal altoPt = (maxY - minY + 1) / qreal(kEscala);
    QVERIFY2(anchoPt >= 78.0 && altoPt >= 78.0, qPrintable(QStringLiteral("%1 x %2 pt").arg(anchoPt).arg(altoPt)));
    QVERIFY2(qAbs(anchoPt - rect.width()) < 1.0, qPrintable(QStringLiteral("%1 x %2 pt").arg(anchoPt).arg(altoPt)));

    // Cada modulo dibujado coincide con el QR de la URL esperada.
    const auto url = urlVerificacionCfdi(DatosQrCfdi{kUuid, kRfcEmisor, kRfcReceptor, QStringLiteral("14300"), kSello});
    QVERIFY(url);
    const auto qr = qrcodegen::QrCode::encodeText(url->toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int n = qr.getSize();
    const qreal modulo = rect.width() / n;
    int diferencias = 0;
    for (int fy = 0; fy < n; ++fy) {
        for (int fx = 0; fx < n; ++fx) {
            const int px = int((rect.x() + (fx + 0.5) * modulo) * kEscala);
            const int py = int((rect.y() + (fy + 0.5) * modulo) * kEscala);
            diferencias += oscuro(px, py) != qr.getModule(fx, fy) ? 1 : 0;
        }
    }
    QCOMPARE(diferencias, 0);
}

void TestCfdiPdf::pagoMuestraComplementoDePagos()
{
    OpcionesCfdi o;
    o.tipo = QStringLiteral("P");
    o.conceptos = 1;
    o.total = QStringLiteral("0");
    o.moneda = QStringLiteral("XXX");
    o.complementos = pagos20();
    const auto r = parsear(o);
    QVERIFY(r.detalle.pagos);
    DocumentoPdf doc;
    QVERIFY(doc.cargar(renderizar(r).contenido));
    const QString t = doc.todo();
    for (const QString& esperado :
         {QStringLiteral("Tipo Pago"), QStringLiteral("Complemento de pagos 2.0"), QStringLiteral("Monto total de pagos"),
          QStringLiteral("5800.00"), QStringLiteral("2026-09-20 12:00:00"), QStringLiteral("Importe pagado"),
          QStringLiteral("3480.00"), QStringLiteral("10000.00"), QStringLiteral("6520.00"),
          QStringLiteral("5A1B2C3D-0000-4000-8000-000000000010"), QStringLiteral("A-90")}) {
        QVERIFY2(t.contains(esperado), qPrintable(esperado));
    }
}

void TestCfdiPdf::nominaMuestraComplementoDeNomina()
{
    OpcionesCfdi o;
    o.tipo = QStringLiteral("N");
    o.conceptos = 1;
    o.complementos = nomina12();
    const auto r = parsear(o);
    QVERIFY(r.detalle.nomina);
    DocumentoPdf doc;
    QVERIFY(doc.cargar(renderizar(r).contenido));
    const QString t = doc.todo();
    for (const QString& esperado :
         {QStringLiteral("Tipo Nómina"), QStringLiteral("Complemento de nómina 1.2"), QStringLiteral("Total de percepciones"),
          QStringLiteral("10000.00"), QStringLiteral("Total de deducciones"), QStringLiteral("1500.25"),
          QStringLiteral("2026-09-01 a 2026-09-15"), QStringLiteral("15.000")}) {
        QVERIFY2(t.contains(esperado), qPrintable(esperado));
    }
}

void TestCfdiPdf::complementoNoRepresentadoSeNombra()
{
    OpcionesCfdi o;
    o.complementos = complementoDesconocido();
    const auto r = parsear(o);
    QCOMPARE(r.estado(), EstadoParseo::Parcial);
    DocumentoPdf doc;
    QVERIFY(doc.cargar(renderizar(r).contenido));
    const QString t = doc.todo();
    QVERIFY(t.contains(QStringLiteral("Contiene el complemento LeyendasFiscales")));
    QVERIFY(t.contains(QStringLiteral("no representado en esta visualización")));
    QVERIFY(t.contains(QStringLiteral("Aviso de lectura")));
}

void TestCfdiPdf::noSoportadoEIlegibleNoGeneranPdf()
{
    RenderizadorPdfCfdi renderer;
    const QByteArray v32 = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><cfdi:Comprobante xmlns:cfdi=\"http://www.sat.gob.mx/cfd/3\" "
        "version=\"3.2\" fecha=\"2015-01-01T00:00:00\" total=\"10.00\" tipoDeComprobante=\"ingreso\">"
        "<cfdi:Emisor rfc=\"XAXX010101000\"/><cfdi:Receptor rfc=\"XEXX010101000\"/></cfdi:Comprobante>");
    const auto r32 = parsearCfdi(v32);
    QCOMPARE(r32.estado(), EstadoParseo::VersionNoSoportada);
    auto pdf = renderer.renderizar(r32);
    QVERIFY(!pdf.esExito());
    QCOMPARE(pdf.error().tipo, ErrorRenderPdf::Tipo::NoRepresentable);

    const auto ilegible = parsearCfdi(QByteArrayLiteral("<?xml version=\"1.0\"?><!DOCTYPE x []><x/>"));
    QCOMPARE(ilegible.estado(), EstadoParseo::Ilegible);
    pdf = renderer.renderizar(ilegible);
    QVERIFY(!pdf.esExito());
    QCOMPARE(pdf.error().tipo, ErrorRenderPdf::Tipo::NoRepresentable);
}

void TestCfdiPdf::acentosYEnieDelXmlAlPdf()
{
    const QString descripcion = QStringLiteral("Bolígrafo de ejemplo, año, PEÑA, cámara, útil, Ü");
    const QString nombre = QStringLiteral("COMERCIALIZADORA PEÑA Y MÚÑIZ");
    QByteArray xml = xmlCfdi(OpcionesCfdi{});
    xml.replace("Articulo sintetico numero 1 con una descripcion larga para ocupar varias lineas en la tabla de "
                "conceptos",
                descripcion.toUtf8());
    xml.replace("EMISORA SINTETICA DE PRUEBA", nombre.toUtf8());
    const auto r = parsearCfdi(xml);
    QCOMPARE(r.detalle.conceptos.first().descripcion, descripcion);
    QCOMPARE(r.resumen.emisor.nombre, nombre);
    DocumentoPdf doc;
    QVERIFY(doc.cargar(renderizar(r).contenido));
    const QString t = doc.todo();
    QVERIFY2(t.contains(descripcion), qPrintable(t.left(1500)));
    QVERIFY(t.contains(nombre));
}

void TestCfdiPdf::cacheAciertoYMiss()
{
    QTemporaryDir tmp;
    AlmacenPdfCfdiFs almacen(tmp.filePath(QStringLiteral("cache/cfdi-pdf")), tmp.filePath(QStringLiteral("temporal")));
    QVERIFY(!almacen.buscar(clave(1)));
    const auto g = almacen.guardar(clave(1), QByteArrayLiteral("%PDF-1.4 uno"));
    QVERIFY(g.esExito());
    QVERIFY(!g.valor().enTemporal);
    QVERIFY(g.valor().rutaAbsoluta.startsWith(almacen.directorioCache() + QLatin1Char('/')));
    QCOMPARE(QFileInfo(g.valor().rutaAbsoluta).fileName(), AlmacenPdfCfdiFs::nombreArchivo(clave(1)));
    QCOMPARE(QFile::permissions(g.valor().rutaAbsoluta),
             QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser);
    QCOMPARE(QFile::permissions(almacen.directorioCache()) & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
             QFileDevice::Permissions());

    // Acierto: misma ruta y marca de uso reciente.
    fijarMtime(g.valor().rutaAbsoluta, 1000);
    const auto acierto = almacen.buscar(clave(1));
    QVERIFY(acierto);
    QCOMPARE(*acierto, g.valor().rutaAbsoluta);
    QVERIFY(QFileInfo(*acierto).lastModified().toSecsSinceEpoch() > 1000);

    // Otra version del generador u otro XML: no hay acierto.
    ClavePdfCfdi otra = clave(1);
    otra.versionGenerador = 2;
    QVERIFY(!almacen.buscar(otra));
    otra = clave(1);
    otra.sha256Xml = QString(64, QLatin1Char('f'));
    QVERIFY(!almacen.buscar(otra));

    // Borrado: no hay acierto (el servicio regenera).
    QVERIFY(QFile::remove(g.valor().rutaAbsoluta));
    QVERIFY(!almacen.buscar(clave(1)));
}

void TestCfdiPdf::cacheExpulsaLosMenosUsados()
{
    QTemporaryDir tmp;
    const QString dir = tmp.filePath(QStringLiteral("cache"));
    // Limite reducido inyectado: caben 3 archivos de 100 bytes.
    AlmacenPdfCfdiFs almacen(dir, tmp.filePath(QStringLiteral("temporal")), 350);
    const QByteArray cien(100, 'x');
    QStringList rutas;
    for (int i = 1; i <= 3; ++i) {
        rutas.append(almacen.guardar(clave(i), cien).valor().rutaAbsoluta);
        fijarMtime(rutas.last(), 1000 + i * 10);
    }
    // Un archivo ajeno nunca se toca.
    QVERIFY(QFile(QDir(dir).filePath(QStringLiteral("ajeno.pdf"))).open(QIODevice::WriteOnly));
    // Uso reciente del 1 (acierto): el menos usado pasa a ser el 2.
    QVERIFY(almacen.buscar(clave(1)));
    const auto cuarto = almacen.guardar(clave(4), cien);
    QVERIFY(cuarto.esExito());
    QVERIFY(QFile::exists(rutas.at(0)));
    QVERIFY(!QFile::exists(rutas.at(1)));
    QVERIFY(QFile::exists(rutas.at(2)));
    QVERIFY(QFile::exists(cuarto.valor().rutaAbsoluta));
    QVERIFY(QFile::exists(QDir(dir).filePath(QStringLiteral("ajeno.pdf"))));

    // Un PDF mayor que el limite se conserva (nunca se expulsa el recien escrito).
    const auto grande = almacen.guardar(clave(5), QByteArray(500, 'y'));
    QVERIFY(grande.esExito());
    QVERIFY(QFile::exists(grande.valor().rutaAbsoluta));
    QCOMPARE(QDir(dir).entryList({QStringLiteral("5A1B*.pdf")}, QDir::Files).size(), 1);
}

void TestCfdiPdf::cacheNoEscribibleUsaTemporal()
{
    QTemporaryDir tmp;
    // Cache bajo un archivo regular: no se puede crear.
    QVERIFY(QFile(tmp.filePath(QStringLiteral("archivo"))).open(QIODevice::WriteOnly));
    AlmacenPdfCfdiFs bloqueada(tmp.filePath(QStringLiteral("archivo/cfdi-pdf")), tmp.filePath(QStringLiteral("temp")));
    auto g = bloqueada.guardar(clave(1), QByteArrayLiteral("%PDF-x"));
    QVERIFY(g.esExito());
    QVERIFY(g.valor().enTemporal);
    QVERIFY(g.valor().rutaAbsoluta.startsWith(bloqueada.directorioTemporal() + QLatin1Char('/')));
    QCOMPARE(*bloqueada.buscar(clave(1)), g.valor().rutaAbsoluta);

    // Cache existente sin permiso de escritura.
    const QString soloLectura = tmp.filePath(QStringLiteral("ro"));
    QVERIFY(QDir().mkpath(soloLectura));
    QVERIFY(QFile::setPermissions(soloLectura, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    AlmacenPdfCfdiFs sinPermiso(soloLectura, tmp.filePath(QStringLiteral("temp2")));
    g = sinPermiso.guardar(clave(2), QByteArrayLiteral("%PDF-y"));
    QVERIFY(g.esExito());
    QVERIFY(g.valor().enTemporal);
    QVERIFY(QDir(soloLectura).isEmpty());

    // Ni cache ni temporal: error de escritura.
    AlmacenPdfCfdiFs nada(tmp.filePath(QStringLiteral("archivo/a")), tmp.filePath(QStringLiteral("archivo/b")));
    g = nada.guardar(clave(3), QByteArrayLiteral("%PDF-z"));
    QVERIFY(!g.esExito());
    QCOMPARE(g.error().tipo, ErrorAlmacenPdf::Tipo::Escritura);
    QFile::setPermissions(soloLectura, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

void TestCfdiPdf::copiarEsAtomicoYFallaSinTocarDestino()
{
    QTemporaryDir tmp;
    AlmacenPdfCfdiFs almacen(tmp.filePath(QStringLiteral("cache")), tmp.filePath(QStringLiteral("temp")));
    const QString origen = almacen.guardar(clave(1), QByteArrayLiteral("%PDF-contenido")).valor().rutaAbsoluta;

    // Reemplaza un destino existente (el dialogo nativo ya confirmo).
    const QString destinos = tmp.filePath(QStringLiteral("destinos"));
    QVERIFY(QDir().mkpath(destinos));
    const QString destino = QDir(destinos).filePath(QStringLiteral("CFDI-A77.pdf"));
    QVERIFY(QFile(destino).open(QIODevice::WriteOnly));
    QVERIFY(almacen.copiar(origen, destino).esExito());
    QFile leido(destino);
    QVERIFY(leido.open(QIODevice::ReadOnly));
    QCOMPARE(leido.readAll(), QByteArrayLiteral("%PDF-contenido"));
    leido.close();
    // Sin temporales residuales junto al destino.
    QCOMPARE(QDir(destinos).entryList(QDir::Files | QDir::Hidden), QStringList{QStringLiteral("CFDI-A77.pdf")});

    // Directorio destino sin permiso de escritura: falla y el destino previo no cambia.
    const QString previo = QDir(destinos).filePath(QStringLiteral("previo.pdf"));
    {
        QFile f(previo);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("previo");
    }
    QVERIFY(QFile::setPermissions(destinos, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    auto r = almacen.copiar(origen, previo);
    QFile::setPermissions(destinos, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QVERIFY(!r.esExito());
    QCOMPARE(r.error().tipo, ErrorAlmacenPdf::Tipo::Escritura);
    QFile p(previo);
    QVERIFY(p.open(QIODevice::ReadOnly));
    QCOMPARE(p.readAll(), QByteArrayLiteral("previo"));

    // Directorio inexistente, ruta relativa o directorio como destino.
    QCOMPARE(almacen.copiar(origen, tmp.filePath(QStringLiteral("no/existe/x.pdf"))).error().tipo,
             ErrorAlmacenPdf::Tipo::Escritura);
    QCOMPARE(almacen.copiar(origen, QStringLiteral("relativo.pdf")).error().tipo, ErrorAlmacenPdf::Tipo::DestinoInvalido);
    QCOMPARE(almacen.copiar(origen, destinos).error().tipo, ErrorAlmacenPdf::Tipo::DestinoInvalido);
}

void TestCfdiPdf::copiarRechazaOrigenAjeno()
{
    QTemporaryDir tmp;
    AlmacenPdfCfdiFs almacen(tmp.filePath(QStringLiteral("cache")), tmp.filePath(QStringLiteral("temp")));
    const QString propio = almacen.guardar(clave(1), QByteArrayLiteral("%PDF-a")).valor().rutaAbsoluta;
    const QString destino = tmp.filePath(QStringLiteral("salida.pdf"));

    // Fuera de la cache (aunque tenga nombre valido).
    const QString fuera = tmp.filePath(AlmacenPdfCfdiFs::nombreArchivo(clave(1)));
    QVERIFY(QFile::copy(propio, fuera));
    QCOMPARE(almacen.copiar(fuera, destino).error().tipo, ErrorAlmacenPdf::Tipo::OrigenInvalido);
    // Enlace simbolico dentro de la cache.
    const QString enlace = QDir(almacen.directorioCache()).filePath(AlmacenPdfCfdiFs::nombreArchivo(clave(2)));
    QVERIFY(QFile::link(fuera, enlace));
    QCOMPARE(almacen.copiar(enlace, destino).error().tipo, ErrorAlmacenPdf::Tipo::OrigenInvalido);
    // Componentes ".." que salen de la cache.
    QCOMPARE(almacen.copiar(almacen.directorioCache() + QStringLiteral("/../") + QFileInfo(fuera).fileName(), destino)
                 .error()
                 .tipo,
             ErrorAlmacenPdf::Tipo::OrigenInvalido);
    // Inexistente.
    QCOMPARE(almacen.copiar(QDir(almacen.directorioCache()).filePath(AlmacenPdfCfdiFs::nombreArchivo(clave(9))),
                            destino)
                 .error()
                 .tipo,
             ErrorAlmacenPdf::Tipo::OrigenInvalido);
    QVERIFY(!QFile::exists(destino));
}

void TestCfdiPdf::logsSinDatosFiscales()
{
    {
        QMutexLocker b(&g_mutexLogs);
        g_logs.clear();
    }
    QTemporaryDir tmp;
    QVERIFY(QFile(tmp.filePath(QStringLiteral("f"))).open(QIODevice::WriteOnly));
    AlmacenPdfCfdiFs almacen(tmp.filePath(QStringLiteral("f/x")), tmp.filePath(QStringLiteral("temporal")), 10);
    const auto r = parsear(OpcionesCfdi{});
    const PdfRenderizado pdf = renderizar(r);
    QVERIFY(almacen.guardar(ClavePdfCfdi{kUuid, QString(64, QLatin1Char('b')), 1}, pdf.contenido).esExito());
    QVERIFY(almacen.guardar(ClavePdfCfdi{kUuid, QString(64, QLatin1Char('c')), 1}, pdf.contenido).esExito());
    almacen.copiar(tmp.filePath(QStringLiteral("nada.pdf")), tmp.filePath(QStringLiteral("salida.pdf")));
    QMutexLocker b(&g_mutexLogs);
    QVERIFY(!g_logs.isEmpty()); // temporal, expulsion y rechazo se registran
    const QStringList prohibidos{kRfcEmisor, kRfcReceptor, kUuid, kUuid.left(8), QStringLiteral("EMISORA"),
                                 QStringLiteral("14300"), QStringLiteral("rH8/bw=="), tmp.path(),
                                 QDir::homePath()};
    for (const QString& linea : std::as_const(g_logs)) {
        for (const QString& p : prohibidos) {
            QVERIFY2(!linea.contains(p), qPrintable(linea));
        }
    }
}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    TestCfdiPdf prueba;
    return QTest::qExec(&prueba, argc, argv);
}

#include "TestCfdiPdf.moc"
