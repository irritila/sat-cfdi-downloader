#include "infrastructure/secrets/macos/KeychainApi.h"

namespace satcfdi::secrets {

ErrorSecretStore::Categoria categoriaDeOSStatus(std::int32_t status)
{
    using C = ErrorSecretStore::Categoria;
    namespace s = osstatus;
    switch (status) {
    case s::kItemNotFound:
        return C::CredencialNoEncontrada;
    case s::kInteractionNotAllowed:
    case s::kInteractionRequired:
    case s::kDataNotAvailable:
    case s::kInDarkWake:
        return C::AlmacenBloqueado;
    case s::kAuthFailed:
    case s::kRestrictedApi:
    case s::kNoAccessForItem:
        return C::AccesoDenegado;
    case s::kUserCanceled:
        return C::CanceladoPorUsuario;
    case s::kMissingEntitlement:
    case s::kParam:
    case s::kBadReq:
    case s::kNoSuchAttr:
    case s::kNoSuchClass:
        return C::AlmacenMalConfigurado;
    case s::kNotAvailable:
    case s::kServiceNotAvailable:
    case s::kNoSuchKeychain:
    case s::kInvalidKeychain:
    case s::kNoDefaultKeychain:
    case s::kUnimplemented:
        return C::AlmacenNoDisponible;
    case s::kReadOnly:
    case s::kWrPerm:
    case s::kIo:
    case s::kDiskFull:
        return C::FalloEscritura;
    default:
        return C::Interno;
    }
}

ErrorSecretStore errorDeOSStatus(std::int32_t status, const char* operacion)
{
    return ErrorSecretStore::de(categoriaDeOSStatus(status), QString::fromLatin1(operacion),
                                static_cast<int>(status));
}

} // namespace satcfdi::secrets
