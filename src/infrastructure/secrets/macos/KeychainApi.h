#pragma once

// Costura falsable sobre Keychain (T005, DA6, DS6). Sin tipos de
// CoreFoundation ni Security en la interfaz: DTOs C++/Qt y OSStatus como
// entero. Implementacion real: KeychainApiMacOS (SecItem*). Pruebas:
// tests/secrets/FakeKeychainApi.h.
//
// Todos los items son kSecClassGenericPassword identificados por
// (servicio, cuenta). La implementacion real usa SIEMPRE: data protection
// keychain, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly, no
// sincronizable y sin UI de autenticacion (falla en lugar de mostrar prompts).
//
// Hilo: reentrante; se invoca desde el hilo del dispatcher o del ejecutor
// serial, nunca desde el hilo grafico.

#include "ports/secrets/SecretStoreTypes.h"

#include <QString>
#include <QStringList>

#include <cstdint>

namespace satcfdi::secrets {

// OSStatus de Security.framework usados por el mapeo (DS6). Los valores se
// verifican con static_assert contra SecBase.h en KeychainApiMacOS.cpp.
namespace osstatus {
inline constexpr std::int32_t kSuccess = 0;
inline constexpr std::int32_t kItemNotFound = -25300;
inline constexpr std::int32_t kDuplicateItem = -25299;
// AlmacenBloqueado
inline constexpr std::int32_t kInteractionNotAllowed = -25308;
inline constexpr std::int32_t kInteractionRequired = -25315;
inline constexpr std::int32_t kDataNotAvailable = -25316;
inline constexpr std::int32_t kInDarkWake = -25320;
// AccesoDenegado
inline constexpr std::int32_t kAuthFailed = -25293;
inline constexpr std::int32_t kRestrictedApi = -34020;
inline constexpr std::int32_t kNoAccessForItem = -25243;
// CanceladoPorUsuario
inline constexpr std::int32_t kUserCanceled = -128;
// AlmacenMalConfigurado
inline constexpr std::int32_t kMissingEntitlement = -34018;
inline constexpr std::int32_t kParam = -50;
inline constexpr std::int32_t kBadReq = -909;
inline constexpr std::int32_t kNoSuchAttr = -25303;
inline constexpr std::int32_t kNoSuchClass = -25306;
// AlmacenNoDisponible
inline constexpr std::int32_t kNotAvailable = -25291;
inline constexpr std::int32_t kServiceNotAvailable = -67585;
inline constexpr std::int32_t kNoSuchKeychain = -25294;
inline constexpr std::int32_t kInvalidKeychain = -25295;
inline constexpr std::int32_t kNoDefaultKeychain = -25307;
inline constexpr std::int32_t kUnimplemented = -4;
// FalloEscritura
inline constexpr std::int32_t kReadOnly = -25292;
inline constexpr std::int32_t kWrPerm = -61;
inline constexpr std::int32_t kIo = -36;
inline constexpr std::int32_t kDiskFull = -34;
} // namespace osstatus

class KeychainApi {
public:
    virtual ~KeychainApi() = default;

    // Agrega un item nuevo con `dato`. errSecDuplicateItem si ya existe (el
    // adaptador nunca sobrescribe: cada generacion usa una cuenta nueva).
    virtual std::int32_t agregar(const QString& servicio, const QString& cuenta, const BufferSecreto& dato) = 0;

    // Copia el dato del item en `salida` (reemplaza su contenido).
    // errSecItemNotFound si no existe.
    virtual std::int32_t copiar(const QString& servicio, const QString& cuenta, BufferSecreto& salida) = 0;

    // Comprueba existencia SIN leer el dato. errSecSuccess o errSecItemNotFound.
    virtual std::int32_t existe(const QString& servicio, const QString& cuenta) = 0;

    // Elimina el item. errSecItemNotFound si no existia.
    virtual std::int32_t eliminar(const QString& servicio, const QString& cuenta) = 0;

    // Enumera las cuentas del servicio. Sin items: errSecSuccess y lista
    // vacia (la implementacion traduce errSecItemNotFound).
    virtual std::int32_t listarCuentas(const QString& servicio, QStringList& cuentas) = 0;
};

// Mapeo de OSStatus a la categoria visible (DS6). `kSuccess` no debe
// mapearse. errSecItemNotFound -> CredencialNoEncontrada (el llamador lo
// reinterpreta segun el contexto); desconocidos -> Interno.
ErrorSecretStore::Categoria categoriaDeOSStatus(std::int32_t status);

// ErrorSecretStore con `operacion` como diagnostico y el OSStatus como codigo
// nativo (nunca servicio, cuenta ni datos).
ErrorSecretStore errorDeOSStatus(std::int32_t status, const char* operacion);

} // namespace satcfdi::secrets
