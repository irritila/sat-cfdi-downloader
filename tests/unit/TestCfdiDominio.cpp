#include "TestCfdiDominio.h"

#include "domain/cfdi/DetalleCfdi.h"
#include "domain/cfdi/ImporteCfdi.h"
#include "domain/cfdi/TipoComprobante.h"
#include "domain/cfdi/UuidCfdi.h"

#include <QTest>

using namespace satcfdi;

void TestCfdiDominio::importeAceptaGramaticaDecimal_data()
{
    QTest::addColumn<QString>("texto");
    QTest::addColumn<bool>("valido");
    QTest::newRow("entero") << "1000" << true;
    QTest::newRow("dos decimales") << "1000.00" << true;
    QTest::newRow("seis decimales") << "0.160000" << true;
    QTest::newRow("cero") << "0" << true;
    QTest::newRow("punto final") << "5." << true;
    QTest::newRow("punto inicial") << ".5" << true;
    QTest::newRow("espacios xsd") << " 12.50 " << true;
    QTest::newRow("mas de 18 enteros") << "1234567890123456789012345.123456" << true;
    QTest::newRow("vacio") << "" << false;
    QTest::newRow("solo punto") << "." << false;
    QTest::newRow("negativo") << "-1.00" << false;
    QTest::newRow("positivo con signo") << "+1.00" << false;
    QTest::newRow("exponente") << "1e3" << false;
    QTest::newRow("miles con coma") << "1,000.00" << false;
    QTest::newRow("dos puntos") << "1.0.0" << false;
    QTest::newRow("letras") << "abc" << false;
    QTest::newRow("espacio interno") << "1 000" << false;
    QTest::newRow("demasiados enteros") << QString(41, u'9') << false;
    QTest::newRow("demasiados decimales") << QStringLiteral("0.") + QString(41, u'1') << false;
}

void TestCfdiDominio::importeAceptaGramaticaDecimal()
{
    QFETCH(QString, texto);
    QFETCH(bool, valido);
    QCOMPARE(ImporteCfdi::desdeTexto(texto).has_value(), valido);
}

void TestCfdiDominio::importeConservaTextoOriginal()
{
    const auto importe = ImporteCfdi::desdeTexto(u"001160.500000");
    QVERIFY(importe);
    QCOMPARE(importe->texto(), QStringLiteral("001160.500000"));
    const auto conEspacios = ImporteCfdi::desdeTexto(u" 7.10 ");
    QVERIFY(conEspacios);
    QCOMPARE(conEspacios->texto(), QStringLiteral(" 7.10 "));
}

void TestCfdiDominio::importeComparaExacto_data()
{
    QTest::addColumn<QString>("a");
    QTest::addColumn<QString>("b");
    QTest::addColumn<int>("orden"); // -1 a<b, 0 iguales, 1 a>b
    QTest::newRow("ceros a la derecha") << "100.50" << "100.5" << 0;
    QTest::newRow("ceros a la izquierda") << "0010" << "10.000" << 0;
    QTest::newRow("cero") << "0.00" << "0" << 0;
    QTest::newRow("menos que double") << "0.1" << "0.10000000000000001" << -1;
    QTest::newRow("mas que double")
        << "12345678901234567890.123456" << "12345678901234567890.123457" << -1;
    QTest::newRow("mas digitos enteros") << "1000" << "999.999999" << 1;
    QTest::newRow("fraccion prefijo") << "1.5" << "1.51" << -1;
    QTest::newRow("fraccion distinta") << "1.6" << "1.59" << 1;
}

void TestCfdiDominio::importeComparaExacto()
{
    QFETCH(QString, a);
    QFETCH(QString, b);
    QFETCH(int, orden);
    const auto ia = ImporteCfdi::desdeTexto(a);
    const auto ib = ImporteCfdi::desdeTexto(b);
    QVERIFY(ia && ib);
    QCOMPARE(*ia == *ib, orden == 0);
    const auto cmp = *ia <=> *ib;
    QCOMPARE(cmp < 0, orden < 0);
    QCOMPARE(cmp > 0, orden > 0);
    const auto inverso = *ib <=> *ia;
    QCOMPARE(inverso < 0, orden > 0);
}

void TestCfdiDominio::importeNormalizado()
{
    QCOMPARE(ImporteCfdi::desdeTexto(u"0010.500")->normalizado(), QStringLiteral("10.5"));
    QCOMPARE(ImporteCfdi::desdeTexto(u"0.00")->normalizado(), QStringLiteral("0"));
    QCOMPARE(ImporteCfdi::desdeTexto(u".25")->normalizado(), QStringLiteral("0.25"));
    QVERIFY(ImporteCfdi::desdeTexto(u"000.000")->esCero());
    QVERIFY(!ImporteCfdi::desdeTexto(u"0.000001")->esCero());
}

void TestCfdiDominio::uuidCfdiValido_data()
{
    QTest::addColumn<QString>("texto");
    QTest::addColumn<bool>("valido");
    QTest::newRow("mayusculas") << "A1B2C3D4-0000-4000-8000-0000000000AB" << true;
    QTest::newRow("minusculas") << "a1b2c3d4-0000-4000-8000-0000000000ab" << true;
    QTest::newRow("mixto") << "a1B2c3D4-0000-4000-8000-0000000000aB" << true;
    QTest::newRow("sin guiones") << "A1B2C3D40000400080000000000000AB" << false;
    QTest::newRow("guion movido") << "A1B2C3D-40000-4000-8000-0000000000AB" << false;
    QTest::newRow("no hex") << "G1B2C3D4-0000-4000-8000-0000000000AB" << false;
    QTest::newRow("llaves") << "{A1B2C3D4-0000-4000-8000-0000000000AB}" << false;
    QTest::newRow("espacio") << " A1B2C3D4-0000-4000-8000-0000000000A" << false;
    QTest::newRow("vacio") << "" << false;
}

void TestCfdiDominio::uuidCfdiValido()
{
    QFETCH(QString, texto);
    QFETCH(bool, valido);
    QCOMPARE(cfdi::esUuidValido(texto), valido);
    QCOMPARE(cfdi::normalizarUuid(texto).has_value(), valido);
}

void TestCfdiDominio::uuidCfdiSeNormalizaEnMayusculas()
{
    QCOMPARE(cfdi::normalizarUuid(u"a1b2c3d4-0000-4000-8000-0000000000ab").value(),
        QStringLiteral("A1B2C3D4-0000-4000-8000-0000000000AB"));
}

void TestCfdiDominio::tipoComprobanteDesdeTexto()
{
    QCOMPARE(satcfdi::tipoComprobanteDesdeTexto(u"I"), TipoComprobante::Ingreso);
    QCOMPARE(satcfdi::tipoComprobanteDesdeTexto(u"E"), TipoComprobante::Egreso);
    QCOMPARE(satcfdi::tipoComprobanteDesdeTexto(u"T"), TipoComprobante::Traslado);
    QCOMPARE(satcfdi::tipoComprobanteDesdeTexto(u"N"), TipoComprobante::Nomina);
    QCOMPARE(satcfdi::tipoComprobanteDesdeTexto(u"P"), TipoComprobante::Pago);
    QCOMPARE(satcfdi::tipoComprobanteDesdeTexto(u"ingreso"), TipoComprobante::Ingreso);
    QCOMPARE(satcfdi::tipoComprobanteDesdeTexto(u"egreso"), TipoComprobante::Egreso);
    QCOMPARE(satcfdi::tipoComprobanteDesdeTexto(u"traslado"), TipoComprobante::Traslado);
    QCOMPARE(satcfdi::tipoComprobanteDesdeTexto(u"i"), TipoComprobante::Desconocido);
    QCOMPARE(satcfdi::tipoComprobanteDesdeTexto(u"X"), TipoComprobante::Desconocido);
    QCOMPARE(satcfdi::tipoComprobanteDesdeTexto(u""), TipoComprobante::Desconocido);
    QCOMPARE(codigoTipoComprobante(TipoComprobante::Pago), QLatin1StringView("P"));
    QCOMPARE(codigoTipoComprobante(TipoComprobante::Desconocido), QLatin1StringView(""));
}

void TestCfdiDominio::ultimosCaracteresDelSello()
{
    DetalleCfdi d;
    d.selloEmisor = QStringLiteral("QUJDREVGR0hJSktMTU5PUFFSU1RVVldY+/ab12345678==");
    QCOMPARE(d.ultimosCaracteresSello(), QStringLiteral("345678=="));
    d.selloEmisor = QStringLiteral("abc");
    QCOMPARE(d.ultimosCaracteresSello(), QStringLiteral("abc"));
}
