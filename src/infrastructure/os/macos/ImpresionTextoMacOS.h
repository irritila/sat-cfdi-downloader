#pragma once

#include "ports/OSIntegration.h"

#include <QString>

namespace satcfdi::macos {

// T025.6 C4: impresion nativa de un texto corto (clave de recuperacion).
// NSPrintOperation sobre un NSTextView EN MEMORIA: titulo en negrita y texto
// en fuente monoespaciada, negro sobre blanco (tambien en modo oscuro),
// ajustado al ancho de la pagina. La app no escribe archivos ni registra el
// texto; al terminar vacia el NSTextStorage y suelta la vista. El panel de
// impresion es el del sistema (app-modal, runOperation); sus destinos
// ("Guardar como PDF", cola de impresion) los decide el usuario.
// Hilo grafico. Texto vacio -> Fallido sin mostrar nada.
OSIntegration::ResultadoImpresion imprimirTextoMacOS(const QString& titulo, const QString& texto);

// Solo pruebas: arma la MISMA vista y la renderiza a PDF en un NSMutableData
// (sin panel ni archivos), mide y lo borra. Permite comprobar contenido,
// paginacion y vaciado sin impresora ni dialogo. El PDF en memoria es una sola
// pagina del alto de la vista (asi lo genera AppKit); las paginas reales del
// panel se estiman con el alto imprimible del papel (paginacion automatica).
struct InspeccionImpresion {
    bool valida = false;
    QString textoVista;     // contenido de la vista antes de vaciarla
    int paginas = 0;        // paginas estimadas al imprimir (alto vista / alto imprimible)
    qsizetype bytesPdf = 0; // tamano del PDF en memoria (ya borrado)
    bool vistaVaciada = false;
};
InspeccionImpresion inspeccionarImpresionEnMemoria(const QString& titulo, const QString& texto);

} // namespace satcfdi::macos
