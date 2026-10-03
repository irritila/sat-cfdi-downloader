#pragma once

#include "infrastructure/secrets/macos/KeychainApi.h"

namespace satcfdi::secrets {

// KeychainApi real sobre SecItem* (Security.framework), kSecClassGenericPassword.
// Atributos fijos (T005): kSecUseDataProtectionKeychain = true,
// kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly, kSecAttrSynchronizable =
// false y, en consultas/borrados, kSecUseAuthenticationUI = Fail (nunca
// prompts; un almacen bloqueado devuelve errSecInteractionNotAllowed).
//
// Requisito de distribucion: el data protection keychain exige un ejecutable
// firmado con entitlements (application-identifier / keychain-access-groups);
// sin ellos SecItem* devuelve errSecMissingEntitlement (AlmacenMalConfigurado).
class KeychainApiMacOS final : public KeychainApi {
public:
    std::int32_t agregar(const QString& servicio, const QString& cuenta, const BufferSecreto& dato) override;
    std::int32_t copiar(const QString& servicio, const QString& cuenta, BufferSecreto& salida) override;
    std::int32_t existe(const QString& servicio, const QString& cuenta) override;
    std::int32_t eliminar(const QString& servicio, const QString& cuenta) override;
    std::int32_t listarCuentas(const QString& servicio, QStringList& cuentas) override;
};

} // namespace satcfdi::secrets
