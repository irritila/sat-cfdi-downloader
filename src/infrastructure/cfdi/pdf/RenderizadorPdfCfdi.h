#pragma once

// RenderizadorPdfCfdi (T015.4, T015 D6-D9): implementacion de CfdiPdfRenderer
// con QPdfWriter/QPainter. Carta (612 x 792 pt), margenes de 40 pt, Helvetica
// 7-14 pt. Dos pasadas: la primera solo mide (paginacion y "Pagina X de N"),
// la segunda dibuja.
//
// Contenido: encabezado con tipo, version, serie y folio, fecha, lugar de
// expedicion y folio fiscal; QR de verificacion (Anexo 20) arriba a la
// derecha de la primera pagina, de kLadoQrPt pt (>= 78 pt = 2.75 cm);
// emisor; receptor; forma y metodo de pago, moneda y tipo de cambio;
// conceptos (tabla con encabezado repetido en cada pagina); impuestos
// trasladados y retenidos; totales; CFDI relacionados; Pagos y Nomina
// basicos (D6); complementos no representados por nombre; timbre (UUID,
// fecha, PAC, certificado SAT, ultimos 8 caracteres del sello del emisor);
// pie con la leyenda D9 y el numero de pagina en cada pagina.
//
// Montos: siempre el texto exacto del XML (ImporteCfdi::texto()), nunca
// double. Catalogos del SAT: solo la clave (no se incluyen descripciones
// que no esten en el repositorio), salvo tipo de comprobante e impuesto.
//
// Hilos: sin estado mutable; cada llamada crea su propio QPdfWriter. Apto
// para un hilo de trabajo (QPainter sobre QPdfWriter). Requiere una
// QGuiApplication viva (base de fuentes). Sin logs (D11).

#include "ports/cfdi/CfdiPdfRenderer.h"

#include <QLatin1StringView>
#include <QRectF>

namespace satcfdi {

class RenderizadorPdfCfdi final : public CfdiPdfRenderer {
public:
    // Incrementar al cambiar lo que se dibuja (invalida la cache, D7).
    static constexpr int kVersionGenerador = 1;

    // Geometria fija de la pagina, en puntos.
    static constexpr qreal kAnchoPaginaPt = 612.0; // Carta
    static constexpr qreal kAltoPaginaPt = 792.0;
    static constexpr qreal kMargenPt = 40.0;
    // Lado del simbolo QR (sin zona de silencio). Anexo 20: >= 2.75 cm (78 pt).
    static constexpr qreal kLadoQrPt = 90.0;

    // Leyenda fija de D9 (con la ortografia de la interfaz).
    static QString leyenda();

    // Rectangulo del simbolo QR en la primera pagina, en puntos desde la
    // esquina superior izquierda.
    static constexpr QRectF rectanguloQrPt()
    {
        return QRectF(kAnchoPaginaPt - kMargenPt - kLadoQrPt, kMargenPt, kLadoQrPt, kLadoQrPt);
    }

    int version() const override { return kVersionGenerador; }
    Resultado<PdfRenderizado, ErrorRenderPdf> renderizar(const ResultadoParseoCfdi& cfdi) override;
};

} // namespace satcfdi
