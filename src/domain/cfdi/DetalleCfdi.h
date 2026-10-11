#pragma once

#include "domain/cfdi/ImporteCfdi.h"

#include <QList>
#include <QString>

#include <optional>

namespace satcfdi {

struct ConceptoCfdi {
    QString claveProdServ;
    QString noIdentificacion;
    std::optional<ImporteCfdi> cantidad;
    QString claveUnidad;
    QString unidad;
    QString descripcion;
    std::optional<ImporteCfdi> valorUnitario;
    std::optional<ImporteCfdi> importe;
    std::optional<ImporteCfdi> descuento;
    QString objetoImp; // 4.0
};

// Traslado a nivel comprobante (cfdi:Impuestos/cfdi:Traslados/cfdi:Traslado).
struct TrasladoCfdi {
    std::optional<ImporteCfdi> base; // 4.0
    QString impuesto;                // c_Impuesto: 001 ISR, 002 IVA, 003 IEPS
    QString tipoFactor;              // Tasa, Cuota, Exento
    std::optional<ImporteCfdi> tasaOCuota;
    std::optional<ImporteCfdi> importe;
};

struct RetencionCfdi {
    QString impuesto;
    std::optional<ImporteCfdi> importe;
};

struct ImpuestosCfdi {
    std::optional<ImporteCfdi> totalTrasladados;
    std::optional<ImporteCfdi> totalRetenidos;
    QList<TrasladoCfdi> traslados;
    QList<RetencionCfdi> retenciones;
};

// Un UUID relacionado con su tipo de relacion (4.0 admite varios grupos).
struct RelacionCfdi {
    QString tipoRelacion;
    QString uuid; // normalizado en mayusculas
};

// TimbreFiscalDigital 1.1 (y 1.0 en CFDI 3.2).
struct TimbreFiscalCfdi {
    QString version;
    QString uuid; // normalizado en mayusculas; vacio si invalido
    QString fechaTimbrado;
    QString rfcProvCertif; // 1.1
    QString noCertificadoSat;
    QString selloCfd;
};

struct DocumentoRelacionadoPago {
    QString idDocumento;
    QString serie;
    QString folio;
    QString monedaDr;
    std::optional<ImporteCfdi> equivalenciaDr; // Pagos 2.0
    std::optional<ImporteCfdi> tipoCambioDr;   // Pagos 1.0
    QString metodoDePagoDr;                    // Pagos 1.0
    QString numParcialidad;
    std::optional<ImporteCfdi> impSaldoAnt;
    std::optional<ImporteCfdi> impPagado;
    std::optional<ImporteCfdi> impSaldoInsoluto;
};

struct PagoCfdi {
    QString fechaPago;
    QString formaDePago;
    QString moneda;
    std::optional<ImporteCfdi> tipoCambio;
    std::optional<ImporteCfdi> monto;
    QList<DocumentoRelacionadoPago> documentos;
};

// Complemento de recepcion de pagos 2.0 (o 1.0 en CFDI 3.3), basico (D6).
struct PagosCfdi {
    QString version; // "2.0" o "1.0"
    std::optional<ImporteCfdi> montoTotalPagos; // Totales, solo 2.0
    QList<PagoCfdi> pagos;
};

// Complemento de nomina 1.2 basico (D6). Solo el primero si hubiera varios.
struct NominaCfdi {
    QString version;
    QString tipoNomina;
    QString fechaPago;
    QString fechaInicialPago;
    QString fechaFinalPago;
    std::optional<ImporteCfdi> numDiasPagados;
    std::optional<ImporteCfdi> totalPercepciones;
    std::optional<ImporteCfdi> totalDeducciones;
    std::optional<ImporteCfdi> totalOtrosPagos;
};

// Detalle completo del CFDI para la vista y el PDF (T015.4). Contiene datos
// fiscales y el sello del emisor: no debe ir a logs (D11).
struct DetalleCfdi {
    QString noCertificado;
    QString exportacion; // 4.0
    // Atributo Sello del comprobante; T015.4 usa sus ultimos 8 caracteres en el QR.
    QString selloEmisor;
    QList<ConceptoCfdi> conceptos;
    ImpuestosCfdi impuestos;
    QList<RelacionCfdi> relacionados;
    std::optional<TimbreFiscalCfdi> timbre;
    std::optional<PagosCfdi> pagos;
    std::optional<NominaCfdi> nomina;
    // Elementos (conceptos, pagos, documentos, relaciones, impuestos) no
    // guardados por exceder los limites del parser. > 0 implica Parcial.
    int elementosOmitidosPorLimite = 0;

    // Ultimos `n` caracteres del sello del emisor (todo si es mas corto).
    QString ultimosCaracteresSello(qsizetype n = 8) const
    {
        return n >= selloEmisor.size() ? selloEmisor : selloEmisor.right(n);
    }
};

} // namespace satcfdi
