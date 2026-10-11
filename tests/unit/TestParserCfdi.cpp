#include "TestParserCfdi.h"

#include "infrastructure/cfdi/xml/ParserCfdi.h"

#include <QTest>

using namespace satcfdi;

Q_DECLARE_METATYPE(satcfdi::EstadoParseo)
Q_DECLARE_METATYPE(satcfdi::CausaIlegible)

namespace {

// Datos sinteticos: RFC genericos del SAT, nombres y UUID inventados.
const QString kUuid = QStringLiteral("A1B2C3D4-0000-4000-8000-000000000001");
const QString kUuidRelacionado = QStringLiteral("a1b2c3d4-0000-4000-8000-000000000002");
const QString kSello = QStringLiteral("U0VMTE9ERVBSVUVCQVNJTlRFVElDTw+/ZZ12345678==");

// Plantilla de CFDI 4.0/3.3 con tokens {..}; cada prueba sustituye lo que necesita.
struct Plantilla {
    QString version = QStringLiteral("4.0");
    QString tipo = QStringLiteral("I");
    QString subtotal = QStringLiteral("1000.00");
    QString descuento = QStringLiteral("50.50");
    QString total = QStringLiteral("1101.42");
    QString moneda = QStringLiteral("MXN");
    QString extraRaiz;      // atributos extra del Comprobante
    QString relacionados;   // antes de Emisor
    QString conceptos;      // vacio -> concepto por defecto
    QString impuestos;      // vacio -> traslado IVA por defecto
    QString complementos;   // despues del timbre
    QString despues;        // despues de Complemento (p. ej. Addenda)
    bool timbre = true;
};

QString conceptoPorDefecto(bool v40)
{
    return QStringLiteral(
               "<cfdi:Conceptos><cfdi:Concepto ClaveProdServ=\"01010101\" NoIdentificacion=\"SKU-1\" "
               "Cantidad=\"2.000000\" ClaveUnidad=\"H87\" Unidad=\"Pieza\" Descripcion=\"Producto de prueba\" "
               "ValorUnitario=\"500.000000\" Importe=\"1000.000000\" Descuento=\"50.50\"%1>"
               "<cfdi:Impuestos><cfdi:Traslados><cfdi:Traslado Base=\"949.50\" Impuesto=\"002\" TipoFactor=\"Tasa\" "
               "TasaOCuota=\"0.160000\" Importe=\"151.92\"/></cfdi:Traslados></cfdi:Impuestos>"
               "</cfdi:Concepto></cfdi:Conceptos>")
        .arg(v40 ? QStringLiteral(" ObjetoImp=\"02\"") : QString());
}

QByteArray construir(const Plantilla& p)
{
    const bool v40 = p.version == u"4.0";
    QString s = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<cfdi:Comprobante xmlns:cfdi=\"{NS}\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:tfd=\"http://www.sat.gob.mx/TimbreFiscalDigital\" "
        "Version=\"{VERSION}\" Serie=\"A\" Folio=\"101\" Fecha=\"2024-03-15T10:20:30\" Sello=\"{SELLO}\" "
        "FormaPago=\"03\" NoCertificado=\"00001000000000000001\" SubTotal=\"{SUBTOTAL}\" Descuento=\"{DESCUENTO}\" "
        "Moneda=\"{MONEDA}\" TipoCambio=\"1\" Total=\"{TOTAL}\" TipoDeComprobante=\"{TIPO}\" {EXPORTACION}"
        "MetodoPago=\"PUE\" LugarExpedicion=\"64000\"{EXTRA}>\n"
        "{RELACIONADOS}"
        "<cfdi:Emisor Rfc=\"XAXX010101000\" Nombre=\"EMISOR DE PRUEBA\" RegimenFiscal=\"601\"/>\n"
        "<cfdi:Receptor Rfc=\"XEXX010101000\" Nombre=\"RECEPTOR DE PRUEBA\" {RECEPTOR40}UsoCFDI=\"G03\"/>\n"
        "{CONCEPTOS}\n{IMPUESTOS}\n"
        "<cfdi:Complemento>{TIMBRE}{COMPLEMENTOS}</cfdi:Complemento>\n"
        "{DESPUES}</cfdi:Comprobante>\n");
    const QString timbre = p.timbre
        ? QStringLiteral("<tfd:TimbreFiscalDigital Version=\"1.1\" UUID=\"%1\" FechaTimbrado=\"2024-03-15T10:21:00\" "
                         "RfcProvCertif=\"XAXX010101000\" SelloCFD=\"%2\" NoCertificadoSAT=\"00001000000000000002\" "
                         "SelloSAT=\"U0VMTE9TQVQ=\"/>")
              .arg(kUuid, kSello)
        : QString();
    s.replace(QStringLiteral("{NS}"), v40 ? QStringLiteral("http://www.sat.gob.mx/cfd/4") : QStringLiteral("http://www.sat.gob.mx/cfd/3"));
    s.replace(QStringLiteral("{VERSION}"), p.version);
    s.replace(QStringLiteral("{SELLO}"), kSello);
    s.replace(QStringLiteral("{SUBTOTAL}"), p.subtotal);
    s.replace(QStringLiteral("{DESCUENTO}"), p.descuento);
    s.replace(QStringLiteral("{MONEDA}"), p.moneda);
    s.replace(QStringLiteral("{TOTAL}"), p.total);
    s.replace(QStringLiteral("{TIPO}"), p.tipo);
    s.replace(QStringLiteral("{EXPORTACION}"), v40 ? QStringLiteral("Exportacion=\"01\" ") : QString());
    s.replace(QStringLiteral("{EXTRA}"), p.extraRaiz);
    s.replace(QStringLiteral("{RELACIONADOS}"), p.relacionados);
    s.replace(QStringLiteral("{RECEPTOR40}"),
        v40 ? QStringLiteral("DomicilioFiscalReceptor=\"64000\" RegimenFiscalReceptor=\"616\" ") : QString());
    s.replace(QStringLiteral("{CONCEPTOS}"), p.conceptos.isEmpty() ? conceptoPorDefecto(v40) : p.conceptos);
    s.replace(QStringLiteral("{IMPUESTOS}"),
        p.impuestos.isEmpty()
            ? QStringLiteral("<cfdi:Impuestos TotalImpuestosTrasladados=\"151.92\"><cfdi:Traslados>"
                             "<cfdi:Traslado%1 Impuesto=\"002\" TipoFactor=\"Tasa\" TasaOCuota=\"0.160000\" "
                             "Importe=\"151.92\"/></cfdi:Traslados></cfdi:Impuestos>")
                  .arg(v40 ? QStringLiteral(" Base=\"949.50\"") : QString())
            : p.impuestos);
    s.replace(QStringLiteral("{TIMBRE}"), timbre);
    s.replace(QStringLiteral("{COMPLEMENTOS}"), p.complementos);
    s.replace(QStringLiteral("{DESPUES}"), p.despues);
    return s.toUtf8();
}

QString pagos20()
{
    return QStringLiteral(
        "<pago20:Pagos xmlns:pago20=\"http://www.sat.gob.mx/Pagos20\" Version=\"2.0\">"
        "<pago20:Totales MontoTotalPagos=\"5800.00\" TotalTrasladosBaseIVA16=\"5000.00\" TotalTrasladosImpuestoIVA16=\"800.00\"/>"
        "<pago20:Pago FechaPago=\"2024-04-01T12:00:00\" FormaDePagoP=\"03\" MonedaP=\"MXN\" TipoCambioP=\"1\" Monto=\"5800.00\">"
        "<pago20:DoctoRelacionado IdDocumento=\"a1b2c3d4-0000-4000-8000-000000000010\" Serie=\"A\" Folio=\"90\" "
        "MonedaDR=\"MXN\" EquivalenciaDR=\"1\" NumParcialidad=\"2\" ImpSaldoAnt=\"10000.00\" ImpPagado=\"3480.00\" "
        "ImpSaldoInsoluto=\"6520.00\" ObjetoImpDR=\"02\"/>"
        "<pago20:DoctoRelacionado IdDocumento=\"A1B2C3D4-0000-4000-8000-000000000011\" MonedaDR=\"MXN\" "
        "EquivalenciaDR=\"1\" NumParcialidad=\"1\" ImpSaldoAnt=\"2320.00\" ImpPagado=\"2320.00\" "
        "ImpSaldoInsoluto=\"0.00\" ObjetoImpDR=\"01\"/>"
        "<pago20:ImpuestosP><pago20:TrasladosP><pago20:TrasladoP BaseP=\"5000.00\" ImpuestoP=\"002\" "
        "TipoFactorP=\"Tasa\" TasaOCuotaP=\"0.160000\" ImporteP=\"800.00\"/></pago20:TrasladosP></pago20:ImpuestosP>"
        "</pago20:Pago></pago20:Pagos>");
}

QString pagos10()
{
    return QStringLiteral(
        "<pago10:Pagos xmlns:pago10=\"http://www.sat.gob.mx/Pagos\" Version=\"1.0\">"
        "<pago10:Pago FechaPago=\"2021-06-01T12:00:00\" FormaDePagoP=\"02\" MonedaP=\"USD\" TipoCambioP=\"20.123456\" Monto=\"100.5\">"
        "<pago10:DoctoRelacionado IdDocumento=\"A1B2C3D4-0000-4000-8000-000000000020\" MonedaDR=\"USD\" "
        "MetodoDePagoDR=\"PPD\" NumParcialidad=\"1\" ImpSaldoAnt=\"200.00\" ImpPagado=\"100.5\" ImpSaldoInsoluto=\"99.50\"/>"
        "</pago10:Pago></pago10:Pagos>");
}

QString nomina12()
{
    return QStringLiteral(
        "<nomina12:Nomina xmlns:nomina12=\"http://www.sat.gob.mx/nomina12\" Version=\"1.2\" TipoNomina=\"O\" "
        "FechaPago=\"2024-03-15\" FechaInicialPago=\"2024-03-01\" FechaFinalPago=\"2024-03-15\" NumDiasPagados=\"15.000\" "
        "TotalPercepciones=\"10000.00\" TotalDeducciones=\"1500.25\" TotalOtrosPagos=\"0.00\">"
        "<nomina12:Receptor Curp=\"XEXX010101HNEXXXA4\" TipoContrato=\"01\" TipoRegimen=\"02\" NumEmpleado=\"1\" "
        "PeriodicidadPago=\"04\" ClaveEntFed=\"NLE\"/>"
        "<nomina12:Percepciones TotalSueldos=\"10000.00\" TotalGravado=\"10000.00\" TotalExento=\"0.00\">"
        "<nomina12:Percepcion TipoPercepcion=\"001\" Clave=\"001\" Concepto=\"Sueldo\" ImporteGravado=\"10000.00\" ImporteExento=\"0.00\"/>"
        "</nomina12:Percepciones></nomina12:Nomina>");
}

// Plantilla para un tipo y version (complementos y montos coherentes).
Plantilla plantillaPara(const QString& version, const QString& tipo)
{
    Plantilla p;
    p.version = version;
    p.tipo = tipo;
    if (tipo == u"P") {
        p.subtotal = QStringLiteral("0");
        p.total = QStringLiteral("0");
        p.descuento = QStringLiteral("0");
        p.moneda = QStringLiteral("XXX");
        p.impuestos = QStringLiteral(" ");
        p.complementos = version == u"4.0" ? pagos20() : pagos10();
    } else if (tipo == u"N") {
        p.subtotal = QStringLiteral("10000.00");
        p.descuento = QStringLiteral("1500.25");
        p.total = QStringLiteral("8499.75");
        p.impuestos = QStringLiteral(" ");
        p.complementos = nomina12();
    } else if (tipo == u"T") {
        p.subtotal = QStringLiteral("0");
        p.descuento = QStringLiteral("0");
        p.total = QStringLiteral("0");
        p.impuestos = QStringLiteral(" ");
    } else if (tipo == u"E") {
        p.relacionados = QStringLiteral("<cfdi:CfdiRelacionados TipoRelacion=\"01\">"
                                        "<cfdi:CfdiRelacionado UUID=\"%1\"/></cfdi:CfdiRelacionados>")
                             .arg(kUuidRelacionado);
    }
    return p;
}

QString anidado(int niveles)
{
    QString s;
    for (int i = 0; i < niveles; ++i) {
        s += QStringLiteral("<n>");
    }
    for (int i = 0; i < niveles; ++i) {
        s += QStringLiteral("</n>");
    }
    return s;
}

} // namespace

void TestParserCfdi::versionesYTipos_data()
{
    QTest::addColumn<QString>("version");
    QTest::addColumn<QString>("tipo");
    QTest::addColumn<int>("tipoEsperado");
    const QList<std::pair<QString, TipoComprobante>> tipos = {
        {QStringLiteral("I"), TipoComprobante::Ingreso},
        {QStringLiteral("E"), TipoComprobante::Egreso},
        {QStringLiteral("P"), TipoComprobante::Pago},
        {QStringLiteral("N"), TipoComprobante::Nomina},
        {QStringLiteral("T"), TipoComprobante::Traslado},
    };
    for (const QString& version : {QStringLiteral("4.0"), QStringLiteral("3.3")}) {
        for (const auto& [codigo, tipo] : tipos) {
            QTest::newRow(qPrintable(version + u' ' + codigo)) << version << codigo << static_cast<int>(tipo);
        }
    }
}

void TestParserCfdi::versionesYTipos()
{
    QFETCH(QString, version);
    QFETCH(QString, tipo);
    QFETCH(int, tipoEsperado);
    const Plantilla p = plantillaPara(version, tipo);
    const ResultadoParseoCfdi r = parsearCfdi(construir(p));
    QVERIFY2(r.estado() == EstadoParseo::Completo, qPrintable(r.resumen.camposInvalidos.join(u',')));
    QVERIFY(!r.causaIlegible);
    QCOMPARE(r.resumen.version, version);
    QCOMPARE(static_cast<int>(r.resumen.tipo), tipoEsperado);
    QCOMPARE(r.resumen.uuid, kUuid);
    QCOMPARE(r.resumen.emisor.rfc, QStringLiteral("XAXX010101000"));
    QCOMPARE(r.resumen.receptor.rfc, QStringLiteral("XEXX010101000"));
    // Montos: exactamente el texto del XML.
    QCOMPARE(r.resumen.subtotal->texto(), p.subtotal);
    QCOMPARE(r.resumen.total->texto(), p.total);
    QCOMPARE(r.resumen.moneda, p.moneda);
    QCOMPARE(r.detalle.pagos.has_value(), tipo == u"P");
    QCOMPARE(r.detalle.nomina.has_value(), tipo == u"N");
    QCOMPARE(r.detalle.relacionados.isEmpty(), tipo != u"E");
}

void TestParserCfdi::ingreso40Completo()
{
    const ResultadoParseoCfdi r = parsearCfdi(construir(Plantilla{}));
    QCOMPARE(r.estado(), EstadoParseo::Completo);
    const ResumenCfdi& s = r.resumen;
    QCOMPARE(s.version, QStringLiteral("4.0"));
    QCOMPARE(s.tipo, TipoComprobante::Ingreso);
    QCOMPARE(s.fecha, QStringLiteral("2024-03-15T10:20:30"));
    QCOMPARE(s.serie, QStringLiteral("A"));
    QCOMPARE(s.folio, QStringLiteral("101"));
    QCOMPARE(s.emisor.nombre, QStringLiteral("EMISOR DE PRUEBA"));
    QCOMPARE(s.emisor.regimenFiscal, QStringLiteral("601"));
    QCOMPARE(s.receptor.nombre, QStringLiteral("RECEPTOR DE PRUEBA"));
    QCOMPARE(s.receptor.regimenFiscal, QStringLiteral("616"));
    QCOMPARE(s.receptor.usoCfdi, QStringLiteral("G03"));
    QCOMPARE(s.receptor.domicilioFiscal, QStringLiteral("64000"));
    QCOMPARE(s.lugarExpedicion, QStringLiteral("64000"));
    QCOMPARE(s.subtotal->texto(), QStringLiteral("1000.00"));
    QCOMPARE(s.descuento->texto(), QStringLiteral("50.50"));
    QCOMPARE(s.total->texto(), QStringLiteral("1101.42"));
    QVERIFY(*s.total == *ImporteCfdi::desdeTexto(u"1101.420"));
    QCOMPARE(s.tipoCambio->texto(), QStringLiteral("1"));
    QCOMPARE(s.metodoPago, QStringLiteral("PUE"));
    QCOMPARE(s.formaPago, QStringLiteral("03"));
    QVERIFY(s.complementosNoRepresentados.isEmpty());
    QVERIFY(s.camposInvalidos.isEmpty());

    const DetalleCfdi& d = r.detalle;
    QCOMPARE(d.exportacion, QStringLiteral("01"));
    QCOMPARE(d.noCertificado, QStringLiteral("00001000000000000001"));
    QCOMPARE(d.selloEmisor, kSello);
    QCOMPARE(d.ultimosCaracteresSello(), QStringLiteral("345678=="));
    QCOMPARE(d.conceptos.size(), 1);
    const ConceptoCfdi& c = d.conceptos.first();
    QCOMPARE(c.claveProdServ, QStringLiteral("01010101"));
    QCOMPARE(c.noIdentificacion, QStringLiteral("SKU-1"));
    QCOMPARE(c.cantidad->texto(), QStringLiteral("2.000000"));
    QCOMPARE(c.claveUnidad, QStringLiteral("H87"));
    QCOMPARE(c.unidad, QStringLiteral("Pieza"));
    QCOMPARE(c.descripcion, QStringLiteral("Producto de prueba"));
    QCOMPARE(c.valorUnitario->texto(), QStringLiteral("500.000000"));
    QCOMPARE(c.importe->texto(), QStringLiteral("1000.000000"));
    QCOMPARE(c.descuento->texto(), QStringLiteral("50.50"));
    QCOMPARE(c.objetoImp, QStringLiteral("02"));
    // Solo los impuestos del comprobante, no los del concepto.
    QCOMPARE(d.impuestos.totalTrasladados->texto(), QStringLiteral("151.92"));
    QVERIFY(!d.impuestos.totalRetenidos);
    QCOMPARE(d.impuestos.traslados.size(), 1);
    QCOMPARE(d.impuestos.traslados.first().base->texto(), QStringLiteral("949.50"));
    QCOMPARE(d.impuestos.traslados.first().impuesto, QStringLiteral("002"));
    QCOMPARE(d.impuestos.traslados.first().tipoFactor, QStringLiteral("Tasa"));
    QCOMPARE(d.impuestos.traslados.first().tasaOCuota->texto(), QStringLiteral("0.160000"));
    QCOMPARE(d.impuestos.traslados.first().importe->texto(), QStringLiteral("151.92"));
    QVERIFY(d.impuestos.retenciones.isEmpty());
    QVERIFY(d.timbre);
    QCOMPARE(d.timbre->version, QStringLiteral("1.1"));
    QCOMPARE(d.timbre->uuid, kUuid);
    QCOMPARE(d.timbre->fechaTimbrado, QStringLiteral("2024-03-15T10:21:00"));
    QCOMPARE(d.timbre->rfcProvCertif, QStringLiteral("XAXX010101000"));
    QCOMPARE(d.timbre->noCertificadoSat, QStringLiteral("00001000000000000002"));
    QCOMPARE(d.timbre->selloCfd, kSello);
    QVERIFY(!d.pagos);
    QVERIFY(!d.nomina);
    QCOMPARE(d.elementosOmitidosPorLimite, 0);
}

void TestParserCfdi::egreso33ConRelacionadosYRetenciones()
{
    Plantilla p = plantillaPara(QStringLiteral("3.3"), QStringLiteral("E"));
    p.total = QStringLiteral("1001.42");
    p.impuestos = QStringLiteral(
        "<cfdi:Impuestos TotalImpuestosRetenidos=\"100.00\" TotalImpuestosTrasladados=\"151.92\">"
        "<cfdi:Retenciones><cfdi:Retencion Impuesto=\"001\" Importe=\"100.00\"/></cfdi:Retenciones>"
        "<cfdi:Traslados><cfdi:Traslado Impuesto=\"002\" TipoFactor=\"Tasa\" TasaOCuota=\"0.160000\" Importe=\"151.92\"/></cfdi:Traslados>"
        "</cfdi:Impuestos>");
    const ResultadoParseoCfdi r = parsearCfdi(construir(p));
    QCOMPARE(r.estado(), EstadoParseo::Completo);
    QCOMPARE(r.resumen.version, QStringLiteral("3.3"));
    QCOMPARE(r.resumen.tipo, TipoComprobante::Egreso);
    QCOMPARE(r.resumen.total->texto(), QStringLiteral("1001.42"));
    QVERIFY(r.resumen.receptor.domicilioFiscal.isEmpty());
    QVERIFY(r.resumen.receptor.regimenFiscal.isEmpty());
    QCOMPARE(r.resumen.receptor.usoCfdi, QStringLiteral("G03"));
    QCOMPARE(r.detalle.relacionados.size(), 1);
    QCOMPARE(r.detalle.relacionados.first().tipoRelacion, QStringLiteral("01"));
    QCOMPARE(r.detalle.relacionados.first().uuid, kUuidRelacionado.toUpper());
    QCOMPARE(r.detalle.impuestos.totalRetenidos->texto(), QStringLiteral("100.00"));
    QCOMPARE(r.detalle.impuestos.retenciones.size(), 1);
    QCOMPARE(r.detalle.impuestos.retenciones.first().impuesto, QStringLiteral("001"));
    QCOMPARE(r.detalle.impuestos.retenciones.first().importe->texto(), QStringLiteral("100.00"));
    QCOMPARE(r.detalle.impuestos.traslados.size(), 1);
    QVERIFY(!r.detalle.impuestos.traslados.first().base);
    QVERIFY(r.detalle.conceptos.first().objetoImp.isEmpty());
}

void TestParserCfdi::pago40ConPagos20()
{
    const ResultadoParseoCfdi r = parsearCfdi(construir(plantillaPara(QStringLiteral("4.0"), QStringLiteral("P"))));
    QCOMPARE(r.estado(), EstadoParseo::Completo);
    QVERIFY(r.detalle.pagos);
    const PagosCfdi& pagos = *r.detalle.pagos;
    QCOMPARE(pagos.version, QStringLiteral("2.0"));
    QCOMPARE(pagos.montoTotalPagos->texto(), QStringLiteral("5800.00"));
    QCOMPARE(pagos.pagos.size(), 1);
    const PagoCfdi& pago = pagos.pagos.first();
    QCOMPARE(pago.fechaPago, QStringLiteral("2024-04-01T12:00:00"));
    QCOMPARE(pago.formaDePago, QStringLiteral("03"));
    QCOMPARE(pago.moneda, QStringLiteral("MXN"));
    QCOMPARE(pago.tipoCambio->texto(), QStringLiteral("1"));
    QCOMPARE(pago.monto->texto(), QStringLiteral("5800.00"));
    QCOMPARE(pago.documentos.size(), 2);
    const DocumentoRelacionadoPago& d0 = pago.documentos.at(0);
    QCOMPARE(d0.idDocumento, QStringLiteral("A1B2C3D4-0000-4000-8000-000000000010"));
    QCOMPARE(d0.serie, QStringLiteral("A"));
    QCOMPARE(d0.folio, QStringLiteral("90"));
    QCOMPARE(d0.monedaDr, QStringLiteral("MXN"));
    QCOMPARE(d0.equivalenciaDr->texto(), QStringLiteral("1"));
    QCOMPARE(d0.numParcialidad, QStringLiteral("2"));
    QCOMPARE(d0.impSaldoAnt->texto(), QStringLiteral("10000.00"));
    QCOMPARE(d0.impPagado->texto(), QStringLiteral("3480.00"));
    QCOMPARE(d0.impSaldoInsoluto->texto(), QStringLiteral("6520.00"));
    QCOMPARE(pago.documentos.at(1).impSaldoInsoluto->texto(), QStringLiteral("0.00"));
    QVERIFY(pago.documentos.at(1).impSaldoInsoluto->esCero());
    QVERIFY(r.detalle.impuestos.traslados.isEmpty()); // ImpuestosP no se confunde con el comprobante
}

void TestParserCfdi::pago33ConPagos10()
{
    const ResultadoParseoCfdi r = parsearCfdi(construir(plantillaPara(QStringLiteral("3.3"), QStringLiteral("P"))));
    QCOMPARE(r.estado(), EstadoParseo::Completo);
    QVERIFY(r.detalle.pagos);
    const PagosCfdi& pagos = *r.detalle.pagos;
    QCOMPARE(pagos.version, QStringLiteral("1.0"));
    QVERIFY(!pagos.montoTotalPagos);
    QCOMPARE(pagos.pagos.size(), 1);
    const PagoCfdi& pago = pagos.pagos.first();
    QCOMPARE(pago.moneda, QStringLiteral("USD"));
    QCOMPARE(pago.tipoCambio->texto(), QStringLiteral("20.123456"));
    QCOMPARE(pago.monto->texto(), QStringLiteral("100.5"));
    QCOMPARE(pago.documentos.size(), 1);
    const DocumentoRelacionadoPago& d = pago.documentos.first();
    QCOMPARE(d.metodoDePagoDr, QStringLiteral("PPD"));
    QVERIFY(!d.equivalenciaDr);
    QCOMPARE(d.impPagado->texto(), QStringLiteral("100.5"));
    QCOMPARE(d.impSaldoInsoluto->texto(), QStringLiteral("99.50"));
    QVERIFY(*d.impPagado == *pago.monto);
}

void TestParserCfdi::nominaBasica_data()
{
    QTest::addColumn<QString>("version");
    QTest::newRow("4.0") << QStringLiteral("4.0");
    QTest::newRow("3.3") << QStringLiteral("3.3");
}

void TestParserCfdi::nominaBasica()
{
    QFETCH(QString, version);
    const ResultadoParseoCfdi r = parsearCfdi(construir(plantillaPara(version, QStringLiteral("N"))));
    QCOMPARE(r.estado(), EstadoParseo::Completo);
    QCOMPARE(r.resumen.tipo, TipoComprobante::Nomina);
    QVERIFY(r.detalle.nomina);
    const NominaCfdi& n = *r.detalle.nomina;
    QCOMPARE(n.version, QStringLiteral("1.2"));
    QCOMPARE(n.tipoNomina, QStringLiteral("O"));
    QCOMPARE(n.fechaPago, QStringLiteral("2024-03-15"));
    QCOMPARE(n.fechaInicialPago, QStringLiteral("2024-03-01"));
    QCOMPARE(n.fechaFinalPago, QStringLiteral("2024-03-15"));
    QCOMPARE(n.numDiasPagados->texto(), QStringLiteral("15.000"));
    QCOMPARE(n.totalPercepciones->texto(), QStringLiteral("10000.00"));
    QCOMPARE(n.totalDeducciones->texto(), QStringLiteral("1500.25"));
    QCOMPARE(n.totalOtrosPagos->texto(), QStringLiteral("0.00"));
}

void TestParserCfdi::version32NoSoportadaConDatosMinimos()
{
    const QByteArray xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<cfdi:Comprobante xmlns:cfdi=\"http://www.sat.gob.mx/cfd/3\" version=\"3.2\" serie=\"B\" folio=\"7\" "
        "fecha=\"2016-05-10T09:00:00\" sello=\"U0VMTE8=\" formaDePago=\"PAGO EN UNA SOLA EXHIBICION\" "
        "noCertificado=\"00001000000000000001\" certificado=\"\" subTotal=\"100.00\" descuento=\"0.00\" "
        "total=\"116.00\" tipoDeComprobante=\"ingreso\" metodoDePago=\"01\" LugarExpedicion=\"MONTERREY\" Moneda=\"MXN\">"
        "<cfdi:Emisor rfc=\"XAXX010101000\" nombre=\"EMISOR TREINTA Y DOS\"><cfdi:RegimenFiscal Regimen=\"GENERAL\"/></cfdi:Emisor>"
        "<cfdi:Receptor rfc=\"XEXX010101000\" nombre=\"RECEPTOR TREINTA Y DOS\"/>"
        "<cfdi:Conceptos><cfdi:Concepto cantidad=\"1\" unidad=\"pieza\" descripcion=\"Producto\" valorUnitario=\"100.00\" importe=\"100.00\"/></cfdi:Conceptos>"
        "<cfdi:Impuestos totalImpuestosTrasladados=\"16.00\"><cfdi:Traslados><cfdi:Traslado impuesto=\"IVA\" tasa=\"16.00\" importe=\"16.00\"/></cfdi:Traslados></cfdi:Impuestos>"
        "<cfdi:Complemento><tfd:TimbreFiscalDigital xmlns:tfd=\"http://www.sat.gob.mx/TimbreFiscalDigital\" version=\"1.0\" "
        "UUID=\"a1b2c3d4-0000-4000-8000-000000000032\" FechaTimbrado=\"2016-05-10T09:01:00\" selloCFD=\"U0VMTE8=\" "
        "noCertificadoSAT=\"00001000000000000002\" selloSAT=\"U0FU\"/>"
        "<implocal:ImpuestosLocales xmlns:implocal=\"http://www.sat.gob.mx/implocal\" version=\"1.0\" TotaldeRetenciones=\"0\" TotaldeTraslados=\"0\"/>"
        "</cfdi:Complemento></cfdi:Comprobante>")
                               .toUtf8();
    const ResultadoParseoCfdi r = parsearCfdi(xml);
    QCOMPARE(r.estado(), EstadoParseo::VersionNoSoportada);
    QVERIFY(!r.causaIlegible);
    const ResumenCfdi& s = r.resumen;
    QCOMPARE(s.version, QStringLiteral("3.2"));
    QCOMPARE(s.tipo, TipoComprobante::Ingreso);
    QCOMPARE(s.fecha, QStringLiteral("2016-05-10T09:00:00"));
    QCOMPARE(s.serie, QStringLiteral("B"));
    QCOMPARE(s.folio, QStringLiteral("7"));
    QCOMPARE(s.emisor.rfc, QStringLiteral("XAXX010101000"));
    QCOMPARE(s.emisor.nombre, QStringLiteral("EMISOR TREINTA Y DOS"));
    QCOMPARE(s.receptor.rfc, QStringLiteral("XEXX010101000"));
    QCOMPARE(s.receptor.nombre, QStringLiteral("RECEPTOR TREINTA Y DOS"));
    QCOMPARE(s.subtotal->texto(), QStringLiteral("100.00"));
    QCOMPARE(s.total->texto(), QStringLiteral("116.00"));
    QCOMPARE(s.moneda, QStringLiteral("MXN"));
    QCOMPARE(s.metodoPago, QStringLiteral("01"));
    QCOMPARE(s.formaPago, QStringLiteral("PAGO EN UNA SOLA EXHIBICION"));
    QCOMPARE(s.uuid, QStringLiteral("A1B2C3D4-0000-4000-8000-000000000032"));
    // Solo datos minimos: sin detalle ni evaluacion de complementos.
    QVERIFY(r.detalle.conceptos.isEmpty());
    QVERIFY(!r.detalle.timbre);
    QVERIFY(s.complementosNoRepresentados.isEmpty());
}

void TestParserCfdi::complementoDesconocidoQuedaParcial()
{
    Plantilla p;
    p.complementos = QStringLiteral(
        "<implocal:ImpuestosLocales xmlns:implocal=\"http://www.sat.gob.mx/implocal\" version=\"1.0\" "
        "TotaldeRetenciones=\"0.00\" TotaldeTraslados=\"10.00\"><implocal:TrasladosLocales ImpLocTrasladado=\"ISH\" "
        "TasadeTraslado=\"3.00\" Importe=\"10.00\"/></implocal:ImpuestosLocales>"
        "<implocal:ImpuestosLocales xmlns:implocal=\"http://www.sat.gob.mx/implocal\" version=\"1.0\" "
        "TotaldeRetenciones=\"0\" TotaldeTraslados=\"0\"/>"
        "<leyendasFisc:LeyendasFiscales xmlns:leyendasFisc=\"http://www.sat.gob.mx/leyendasFiscales\" version=\"1.0\"/>");
    const ResultadoParseoCfdi r = parsearCfdi(construir(p));
    QCOMPARE(r.estado(), EstadoParseo::Parcial);
    QVERIFY(r.resumen.camposInvalidos.isEmpty());
    // Datos legibles conservados.
    QCOMPARE(r.resumen.uuid, kUuid);
    QCOMPARE(r.resumen.total->texto(), QStringLiteral("1101.42"));
    QCOMPARE(r.detalle.conceptos.size(), 1);
    // Nombrados una vez cada uno: namespace + nombre local.
    const QList<ComplementoNoRepresentado> esperados = {
        {QStringLiteral("http://www.sat.gob.mx/implocal"), QStringLiteral("ImpuestosLocales")},
        {QStringLiteral("http://www.sat.gob.mx/leyendasFiscales"), QStringLiteral("LeyendasFiscales")},
    };
    QCOMPARE(r.resumen.complementosNoRepresentados, esperados);
}

void TestParserCfdi::complementoDeConceptoDesconocidoQuedaParcial()
{
    Plantilla p;
    p.conceptos = QStringLiteral(
        "<cfdi:Conceptos><cfdi:Concepto ClaveProdServ=\"86121700\" Cantidad=\"1\" ClaveUnidad=\"E48\" "
        "Descripcion=\"Colegiatura\" ValorUnitario=\"1000.00\" Importe=\"1000.00\" ObjetoImp=\"01\">"
        "<cfdi:ComplementoConcepto><iedu:instEducativas xmlns:iedu=\"http://www.sat.gob.mx/iedu\" version=\"1.0\" "
        "nombreAlumno=\"ALUMNO DE PRUEBA\" CURP=\"XEXX010101HNEXXXA4\" nivelEducativo=\"Primaria\" "
        "autRVOE=\"1\"/></cfdi:ComplementoConcepto></cfdi:Concepto></cfdi:Conceptos>");
    const ResultadoParseoCfdi r = parsearCfdi(construir(p));
    QCOMPARE(r.estado(), EstadoParseo::Parcial);
    QCOMPARE(r.resumen.complementosNoRepresentados.size(), 1);
    QCOMPARE(r.resumen.complementosNoRepresentados.first().espacioNombres, QStringLiteral("http://www.sat.gob.mx/iedu"));
    QCOMPARE(r.resumen.complementosNoRepresentados.first().nombreLocal, QStringLiteral("instEducativas"));
}

void TestParserCfdi::addendaNoAfectaEstado()
{
    Plantilla p;
    p.despues = QStringLiteral("<cfdi:Addenda><prov:Datos xmlns:prov=\"urn:ejemplo:addenda\" Pedido=\"1\">"
                               "<prov:Linea/></prov:Datos></cfdi:Addenda>");
    const ResultadoParseoCfdi r = parsearCfdi(construir(p));
    QCOMPARE(r.estado(), EstadoParseo::Completo);
    QVERIFY(r.resumen.complementosNoRepresentados.isEmpty());
}

void TestParserCfdi::montoInvalidoQuedaParcial()
{
    Plantilla p;
    p.total = QStringLiteral("1,101.42");
    const ResultadoParseoCfdi r = parsearCfdi(construir(p));
    QCOMPARE(r.estado(), EstadoParseo::Parcial);
    QVERIFY(!r.resumen.total);
    QCOMPARE(r.resumen.camposInvalidos, QStringList{QStringLiteral("Comprobante@Total")});
    QCOMPARE(r.resumen.subtotal->texto(), QStringLiteral("1000.00"));
}

void TestParserCfdi::sinTimbreQuedaParcial()
{
    Plantilla p;
    p.timbre = false;
    const ResultadoParseoCfdi r = parsearCfdi(construir(p));
    QCOMPARE(r.estado(), EstadoParseo::Parcial);
    QVERIFY(r.resumen.uuid.isEmpty());
    QVERIFY(r.resumen.camposInvalidos.contains(QStringLiteral("TimbreFiscalDigital@UUID")));
}

void TestParserCfdi::doctypeEsIlegible_data()
{
    QTest::addColumn<QByteArray>("xml");
    const QByteArray cfdi = construir(Plantilla{});
    QByteArray conDoctype = cfdi;
    conDoctype.replace("?>\n", "?>\n<!DOCTYPE cfdi:Comprobante SYSTEM \"cfdv40.dtd\">\n");
    QTest::newRow("doctype externo") << conDoctype;

    QByteArray minusculas = cfdi;
    minusculas.replace("?>\n", "?>\n<!doctype cfdi:Comprobante>\n");
    QTest::newRow("doctype minusculas") << minusculas;

    QTest::newRow("billion laughs") << QByteArray(
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE lolz [\n"
        " <!ENTITY lol \"lol\">\n"
        " <!ENTITY lol2 \"&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;\">\n"
        " <!ENTITY lol3 \"&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;&lol2;\">\n"
        " <!ENTITY lol4 \"&lol3;&lol3;&lol3;&lol3;&lol3;&lol3;&lol3;&lol3;&lol3;&lol3;\">\n"
        " <!ENTITY lol5 \"&lol4;&lol4;&lol4;&lol4;&lol4;&lol4;&lol4;&lol4;&lol4;&lol4;\">\n"
        " <!ENTITY lol6 \"&lol5;&lol5;&lol5;&lol5;&lol5;&lol5;&lol5;&lol5;&lol5;&lol5;\">\n"
        " <!ENTITY lol7 \"&lol6;&lol6;&lol6;&lol6;&lol6;&lol6;&lol6;&lol6;&lol6;&lol6;\">\n"
        " <!ENTITY lol8 \"&lol7;&lol7;&lol7;&lol7;&lol7;&lol7;&lol7;&lol7;&lol7;&lol7;\">\n"
        " <!ENTITY lol9 \"&lol8;&lol8;&lol8;&lol8;&lol8;&lol8;&lol8;&lol8;&lol8;&lol8;\">\n"
        "]>\n"
        "<cfdi:Comprobante xmlns:cfdi=\"http://www.sat.gob.mx/cfd/4\" Version=\"4.0\" Serie=\"&lol9;\"/>\n");

    QByteArray entidadExterna = cfdi;
    entidadExterna.replace("?>\n", "?>\n<!DOCTYPE x [<!ENTITY ext SYSTEM \"file:///etc/passwd\">]>\n");
    entidadExterna.replace("Folio=\"101\"", "Folio=\"&ext;\"");
    QTest::newRow("entidad externa") << entidadExterna;
}

void TestParserCfdi::doctypeEsIlegible()
{
    QFETCH(QByteArray, xml);
    const ResultadoParseoCfdi r = parsearCfdi(xml);
    QCOMPARE(r.estado(), EstadoParseo::Ilegible);
    QCOMPARE(r.causaIlegible, std::optional<CausaIlegible>(CausaIlegible::Doctype));
    QVERIFY(r.resumen.uuid.isEmpty());
    QVERIFY(r.resumen.serie.isEmpty());
}

void TestParserCfdi::malFormadoEsIlegible_data()
{
    QTest::addColumn<QByteArray>("xml");
    const QByteArray cfdi = construir(Plantilla{});
    QTest::newRow("vacio") << QByteArray();
    QTest::newRow("solo espacios") << QByteArray("   \n");
    QTest::newRow("truncado") << cfdi.left(cfdi.size() / 2);
    QTest::newRow("sin cerrar raiz") << cfdi.left(cfdi.lastIndexOf("</cfdi:Comprobante>"));
    QByteArray cruzado = cfdi;
    cruzado.replace("</cfdi:Conceptos>", "</cfdi:Concepto>");
    QTest::newRow("etiquetas cruzadas") << cruzado;
    QByteArray entidad = cfdi;
    entidad.replace("Folio=\"101\"", "Folio=\"&noDeclarada;\"");
    QTest::newRow("entidad no declarada") << entidad;
    QTest::newRow("dos raices") << cfdi + "<cfdi:Comprobante xmlns:cfdi=\"http://www.sat.gob.mx/cfd/4\"/>";
    QByteArray prefijo = cfdi;
    prefijo.replace("<cfdi:Emisor ", "<otro:Emisor ");
    QTest::newRow("prefijo no declarado") << prefijo;
    QByteArray atributoDuplicado = cfdi;
    atributoDuplicado.replace("Folio=\"101\"", "Folio=\"101\" Folio=\"102\"");
    QTest::newRow("atributo duplicado") << atributoDuplicado;
}

void TestParserCfdi::malFormadoEsIlegible()
{
    QFETCH(QByteArray, xml);
    const ResultadoParseoCfdi r = parsearCfdi(xml);
    QCOMPARE(r.estado(), EstadoParseo::Ilegible);
    QCOMPARE(r.causaIlegible, std::optional<CausaIlegible>(CausaIlegible::MalFormado));
    // Sin datos parciales de un XML rechazado.
    QVERIFY(r.resumen.uuid.isEmpty());
    QVERIFY(r.resumen.emisor.rfc.isEmpty());
    QVERIFY(r.detalle.conceptos.isEmpty());
}

void TestParserCfdi::codificacion_data()
{
    QTest::addColumn<QByteArray>("xml");
    QTest::addColumn<EstadoParseo>("estado");
    const QByteArray cfdi = construir(Plantilla{});

    QByteArray latin1Ascii = cfdi;
    latin1Ascii.replace("encoding=\"UTF-8\"", "encoding=\"ISO-8859-1\"");
    QTest::newRow("ISO-8859-1 declarada (solo ASCII)") << latin1Ascii << EstadoParseo::Ilegible;

    QByteArray latin1 = latin1Ascii;
    latin1.replace("EMISOR DE PRUEBA", "EMISOR DE PRUEBA \xD1");
    QTest::newRow("ISO-8859-1 con bytes latinos") << latin1 << EstadoParseo::Ilegible;

    QByteArray latin1SinDeclarar = cfdi;
    latin1SinDeclarar.replace("EMISOR DE PRUEBA", "EMISOR DE PRUEBA \xD1");
    QTest::newRow("bytes no UTF-8 sin declarar") << latin1SinDeclarar << EstadoParseo::Ilegible;

    QByteArray utf16 = QByteArray("\xFF\xFE", 2);
    const QString texto = QString::fromUtf8(cfdi).replace(QStringLiteral("UTF-8"), QStringLiteral("UTF-16"));
    utf16.append(reinterpret_cast<const char*>(texto.utf16()), texto.size() * 2);
    QTest::newRow("UTF-16 con BOM") << utf16 << EstadoParseo::Ilegible;

    QByteArray sobrelargo = cfdi;
    sobrelargo.replace("EMISOR DE PRUEBA", "EMISOR \xC0\xAF");
    QTest::newRow("UTF-8 sobrelargo") << sobrelargo << EstadoParseo::Ilegible;

    QTest::newRow("BOM UTF-8") << QByteArray("\xEF\xBB\xBF") + cfdi << EstadoParseo::Completo;

    QByteArray sinDeclaracion = cfdi.mid(cfdi.indexOf('\n') + 1);
    QTest::newRow("sin declaracion") << sinDeclaracion << EstadoParseo::Completo;

    QByteArray utf8Minusculas = cfdi;
    utf8Minusculas.replace("encoding=\"UTF-8\"", "encoding=\"utf-8\"");
    QTest::newRow("utf-8 en minusculas") << utf8Minusculas << EstadoParseo::Completo;
}

void TestParserCfdi::codificacion()
{
    QFETCH(QByteArray, xml);
    QFETCH(EstadoParseo, estado);
    const ResultadoParseoCfdi r = parsearCfdi(xml);
    QCOMPARE(r.estado(), estado);
    if (estado == EstadoParseo::Ilegible) {
        QCOMPARE(r.causaIlegible, std::optional<CausaIlegible>(CausaIlegible::Codificacion));
    }
}

void TestParserCfdi::profundidad()
{
    // Raiz (1) + Addenda (2) + n niveles anidados.
    Plantilla limite;
    limite.despues = QStringLiteral("<cfdi:Addenda>") + anidado(LimitesParserCfdi::kProfundidadMaxima - 2)
                     + QStringLiteral("</cfdi:Addenda>");
    QCOMPARE(parsearCfdi(construir(limite)).estado(), EstadoParseo::Completo);

    Plantilla excede;
    excede.despues = QStringLiteral("<cfdi:Addenda>") + anidado(LimitesParserCfdi::kProfundidadMaxima - 1)
                     + QStringLiteral("</cfdi:Addenda>");
    const ResultadoParseoCfdi r = parsearCfdi(construir(excede));
    QCOMPARE(r.estado(), EstadoParseo::Ilegible);
    QCOMPARE(r.causaIlegible, std::optional<CausaIlegible>(CausaIlegible::Profundidad));

    // Muy profundo y sin cerrar: se corta en 65 sin recorrer el resto.
    QByteArray profundo("<cfdi:Comprobante xmlns:cfdi=\"http://www.sat.gob.mx/cfd/4\" Version=\"4.0\">");
    profundo += QByteArray("<n>").repeated(100'000);
    QCOMPARE(parsearCfdi(profundo).causaIlegible, std::optional<CausaIlegible>(CausaIlegible::Profundidad));
}

void TestParserCfdi::limiteDeElementos()
{
    Plantilla p;
    p.despues = QStringLiteral("<cfdi:Addenda>") + QStringLiteral("<a/>").repeated(LimitesParserCfdi::kMaxElementos)
                + QStringLiteral("</cfdi:Addenda>");
    const ResultadoParseoCfdi r = parsearCfdi(construir(p));
    QCOMPARE(r.estado(), EstadoParseo::Ilegible);
    QCOMPARE(r.causaIlegible, std::optional<CausaIlegible>(CausaIlegible::ExcedeLimites));

    QByteArray grande(LimitesParserCfdi::kMaxBytes + 1, ' ');
    QCOMPARE(parsearCfdi(grande).causaIlegible, std::optional<CausaIlegible>(CausaIlegible::ExcedeLimites));
}

void TestParserCfdi::limiteDeConceptosRecortaYQuedaParcial()
{
    Plantilla p;
    const QString concepto = QStringLiteral(
        "<cfdi:Concepto ClaveProdServ=\"01010101\" Cantidad=\"1\" ClaveUnidad=\"H87\" Descripcion=\"x\" "
        "ValorUnitario=\"1\" Importe=\"1\" ObjetoImp=\"01\"/>");
    p.conceptos = QStringLiteral("<cfdi:Conceptos>") + concepto.repeated(LimitesParserCfdi::kMaxConceptos + 3)
                  + QStringLiteral("</cfdi:Conceptos>");
    const ResultadoParseoCfdi r = parsearCfdi(construir(p));
    QCOMPARE(r.estado(), EstadoParseo::Parcial);
    QCOMPARE(r.detalle.conceptos.size(), LimitesParserCfdi::kMaxConceptos);
    QCOMPARE(r.detalle.elementosOmitidosPorLimite, 3);
    QCOMPARE(r.resumen.uuid, kUuid);
}

void TestParserCfdi::raizNoCfdi_data()
{
    QTest::addColumn<QByteArray>("xml");
    QTest::newRow("otra raiz") << QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?><factura><total>1</total></factura>");
    QTest::newRow("Comprobante sin namespace") << QByteArray("<Comprobante Version=\"4.0\"/>");
    QTest::newRow("Comprobante de otro namespace")
        << QByteArray("<cfdi:Comprobante xmlns:cfdi=\"urn:ejemplo:otro\" Version=\"4.0\"/>");
    QTest::newRow("retenciones")
        << QByteArray("<retenciones:Retenciones xmlns:retenciones=\"http://www.sat.gob.mx/esquemas/retencionpago/2\" "
                      "Version=\"2.0\"/>");
    QTest::newRow("timbre suelto")
        << QByteArray("<tfd:TimbreFiscalDigital xmlns:tfd=\"http://www.sat.gob.mx/TimbreFiscalDigital\" Version=\"1.1\"/>");
}

void TestParserCfdi::raizNoCfdi()
{
    QFETCH(QByteArray, xml);
    const ResultadoParseoCfdi r = parsearCfdi(xml);
    QCOMPARE(r.estado(), EstadoParseo::NoEsCfdi);
    QVERIFY(!r.causaIlegible);
}

namespace {
int g_mensajes = 0;
void contarMensajes(QtMsgType, const QMessageLogContext&, const QString&)
{
    ++g_mensajes;
}
} // namespace

void TestParserCfdi::noEmiteLogs()
{
    // D11: el parser no escribe logs; nada de su contenido puede filtrarse.
    QList<QByteArray> entradas;
    entradas << construir(Plantilla{}) << construir(plantillaPara(QStringLiteral("4.0"), QStringLiteral("P")))
             << construir(plantillaPara(QStringLiteral("3.3"), QStringLiteral("N")))
             << QByteArray("<?xml version=\"1.0\"?><!DOCTYPE x [<!ENTITY a \"b\">]><x>&a;</x>")
             << QByteArray("<cfdi:Comprobante xmlns:cfdi=\"http://www.sat.gob.mx/cfd/4\"") << QByteArray("\xFF\xFE<")
             << QByteArray("<otro/>");
    g_mensajes = 0;
    const QtMessageHandler anterior = qInstallMessageHandler(contarMensajes);
    for (const QByteArray& xml : std::as_const(entradas)) {
        (void)parsearCfdi(xml);
    }
    qInstallMessageHandler(anterior);
    QCOMPARE(g_mensajes, 0);
}
