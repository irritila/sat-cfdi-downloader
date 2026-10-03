#include "SecretStoreFactory.h"

#include <QDir>

#if defined(Q_OS_MACOS)
#include "infrastructure/secrets/macos/MacOSSecretStore.h"
#endif

#ifndef SATCFDI_BUNDLE_ID
#error "SATCFDI_BUNDLE_ID debe definirse desde CMake (SATCFDI_BUNDLE_IDENTIFIER)"
#endif

namespace satcfdi {

std::unique_ptr<SecretStore> crearSecretStore(const QString& directorioDatos)
{
#if defined(Q_OS_MACOS)
    return MacOSSecretStore::conKeychainDelSistema(
        QDir(directorioDatos).filePath(QStringLiteral("credentials")),
        QStringLiteral(SATCFDI_BUNDLE_ID));
#else
    Q_UNUSED(directorioDatos);
    return nullptr; // plataforma no soportada (ADR: solo macOS)
#endif
}

} // namespace satcfdi
