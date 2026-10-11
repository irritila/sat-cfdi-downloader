#pragma once

#include "domain/cfdi/ImporteCfdi.h"
#include "domain/cfdi/TipoComprobante.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace satcfdi {

// Resultado del parseo de un XML (T015 D5, D6).
//  - Completo: CFDI 4.0 o 3.3 con todos sus datos representados.
//  - Parcial: 4.0/3.3 legible, pero con complementos no representados,
//    campos requeridos ausentes o invalidos, o listas recortadas por limite.
//  - VersionNoSoportada: CFDI 3.2 u otra version; solo datos minimos.
//  - Ilegible: XML rechazado (DOCTYPE, mal formado, codificacion, limites).
//  - NoEsCfdi: XML bien encabezado cuya raiz no es cfdi:Comprobante.
enum class EstadoParseo {
    Completo,
    Parcial,
    VersionNoSoportada,
    Ilegible,
    NoEsCfdi,
};

// Complemento (o complemento de concepto) que el parser no representa. Solo
// identifica el tipo: namespace + nombre local del elemento; nunca contenido.
struct ComplementoNoRepresentado {
    QString espacioNombres;
    QString nombreLocal;

    friend bool operator==(const ComplementoNoRepresentado&, const ComplementoNoRepresentado&) = default;
};

struct EmisorCfdi {
    QString rfc;
    QString nombre;
    QString regimenFiscal;
};

struct ReceptorCfdi {
    QString rfc;
    QString nombre;
    QString regimenFiscal;    // RegimenFiscalReceptor (4.0)
    QString usoCfdi;          // UsoCFDI (3.3/4.0)
    QString domicilioFiscal;  // DomicilioFiscalReceptor (4.0)
};

// Datos de lista del CFDI (T015 D5/D6, respuesta SAT-seguridad). Contiene
// datos fiscales: no debe ir a logs (D11).
struct ResumenCfdi {
    EstadoParseo estado = EstadoParseo::Ilegible;
    QString version;          // texto del atributo Version/version
    TipoComprobante tipo = TipoComprobante::Desconocido;
    QString fecha;            // texto ISO local del atributo Fecha (sin zona)
    QString serie;
    QString folio;
    EmisorCfdi emisor;
    ReceptorCfdi receptor;
    QString lugarExpedicion;
    std::optional<ImporteCfdi> subtotal;
    std::optional<ImporteCfdi> descuento;
    std::optional<ImporteCfdi> total;
    QString moneda;
    std::optional<ImporteCfdi> tipoCambio;
    QString metodoPago;
    QString formaPago;
    QString uuid;             // normalizado en mayusculas; vacio si falta o es invalido

    QList<ComplementoNoRepresentado> complementosNoRepresentados;
    // Campos requeridos ausentes o con valor invalido, como "Elemento@Atributo"
    // (p. ej. "Comprobante@Total"). Solo nombres del esquema, nunca valores.
    QStringList camposInvalidos;
};

} // namespace satcfdi
