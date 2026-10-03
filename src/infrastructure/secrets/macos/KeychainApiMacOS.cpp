#include "infrastructure/secrets/macos/KeychainApiMacOS.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <utility>

namespace satcfdi::secrets {

static_assert(errSecSuccess == osstatus::kSuccess);
static_assert(errSecItemNotFound == osstatus::kItemNotFound);
static_assert(errSecDuplicateItem == osstatus::kDuplicateItem);
static_assert(errSecInteractionNotAllowed == osstatus::kInteractionNotAllowed);
static_assert(errSecInteractionRequired == osstatus::kInteractionRequired);
static_assert(errSecDataNotAvailable == osstatus::kDataNotAvailable);
static_assert(errSecInDarkWake == osstatus::kInDarkWake);
static_assert(errSecAuthFailed == osstatus::kAuthFailed);
static_assert(errSecRestrictedAPI == osstatus::kRestrictedApi);
static_assert(errSecNoAccessForItem == osstatus::kNoAccessForItem);
static_assert(errSecUserCanceled == osstatus::kUserCanceled);
static_assert(errSecMissingEntitlement == osstatus::kMissingEntitlement);
static_assert(errSecParam == osstatus::kParam);
static_assert(errSecBadReq == osstatus::kBadReq);
static_assert(errSecNoSuchAttr == osstatus::kNoSuchAttr);
static_assert(errSecNoSuchClass == osstatus::kNoSuchClass);
static_assert(errSecNotAvailable == osstatus::kNotAvailable);
static_assert(errSecServiceNotAvailable == osstatus::kServiceNotAvailable);
static_assert(errSecNoSuchKeychain == osstatus::kNoSuchKeychain);
static_assert(errSecInvalidKeychain == osstatus::kInvalidKeychain);
static_assert(errSecNoDefaultKeychain == osstatus::kNoDefaultKeychain);
static_assert(errSecUnimplemented == osstatus::kUnimplemented);
static_assert(errSecReadOnly == osstatus::kReadOnly);
static_assert(errSecWrPerm == osstatus::kWrPerm);
static_assert(errSecIO == osstatus::kIo);
static_assert(errSecDiskFull == osstatus::kDiskFull);

namespace {

// Propietario de una referencia CF (+1).
template <typename T>
class Cf {
public:
    explicit Cf(T ref = nullptr) noexcept : m_ref(ref) {}
    ~Cf()
    {
        if (m_ref != nullptr) {
            CFRelease(m_ref);
        }
    }
    Cf(const Cf&) = delete;
    Cf& operator=(const Cf&) = delete;
    Cf(Cf&& otro) noexcept : m_ref(std::exchange(otro.m_ref, nullptr)) {}
    Cf& operator=(Cf&&) = delete;
    T get() const noexcept { return m_ref; }

private:
    T m_ref;
};

// Diccionario base: clase, servicio, cuenta (opcional) y atributos fijos.
Cf<CFMutableDictionaryRef> consultaBase(const QString& servicio, const QString* cuenta, bool consulta)
{
    Cf<CFMutableDictionaryRef> d(CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                           &kCFTypeDictionaryValueCallBacks));
    Cf<CFStringRef> cfServicio(servicio.toCFString());
    CFDictionarySetValue(d.get(), kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(d.get(), kSecAttrService, cfServicio.get());
    if (cuenta != nullptr) {
        Cf<CFStringRef> cfCuenta(cuenta->toCFString());
        CFDictionarySetValue(d.get(), kSecAttrAccount, cfCuenta.get());
    }
    CFDictionarySetValue(d.get(), kSecUseDataProtectionKeychain, kCFBooleanTrue);
    CFDictionarySetValue(d.get(), kSecAttrSynchronizable, kCFBooleanFalse);
    if (consulta) {
        // Sin prompts (DS6). kSecUseAuthenticationUI esta deprecado desde
        // macOS 11 a favor de LAContext.interactionNotAllowed; sigue vigente y
        // evita depender de LocalAuthentication. Solo aplica a consultas.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        CFDictionarySetValue(d.get(), kSecUseAuthenticationUI, kSecUseAuthenticationUIFail);
#pragma clang diagnostic pop
    }
    return d;
}

} // namespace

std::int32_t KeychainApiMacOS::agregar(const QString& servicio, const QString& cuenta, const BufferSecreto& dato)
{
    auto d = consultaBase(servicio, &cuenta, false);
    CFDictionarySetValue(d.get(), kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
    // Sin copia propia: CFData apunta al BufferSecreto durante la llamada.
    Cf<CFDataRef> datos(CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, dato.datos(),
                                                    static_cast<CFIndex>(dato.tamano()), kCFAllocatorNull));
    CFDictionarySetValue(d.get(), kSecValueData, datos.get());
    return SecItemAdd(d.get(), nullptr);
}

std::int32_t KeychainApiMacOS::copiar(const QString& servicio, const QString& cuenta, BufferSecreto& salida)
{
    auto d = consultaBase(servicio, &cuenta, true);
    CFDictionarySetValue(d.get(), kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(d.get(), kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef resultado = nullptr;
    const OSStatus status = SecItemCopyMatching(d.get(), &resultado);
    Cf<CFTypeRef> propietario(resultado);
    if (status != errSecSuccess) {
        return status;
    }
    if (resultado == nullptr || CFGetTypeID(resultado) != CFDataGetTypeID()) {
        return errSecDecode;
    }
    const auto datos = static_cast<CFDataRef>(resultado);
    // Best effort: la copia interna de CFData (inmutable) no puede limpiarse.
    salida = BufferSecreto::desdeBytes(CFDataGetBytePtr(datos), static_cast<std::size_t>(CFDataGetLength(datos)));
    return errSecSuccess;
}

std::int32_t KeychainApiMacOS::existe(const QString& servicio, const QString& cuenta)
{
    auto d = consultaBase(servicio, &cuenta, true);
    CFDictionarySetValue(d.get(), kSecReturnAttributes, kCFBooleanTrue);
    CFDictionarySetValue(d.get(), kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef resultado = nullptr;
    const OSStatus status = SecItemCopyMatching(d.get(), &resultado);
    Cf<CFTypeRef> propietario(resultado);
    return status;
}

std::int32_t KeychainApiMacOS::eliminar(const QString& servicio, const QString& cuenta)
{
    auto d = consultaBase(servicio, &cuenta, true);
    return SecItemDelete(d.get());
}

std::int32_t KeychainApiMacOS::listarCuentas(const QString& servicio, QStringList& cuentas)
{
    cuentas.clear();
    auto d = consultaBase(servicio, nullptr, true);
    CFDictionarySetValue(d.get(), kSecReturnAttributes, kCFBooleanTrue);
    CFDictionarySetValue(d.get(), kSecMatchLimit, kSecMatchLimitAll);
    CFTypeRef resultado = nullptr;
    const OSStatus status = SecItemCopyMatching(d.get(), &resultado);
    Cf<CFTypeRef> propietario(resultado);
    if (status == errSecItemNotFound) {
        return errSecSuccess;
    }
    if (status != errSecSuccess) {
        return status;
    }
    if (resultado == nullptr || CFGetTypeID(resultado) != CFArrayGetTypeID()) {
        return errSecDecode;
    }
    const auto lista = static_cast<CFArrayRef>(resultado);
    const CFIndex n = CFArrayGetCount(lista);
    for (CFIndex i = 0; i < n; ++i) {
        const auto atributos = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(lista, i));
        if (atributos == nullptr || CFGetTypeID(atributos) != CFDictionaryGetTypeID()) {
            return errSecDecode;
        }
        const auto cuenta = static_cast<CFStringRef>(CFDictionaryGetValue(atributos, kSecAttrAccount));
        if (cuenta == nullptr || CFGetTypeID(cuenta) != CFStringGetTypeID()) {
            // Item sin cuenta legible: no se puede enumerar con certeza.
            return errSecDecode;
        }
        cuentas.append(QString::fromCFString(cuenta));
    }
    return errSecSuccess;
}

} // namespace satcfdi::secrets
