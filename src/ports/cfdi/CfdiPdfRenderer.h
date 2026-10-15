#pragma once

// Puerto CfdiPdfRenderer (T015.4, T015 D6-D9/D10): genera en memoria el PDF de
// visualizacion de un CFDI ya parseado (T015.1). La implementacion vive en
// infrastructure/cfdi/pdf (QPdfWriter/QPainter, QR del Anexo 20 con qrcodegen).
//
// Hilo: un hilo de trabajo (nunca el grafico ni la cola SAT). Sin estado
// mutable compartido: cada llamada crea su propio QPdfWriter. Sin E/S de
// archivos (el almacenamiento en cache es AlmacenPdfCfdi). Sin logs con datos
// del CFDI (D11).

#include "domain/cfdi/ResultadoParseoCfdi.h"
#include "domain/common/Resultado.h"

#include <QByteArray>

namespace satcfdi {

struct PdfRenderizado {
    QByteArray contenido; // bytes del PDF (empieza con "%PDF-")
    int paginas = 0;
};

struct ErrorRenderPdf {
    enum class Tipo {
        NoRepresentable, // estado Ilegible, NoEsCfdi o VersionNoSoportada: no se genera PDF
        Fallo,           // QPdfWriter/QPainter no pudo generar el documento
    } tipo = Tipo::Fallo;
};

class CfdiPdfRenderer {
public:
    virtual ~CfdiPdfRenderer() = default;

    // Version del generador: forma parte de la clave de cache (T015 D7). Se
    // incrementa cuando cambia lo que se dibuja, para no servir PDF viejos.
    virtual int version() const = 0;

    // Solo CFDI Completo o Parcial; cualquier otro estado -> NoRepresentable.
    virtual Resultado<PdfRenderizado, ErrorRenderPdf> renderizar(const ResultadoParseoCfdi& cfdi) = 0;
};

} // namespace satcfdi
