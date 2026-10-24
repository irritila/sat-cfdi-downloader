// ImpresionTextoMacOS (T025.6 C4). Objective-C++ con ARC. Ver contrato en
// ImpresionTextoMacOS.h y OSIntegration::imprimirTexto.

#import <AppKit/AppKit.h>

#include "infrastructure/os/macos/ImpresionTextoMacOS.h"

#include <cfloat>
#include <cmath>
#include <cstring>

#if !__has_feature(objc_arc)
#error "ImpresionTextoMacOS.mm requiere ARC (-fobjc-arc)"
#endif

namespace satcfdi::macos {

namespace {

NSPrintInfo* infoImpresion()
{
    NSPrintInfo* info = [[NSPrintInfo sharedPrintInfo] copy];
    if (@available(macOS 13.0, *)) {
        info.horizontalPagination = NSPrintingPaginationModeFit;
        info.verticalPagination = NSPrintingPaginationModeAutomatic;
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        info.horizontalPagination = NSFitPagination;
        info.verticalPagination = NSAutoPagination;
#pragma clang diagnostic pop
    }
    info.horizontallyCentered = NO;
    info.verticallyCentered = NO;
    return info;
}

NSTextView* crearVista(NSPrintInfo* info, const QString& titulo, const QString& texto)
{
    const NSSize papel = info.paperSize;
    const CGFloat ancho = MAX(papel.width - info.leftMargin - info.rightMargin, CGFloat(200));
    NSTextView* vista = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, ancho, 100)];
    // Impresion legible en modo oscuro: apariencia clara, negro sobre blanco.
    vista.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    vista.editable = NO;
    vista.selectable = NO;
    vista.richText = YES;
    vista.drawsBackground = NO;
    vista.horizontallyResizable = NO;
    vista.verticallyResizable = YES;
    vista.minSize = NSMakeSize(ancho, 0);
    vista.maxSize = NSMakeSize(ancho, CGFLOAT_MAX);
    vista.textContainer.widthTracksTextView = YES;

    NSMutableAttributedString* contenido = [[NSMutableAttributedString alloc] init];
    if (!titulo.isEmpty()) {
        [contenido appendAttributedString:[[NSAttributedString alloc]
                                              initWithString:[titulo.toNSString() stringByAppendingString:@"\n\n"]
                                                  attributes:@{
                                                      NSFontAttributeName : [NSFont boldSystemFontOfSize:16],
                                                      NSForegroundColorAttributeName : NSColor.blackColor,
                                                  }]];
    }
    [contenido appendAttributedString:[[NSAttributedString alloc]
                                          initWithString:texto.toNSString()
                                              attributes:@{
                                                  NSFontAttributeName :
                                                      [NSFont monospacedSystemFontOfSize:14 weight:NSFontWeightRegular],
                                                  NSForegroundColorAttributeName : NSColor.blackColor,
                                              }]];
    [vista.textStorage setAttributedString:contenido];
    [contenido deleteCharactersInRange:NSMakeRange(0, contenido.length)];
    [vista.layoutManager ensureLayoutForTextContainer:vista.textContainer];
    [vista sizeToFit];
    return vista;
}

bool vaciar(NSTextView* vista)
{
    NSTextStorage* almacen = vista.textStorage;
    [almacen deleteCharactersInRange:NSMakeRange(0, almacen.length)];
    return almacen.length == 0;
}

int paginasPdf(NSData* datos)
{
    CGDataProviderRef proveedor = CGDataProviderCreateWithCFData((__bridge CFDataRef)datos);
    if (proveedor == nullptr) {
        return 0;
    }
    CGPDFDocumentRef documento = CGPDFDocumentCreateWithProvider(proveedor);
    CGDataProviderRelease(proveedor);
    if (documento == nullptr) {
        return 0;
    }
    const int n = static_cast<int>(CGPDFDocumentGetNumberOfPages(documento));
    CGPDFDocumentRelease(documento);
    return n;
}

} // namespace

OSIntegration::ResultadoImpresion imprimirTextoMacOS(const QString& titulo, const QString& texto)
{
    using R = OSIntegration::ResultadoImpresion;
    if (NSApp == nil || texto.isEmpty()) {
        return R::Fallido;
    }
    R resultado = R::Fallido;
    @autoreleasepool {
        NSPrintInfo* info = infoImpresion();
        NSTextView* vista = crearVista(info, titulo, texto);
        NSPrintOperation* operacion = [NSPrintOperation printOperationWithView:vista printInfo:info];
        // Nombre del trabajo: el titulo (nunca el texto).
        operacion.jobTitle = titulo.isEmpty() ? @"Archivo Fiscal" : titulo.toNSString();
        operacion.showsPrintPanel = YES;
        operacion.showsProgressPanel = YES;
        const BOOL enviado = [operacion runOperation];
        resultado = enviado ? R::Enviado : R::Cancelado;
        vaciar(vista);
    }
    return resultado;
}

InspeccionImpresion inspeccionarImpresionEnMemoria(const QString& titulo, const QString& texto)
{
    InspeccionImpresion r;
    @autoreleasepool {
        NSPrintInfo* info = infoImpresion();
        NSTextView* vista = crearVista(info, titulo, texto);
        r.textoVista = QString::fromNSString(vista.textStorage.string);
        NSMutableData* pdf = [NSMutableData data];
        NSPrintOperation* operacion =
            [NSPrintOperation PDFOperationWithView:vista insideRect:vista.bounds toData:pdf printInfo:info];
        operacion.showsPrintPanel = NO;
        operacion.showsProgressPanel = NO;
        r.valida = [operacion runOperation];
        r.bytesPdf = static_cast<qsizetype>(pdf.length);
        const CGFloat altoPagina = info.paperSize.height - info.topMargin - info.bottomMargin;
        r.paginas = paginasPdf(pdf) > 0 && altoPagina > 0
            ? static_cast<int>(std::ceil(vista.frame.size.height / altoPagina))
            : 0;
        std::memset(pdf.mutableBytes, 0, pdf.length);
        r.vistaVaciada = vaciar(vista);
    }
    return r;
}

} // namespace satcfdi::macos
