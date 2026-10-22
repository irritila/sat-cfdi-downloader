// ActividadSistemaMacOS (T025.4). Objective-C++ con ARC.

#import <Foundation/Foundation.h>

#include "infrastructure/os/macos/ActividadSistemaMacOS.h"

#if !__has_feature(objc_arc)
#error "ActividadSistemaMacOS.mm requiere ARC (-fobjc-arc)"
#endif

namespace satcfdi::macos {

namespace {

class ActividadMacOS final : public OSIntegration::ActividadSistema {
public:
    explicit ActividadMacOS(OSIntegration::MotivoActividad motivo)
        : m_actividad([[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiated
                                                                     reason:textoMotivoActividad(motivo).toNSString()])
    {
    }
    ~ActividadMacOS() override
    {
        if (m_actividad != nil) {
            [[NSProcessInfo processInfo] endActivity:m_actividad];
        }
    }

private:
    id<NSObject> m_actividad = nil; // ARC la retiene
};

} // namespace

QString textoMotivoActividad(OSIntegration::MotivoActividad motivo)
{
    switch (motivo) {
    case OSIntegration::MotivoActividad::Respaldo:
        return QStringLiteral("Archivo Fiscal: respaldo en curso");
    }
    return QStringLiteral("Archivo Fiscal");
}

std::unique_ptr<OSIntegration::ActividadSistema> iniciarActividadSistemaMacOS(OSIntegration::MotivoActividad motivo)
{
    return std::make_unique<ActividadMacOS>(motivo);
}

} // namespace satcfdi::macos
