#include "infrastructure/persistence/sqlite/SqliteCfdiFilas.h"

#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

#include <utility>

namespace satcfdi::sqlite {

namespace {

constexpr QStringView kTablaCfdi = u"cfdi_indexado";

std::optional<ImporteCfdi> leerImporte(const QVariant& valor)
{
    if (valor.isNull()) {
        return std::nullopt;
    }
    return ImporteCfdi::desdeTexto(valor.toString());
}

} // namespace

QString leerTexto(const QVariant& valor)
{
    return valor.isNull() ? QString() : valor.toString();
}

// Escapa comodines de LIKE (ESCAPE '\').
QString patronLike(const QString& texto)
{
    QString escapado;
    escapado.reserve(texto.size() + 2);
    escapado.append(QLatin1Char('%'));
    for (const QChar c : texto) {
        if (c == QLatin1Char('%') || c == QLatin1Char('_') || c == QLatin1Char('\\')) {
            escapado.append(QLatin1Char('\\'));
        }
        escapado.append(c);
    }
    escapado.append(QLatin1Char('%'));
    return escapado;
}

const QString kColumnasCfdi = QStringLiteral(
    "paquete_solicitud_id, posicion, estado_parseo, version, tipo_comprobante, fecha, serie, folio, "
    "rfc_emisor, nombre_emisor, regimen_emisor, rfc_receptor, nombre_receptor, regimen_receptor, uso_cfdi, "
    "domicilio_receptor, lugar_expedicion, subtotal, descuento, total, moneda, tipo_cambio, metodo_pago, "
    "forma_pago, uuid, complementos_json, campos_invalidos_json");

Resultado<CfdiListado, ErrorPersistencia> leerCfdi(const QSqlQuery& q, int desde)
{
    auto v = [&q, desde](int i) { return q.value(desde + i); };
    using R = Resultado<CfdiListado, ErrorPersistencia>;
    CfdiListado c;
    c.paqueteId = v(0).toString();
    c.posicion = v(1).toInt();
    const auto estado = estadoParseoDesdeClave(v(2).toString());
    if (!estado) {
        return R::fallo(sqlite::filaIlegible(kTablaCfdi, u"estado_parseo"));
    }
    ResumenCfdi& r = c.resumen;
    r.estado = *estado;
    r.version = leerTexto(v(3));
    r.tipo = tipoComprobanteDesdeTexto(leerTexto(v(4)));
    r.fecha = leerTexto(v(5));
    r.serie = leerTexto(v(6));
    r.folio = leerTexto(v(7));
    r.emisor.rfc = leerTexto(v(8));
    r.emisor.nombre = leerTexto(v(9));
    r.emisor.regimenFiscal = leerTexto(v(10));
    r.receptor.rfc = leerTexto(v(11));
    r.receptor.nombre = leerTexto(v(12));
    r.receptor.regimenFiscal = leerTexto(v(13));
    r.receptor.usoCfdi = leerTexto(v(14));
    r.receptor.domicilioFiscal = leerTexto(v(15));
    r.lugarExpedicion = leerTexto(v(16));
    r.subtotal = leerImporte(v(17));
    r.descuento = leerImporte(v(18));
    r.total = leerImporte(v(19));
    r.moneda = leerTexto(v(20));
    r.tipoCambio = leerImporte(v(21));
    r.metodoPago = leerTexto(v(22));
    r.formaPago = leerTexto(v(23));
    r.uuid = leerTexto(v(24));
    if (!v(25).isNull()) {
        const QJsonArray arreglo = QJsonDocument::fromJson(v(25).toString().toUtf8()).array();
        for (const QJsonValue& elemento : arreglo) {
            const QJsonObject o = elemento.toObject();
            r.complementosNoRepresentados.append(
                {o.value(QStringLiteral("ns")).toString(), o.value(QStringLiteral("nombre")).toString()});
        }
    }
    if (!v(26).isNull()) {
        const QJsonArray arreglo = QJsonDocument::fromJson(v(26).toString().toUtf8()).array();
        for (const QJsonValue& elemento : arreglo) {
            r.camposInvalidos.append(elemento.toString());
        }
    }
    return R::exito(std::move(c));
}


QString columnasCfdi(QStringView alias)
{
    if (alias.isEmpty()) {
        return kColumnasCfdi;
    }
    QStringList columnas = kColumnasCfdi.split(QStringLiteral(", "));
    for (QString& c : columnas) {
        c.prepend(alias.toString() + QLatin1Char('.'));
    }
    return columnas.join(QStringLiteral(", "));
}

} // namespace satcfdi::sqlite
