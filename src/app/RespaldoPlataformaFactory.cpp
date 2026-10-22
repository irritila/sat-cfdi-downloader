#include "RespaldoPlataformaFactory.h"

#if defined(Q_OS_MACOS)
#include "infrastructure/os/macos/CarpetaRespaldoMacOS.h"
#include "infrastructure/secrets/macos/AlmacenKekRespaldoMacOS.h"
#endif

#ifndef SATCFDI_BUNDLE_ID
#error "SATCFDI_BUNDLE_ID debe definirse desde CMake (SATCFDI_BUNDLE_IDENTIFIER)"
#endif

namespace satcfdi {

std::unique_ptr<respaldo::AlmacenKekRespaldo> crearAlmacenKekRespaldo(const QUuid& idInstalacion)
{
#if defined(Q_OS_MACOS)
    // QUuid nulo -> "00000000-..." que el almacen rechaza como ambito.
    return secrets::AlmacenKekRespaldoMacOS::conKeychainDelSistema(QStringLiteral(SATCFDI_BUNDLE_ID),
                                                                    idInstalacion.toString(QUuid::WithoutBraces));
#else
    Q_UNUSED(idInstalacion);
    return nullptr;
#endif
}

std::unique_ptr<respaldo::CarpetaRespaldoSistema> crearCarpetaRespaldoSistema()
{
#if defined(Q_OS_MACOS)
    return std::make_unique<macos::CarpetaRespaldoMacOS>();
#else
    return nullptr;
#endif
}

} // namespace satcfdi
