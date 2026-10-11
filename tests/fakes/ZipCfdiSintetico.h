#pragma once

// CFDI 4.0 sinteticos (RFC genericos del SAT, nombres y UUID inventados) y
// ZIP reales generados con libzip (T015.2). Solo para pruebas que enlazan
// libzip::zip. Nada de esto contiene datos reales.

#include <zip.h>

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>

#include <utility>

namespace fakes {

struct CfdiSintetico {
    QString uuid;                                      // 8-4-4-4-12 hex
    QString tipo = QStringLiteral("I");
    QString fecha = QStringLiteral("2026-09-01T10:00:00");
    QString rfcEmisor = QStringLiteral("XAXX010101000");
    QString nombreEmisor = QStringLiteral("EMISOR SINTETICO");
    QString total = QStringLiteral("116.00");
    QString folio = QStringLiteral("1");
};

inline QString uuidSintetico(int n)
{
    return QStringLiteral("A0000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0'));
}

inline QByteArray xmlCfdi40(const CfdiSintetico& c)
{
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<cfdi:Comprobante xmlns:cfdi=\"http://www.sat.gob.mx/cfd/4\" "
               "xmlns:tfd=\"http://www.sat.gob.mx/TimbreFiscalDigital\" Version=\"4.0\" Serie=\"S\" Folio=\"%6\" "
               "Fecha=\"%3\" Sello=\"U0VMTE8=\" FormaPago=\"03\" NoCertificado=\"00001000000000000001\" "
               "SubTotal=\"100.00\" Moneda=\"MXN\" Total=\"%5\" TipoDeComprobante=\"%2\" Exportacion=\"01\" "
               "MetodoPago=\"PUE\" LugarExpedicion=\"64000\">"
               "<cfdi:Emisor Rfc=\"%4\" Nombre=\"%7\" RegimenFiscal=\"601\"/>"
               "<cfdi:Receptor Rfc=\"XEXX010101000\" Nombre=\"RECEPTOR SINTETICO\" DomicilioFiscalReceptor=\"64000\" "
               "RegimenFiscalReceptor=\"616\" UsoCFDI=\"S01\"/>"
               "<cfdi:Conceptos><cfdi:Concepto ClaveProdServ=\"01010101\" Cantidad=\"1\" ClaveUnidad=\"H87\" "
               "Descripcion=\"Concepto sintetico\" ValorUnitario=\"100.00\" Importe=\"100.00\" ObjetoImp=\"01\"/>"
               "</cfdi:Conceptos>"
               "<cfdi:Complemento><tfd:TimbreFiscalDigital Version=\"1.1\" UUID=\"%1\" "
               "FechaTimbrado=\"%3\" RfcProvCertif=\"XAXX010101000\" SelloCFD=\"U0VMTE8=\" "
               "NoCertificadoSAT=\"00001000000000000002\" SelloSAT=\"U0VMTE8=\"/></cfdi:Complemento>"
               "</cfdi:Comprobante>\n")
        .arg(c.uuid, c.tipo, c.fecha, c.rfcEmisor, c.total, c.folio, c.nombreEmisor)
        .toUtf8();
}

// ZIP real con libzip (DEFLATE) en memoria. Las entradas son (nombre,
// contenido). Devuelve los bytes del ZIP (vacio si fallo).
inline QByteArray crearZipSintetico(const QList<std::pair<QByteArray, QByteArray>>& entradas)
{
    zip_error_t error;
    zip_error_init(&error);
    zip_source_t* destino = zip_source_buffer_create(nullptr, 0, 0, &error);
    if (!destino) {
        zip_error_fini(&error);
        return {};
    }
    zip_source_keep(destino);
    zip_t* zip = zip_open_from_source(destino, ZIP_TRUNCATE, &error);
    if (!zip) {
        zip_source_free(destino);
        zip_error_fini(&error);
        return {};
    }
    for (const auto& [nombre, contenido] : entradas) {
        zip_source_t* fuente = zip_source_buffer(zip, contenido.constData(), zip_uint64_t(contenido.size()), 0);
        if (!fuente || zip_file_add(zip, nombre.constData(), fuente, ZIP_FL_ENC_UTF_8) < 0) {
            zip_source_free(fuente);
            zip_discard(zip);
            zip_source_free(destino);
            zip_error_fini(&error);
            return {};
        }
    }
    if (zip_close(zip) < 0) {
        zip_discard(zip);
        zip_source_free(destino);
        zip_error_fini(&error);
        return {};
    }
    QByteArray resultado;
    if (zip_source_open(destino) == 0) {
        zip_source_seek(destino, 0, SEEK_END);
        const zip_int64_t tamano = zip_source_tell(destino);
        zip_source_seek(destino, 0, SEEK_SET);
        if (tamano > 0) {
            resultado.resize(qsizetype(tamano));
            if (zip_source_read(destino, resultado.data(), zip_uint64_t(tamano)) != tamano) {
                resultado.clear();
            }
        }
        zip_source_close(destino);
    }
    zip_source_free(destino);
    zip_error_fini(&error);
    return resultado;
}

// `n` CFDI 4.0 sinteticos a partir de `primero` (uuid, folio y fecha distintos).
inline QByteArray zipConCfdi(int primero, int n, const QString& nombreEmisor = QStringLiteral("EMISOR SINTETICO"))
{
    QList<std::pair<QByteArray, QByteArray>> entradas;
    for (int i = 0; i < n; ++i) {
        CfdiSintetico c;
        c.uuid = uuidSintetico(primero + i);
        c.folio = QString::number(primero + i);
        c.fecha = QStringLiteral("2026-09-%1T10:00:00").arg(1 + (primero + i) % 28, 2, 10, QLatin1Char('0'));
        c.nombreEmisor = nombreEmisor;
        entradas.append({c.uuid.toUtf8() + ".xml", xmlCfdi40(c)});
    }
    return crearZipSintetico(entradas);
}

inline bool escribirArchivo(const QString& ruta, const QByteArray& datos)
{
    QFile f(ruta);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(datos) == datos.size();
}

} // namespace fakes
