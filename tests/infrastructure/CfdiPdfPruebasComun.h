#pragma once

// CFDI sinteticos para las pruebas del PDF (T015.4). RFC genericos del SAT,
// nombres, UUID y sellos inventados; nada de esto es un CFDI real.

#include <QByteArray>
#include <QString>

namespace pruebaspdf {

inline const QString kUuid = QStringLiteral("5A1B2C3D-0000-4000-8000-0000000000AB");
inline const QString kSello = QStringLiteral("U0VMTE9TSU5URVRJQ09ERUxFTUlTT1I=QWERTYrH8/bw==");
inline const QString kRfcEmisor = QStringLiteral("VISM990101474");
inline const QString kRfcReceptor = QStringLiteral("XAXX010101000");

struct OpcionesCfdi {
    QString version = QStringLiteral("4.0");
    QString tipo = QStringLiteral("I");
    int conceptos = 3;
    QString total = QStringLiteral("14300");
    QString complementos; // despues del timbre
    QString moneda = QStringLiteral("MXN");
};

inline QString conceptos(int n)
{
    QString s = QStringLiteral("<cfdi:Conceptos>");
    for (int i = 1; i <= n; ++i) {
        s += QStringLiteral("<cfdi:Concepto ClaveProdServ=\"01010101\" NoIdentificacion=\"SKU-%1\" Cantidad=\"%1.50\" "
                            "ClaveUnidad=\"H87\" Unidad=\"Pieza\" Descripcion=\"Articulo sintetico numero %1 con una "
                            "descripcion larga para ocupar varias lineas en la tabla de conceptos\" "
                            "ValorUnitario=\"100.000001\" Importe=\"%2\" ObjetoImp=\"02\"/>")
                 .arg(QString::number(i), QString::number(i) + QStringLiteral("000.01"));
    }
    return s + QStringLiteral("</cfdi:Conceptos>");
}

inline QByteArray xmlCfdi(const OpcionesCfdi& o)
{
    const bool v40 = o.version == u"4.0";
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<cfdi:Comprobante xmlns:cfdi=\"%1\" xmlns:tfd=\"http://www.sat.gob.mx/TimbreFiscalDigital\" "
               "Version=\"%2\" Serie=\"A/B\" Folio=\"77\" Fecha=\"2026-09-15T10:20:30\" Sello=\"%3\" FormaPago=\"03\" "
               "NoCertificado=\"00001000000000000001\" SubTotal=\"12327.59\" Descuento=\"0.00\" Moneda=\"%9\" "
               "TipoCambio=\"1\" Total=\"%4\" TipoDeComprobante=\"%5\" %6MetodoPago=\"PUE\" LugarExpedicion=\"64000\">"
               "<cfdi:CfdiRelacionados TipoRelacion=\"04\"><cfdi:CfdiRelacionado "
               "UUID=\"5A1B2C3D-0000-4000-8000-0000000000CD\"/></cfdi:CfdiRelacionados>"
               "<cfdi:Emisor Rfc=\"%10\" Nombre=\"EMISORA SINTETICA DE PRUEBA\" RegimenFiscal=\"601\"/>"
               "<cfdi:Receptor Rfc=\"%11\" Nombre=\"RECEPTOR SINTETICO\" %7UsoCFDI=\"G03\"/>"
               "%8"
               "<cfdi:Impuestos TotalImpuestosRetenidos=\"100.00\" TotalImpuestosTrasladados=\"1972.41\">"
               "<cfdi:Retenciones><cfdi:Retencion Impuesto=\"001\" Importe=\"100.00\"/></cfdi:Retenciones>"
               "<cfdi:Traslados><cfdi:Traslado Base=\"12327.59\" Impuesto=\"002\" TipoFactor=\"Tasa\" "
               "TasaOCuota=\"0.160000\" Importe=\"1972.41\"/></cfdi:Traslados></cfdi:Impuestos>"
               "<cfdi:Complemento><tfd:TimbreFiscalDigital Version=\"1.1\" UUID=\"%12\" "
               "FechaTimbrado=\"2026-09-15T10:21:00\" RfcProvCertif=\"XAXX010101000\" SelloCFD=\"%3\" "
               "NoCertificadoSAT=\"00001000000000000002\" SelloSAT=\"U0VMTE9TQVQ=\"/>%13</cfdi:Complemento>"
               "</cfdi:Comprobante>\n")
        .arg(v40 ? QStringLiteral("http://www.sat.gob.mx/cfd/4") : QStringLiteral("http://www.sat.gob.mx/cfd/3"),
             o.version, kSello, o.total, o.tipo, v40 ? QStringLiteral("Exportacion=\"01\" ") : QString(),
             v40 ? QStringLiteral("DomicilioFiscalReceptor=\"64000\" RegimenFiscalReceptor=\"616\" ") : QString(),
             conceptos(o.conceptos), o.moneda)
        .arg(kRfcEmisor, kRfcReceptor, kUuid, o.complementos)
        .toUtf8();
}

inline QString pagos20()
{
    return QStringLiteral(
        "<pago20:Pagos xmlns:pago20=\"http://www.sat.gob.mx/Pagos20\" Version=\"2.0\">"
        "<pago20:Totales MontoTotalPagos=\"5800.00\"/>"
        "<pago20:Pago FechaPago=\"2026-09-20T12:00:00\" FormaDePagoP=\"03\" MonedaP=\"MXN\" TipoCambioP=\"1\" "
        "Monto=\"5800.00\">"
        "<pago20:DoctoRelacionado IdDocumento=\"5A1B2C3D-0000-4000-8000-000000000010\" Serie=\"A\" Folio=\"90\" "
        "MonedaDR=\"MXN\" EquivalenciaDR=\"1\" NumParcialidad=\"2\" ImpSaldoAnt=\"10000.00\" ImpPagado=\"3480.00\" "
        "ImpSaldoInsoluto=\"6520.00\" ObjetoImpDR=\"02\"/>"
        "</pago20:Pago></pago20:Pagos>");
}

inline QString nomina12()
{
    return QStringLiteral(
        "<nomina12:Nomina xmlns:nomina12=\"http://www.sat.gob.mx/nomina12\" Version=\"1.2\" TipoNomina=\"O\" "
        "FechaPago=\"2026-09-15\" FechaInicialPago=\"2026-09-01\" FechaFinalPago=\"2026-09-15\" "
        "NumDiasPagados=\"15.000\" TotalPercepciones=\"10000.00\" TotalDeducciones=\"1500.25\" "
        "TotalOtrosPagos=\"0.00\"/>");
}

inline QString complementoDesconocido()
{
    return QStringLiteral("<leyendasFisc:LeyendasFiscales xmlns:leyendasFisc=\"http://www.sat.gob.mx/leyendasFiscales\" "
                          "version=\"1.0\"/>");
}

} // namespace pruebaspdf
