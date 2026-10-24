// PortapapelesSecretoMacOS (T025.6 H1/M1). Objective-C++ con ARC.

#import <AppKit/AppKit.h>

#include "infrastructure/os/macos/PortapapelesSecretoMacOS.h"

#include "infrastructure/os/macos/ActividadSistemaMacOS.h"

#include <utility>

#if !__has_feature(objc_arc)
#error "PortapapelesSecretoMacOS.mm requiere ARC (-fobjc-arc)"
#endif

namespace satcfdi::macos {

namespace {

NSString* const kTipoOculto = @"org.nspasteboard.ConcealedType";
NSString* const kTipoTransitorio = @"org.nspasteboard.TransientType";

NSPasteboard* pasteboard(const QString& nombre)
{
    return nombre.isEmpty() ? [NSPasteboard generalPasteboard] : [NSPasteboard pasteboardWithName:nombre.toNSString()];
}

} // namespace

PortapapelesSecretoMacOS::PortapapelesSecretoMacOS(QString nombrePasteboard, std::chrono::milliseconds borradoTras,
                                                   QObject* parent)
    : QObject(parent)
    , m_nombre(std::move(nombrePasteboard))
{
    m_temporizador.setSingleShot(true);
    m_temporizador.setInterval(borradoTras);
    connect(&m_temporizador, &QTimer::timeout, this, [this] { borrarSiSigueSiendoNuestro(); });
}

PortapapelesSecretoMacOS::~PortapapelesSecretoMacOS() = default;

bool PortapapelesSecretoMacOS::copiar(const QString& texto)
{
    if (texto.isEmpty()) {
        return false;
    }
    @autoreleasepool {
        NSPasteboard* pb = pasteboard(m_nombre);
        // Vacia el portapapeles y lo marca solo para este equipo.
        [pb prepareForNewContentsWithOptions:NSPasteboardContentsCurrentHostOnly];
        NSPasteboardItem* item = [[NSPasteboardItem alloc] init];
        BOOL ok = [item setString:texto.toNSString() forType:NSPasteboardTypeString];
        ok = ok && [item setData:[NSData data] forType:kTipoOculto];
        ok = ok && [item setData:[NSData data] forType:kTipoTransitorio];
        ok = ok && [pb writeObjects:@[ item ]];
        if (!ok) {
            m_pendiente = false;
            m_temporizador.stop();
            m_actividad.reset();
            return false;
        }
        m_changeCount = static_cast<long>(pb.changeCount);
        m_pendiente = true;
        // B6: una sola actividad por borrado pendiente (la anterior termina).
        m_actividad = iniciarActividadSinBloquearReposoMacOS();
        m_temporizador.start();
        return true;
    }
}

bool PortapapelesSecretoMacOS::borrarSiSigueSiendoNuestro()
{
    m_temporizador.stop();
    m_actividad.reset();
    if (!m_pendiente) {
        return false;
    }
    m_pendiente = false;
    @autoreleasepool {
        NSPasteboard* pb = pasteboard(m_nombre);
        if (static_cast<long>(pb.changeCount) != m_changeCount) {
            return false; // el usuario copio otra cosa: no se toca
        }
        [pb clearContents];
        return true;
    }
}

} // namespace satcfdi::macos
