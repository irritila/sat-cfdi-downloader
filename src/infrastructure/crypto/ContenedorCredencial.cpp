#include "infrastructure/crypto/ContenedorCredencial.h"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <climits>
#include <optional>
#include <cstring>
#include <memory>

namespace satcfdi::crypto {

namespace {

using Categoria = ErrorSecretStore::Categoria;

constexpr std::uint8_t kMagic[4] = {'S', 'C', 'C', '1'};
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kAlgAes256Gcm = 1;
constexpr std::size_t kLargoUuid = 36;

struct LiberarCtx {
    void operator()(EVP_CIPHER_CTX* p) const noexcept { EVP_CIPHER_CTX_free(p); }
};
using CtxPtr = std::unique_ptr<EVP_CIPHER_CTX, LiberarCtx>;

ErrorSecretStore fallo(Categoria categoria, const char* diagnostico)
{
    const unsigned long codigo = ERR_peek_last_error();
    ERR_clear_error();
    std::optional<int> nativo;
    if (codigo != 0) {
        nativo = ERR_GET_REASON(codigo);
    }
    return ErrorSecretStore::de(categoria, QString::fromLatin1(diagnostico), nativo);
}

void escribirU32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

std::uint32_t leerU32(const std::uint8_t* p)
{
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8)
        | std::uint32_t{p[3]};
}

// AAD canonica: magic | version | alg | uuid (ASCII). nullopt si el uuid no
// es ASCII de 36 caracteres.
std::optional<std::array<std::uint8_t, 6 + kLargoUuid>> aad(QStringView uuid)
{
    if (uuid.size() != static_cast<qsizetype>(kLargoUuid)) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 6 + kLargoUuid> a{};
    std::memcpy(a.data(), kMagic, 4);
    a[4] = kVersion;
    a[5] = kAlgAes256Gcm;
    for (std::size_t i = 0; i < kLargoUuid; ++i) {
        const char16_t c = uuid.at(static_cast<qsizetype>(i)).unicode();
        if (c > 0x7F) {
            return std::nullopt;
        }
        a[6 + i] = static_cast<std::uint8_t>(c);
    }
    return a;
}

} // namespace

Resultado<BufferSecreto, ErrorSecretStore> generarClaveContenedor()
{
    using R = Resultado<BufferSecreto, ErrorSecretStore>;
    BufferSecreto clave(kTamanoClaveContenedor);
    if (RAND_bytes(clave.datos(), static_cast<int>(clave.tamano())) != 1) {
        return R::fallo(fallo(Categoria::Interno, "rand"));
    }
    return R::exito(std::move(clave));
}

Resultado<QByteArray, ErrorSecretStore> bytesAleatorios(std::size_t n)
{
    using R = Resultado<QByteArray, ErrorSecretStore>;
    QByteArray bytes(static_cast<qsizetype>(n), '\0');
    if (n > 0
        && RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), static_cast<int>(n)) != 1) {
        return R::fallo(fallo(Categoria::Interno, "rand"));
    }
    return R::exito(std::move(bytes));
}

Resultado<BufferSecreto, ErrorSecretStore> empaquetarCarga(const BufferSecreto& certificadoDer,
                                                           const BufferSecreto& llaveCifradaDer)
{
    using R = Resultado<BufferSecreto, ErrorSecretStore>;
    const std::size_t total = 8 + certificadoDer.tamano() + llaveCifradaDer.tamano();
    if (certificadoDer.vacio() || llaveCifradaDer.vacio() || total > kMaxCargaUtil) {
        return R::fallo(ErrorSecretStore::de(Categoria::FormatoInvalido, QStringLiteral("carga.tamano")));
    }
    BufferSecreto carga(total);
    std::uint8_t* p = carga.datos();
    escribirU32(p, static_cast<std::uint32_t>(certificadoDer.tamano()));
    std::memcpy(p + 4, certificadoDer.datos(), certificadoDer.tamano());
    p += 4 + certificadoDer.tamano();
    escribirU32(p, static_cast<std::uint32_t>(llaveCifradaDer.tamano()));
    std::memcpy(p + 4, llaveCifradaDer.datos(), llaveCifradaDer.tamano());
    return R::exito(std::move(carga));
}

Resultado<CargaContenedor, ErrorSecretStore> desempaquetarCarga(const BufferSecreto& carga)
{
    using R = Resultado<CargaContenedor, ErrorSecretStore>;
    const auto danada = [] {
        return R::fallo(ErrorSecretStore::de(Categoria::CredencialDanada, QStringLiteral("carga.formato")));
    };
    const std::size_t total = carga.tamano();
    if (total < 8) {
        return danada();
    }
    const std::uint8_t* p = carga.datos();
    const std::size_t largoCert = leerU32(p);
    if (largoCert == 0 || largoCert > total - 8) {
        return danada();
    }
    const std::size_t largoLlave = leerU32(p + 4 + largoCert);
    if (largoLlave == 0 || 8 + largoCert + largoLlave != total) {
        return danada();
    }
    CargaContenedor resultado;
    resultado.certificadoDer = BufferSecreto::desdeBytes(p + 4, largoCert);
    resultado.llaveCifradaDer = BufferSecreto::desdeBytes(p + 8 + largoCert, largoLlave);
    return R::exito(std::move(resultado));
}

Resultado<QByteArray, ErrorSecretStore> cifrarContenedor(const BufferSecreto& clave, QStringView uuid,
                                                         const BufferSecreto& carga)
{
    using R = Resultado<QByteArray, ErrorSecretStore>;
    const auto datosAad = aad(uuid);
    if (clave.tamano() != kTamanoClaveContenedor || !datosAad || carga.vacio()
        || carga.tamano() > kMaxCargaUtil) {
        return R::fallo(ErrorSecretStore::de(Categoria::FormatoInvalido, QStringLiteral("contenedor.entrada")));
    }

    auto nonce = bytesAleatorios(kTamanoNonce);
    if (!nonce) {
        return R::fallo(std::move(nonce).error());
    }

    const std::size_t total = kTamanoCabecera + carga.tamano() + kTamanoTag;
    QByteArray salida(static_cast<qsizetype>(total), '\0');
    auto* p = reinterpret_cast<std::uint8_t*>(salida.data());
    std::memcpy(p, kMagic, 4);
    p[4] = kVersion;
    p[5] = kAlgAes256Gcm;
    p[6] = static_cast<std::uint8_t>(kTamanoNonce);
    std::memcpy(p + 7, nonce.valor().constData(), kTamanoNonce);
    escribirU32(p + 7 + kTamanoNonce, static_cast<std::uint32_t>(carga.tamano()));
    std::uint8_t* ct = p + kTamanoCabecera;
    std::uint8_t* tag = ct + carga.tamano();

    CtxPtr ctx(EVP_CIPHER_CTX_new());
    int n = 0;
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kTamanoNonce), nullptr) != 1
        || EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, clave.datos(), p + 7) != 1
        || EVP_EncryptUpdate(ctx.get(), nullptr, &n, datosAad->data(), static_cast<int>(datosAad->size())) != 1
        || EVP_EncryptUpdate(ctx.get(), ct, &n, carga.datos(), static_cast<int>(carga.tamano())) != 1
        || static_cast<std::size_t>(n) != carga.tamano()
        || EVP_EncryptFinal_ex(ctx.get(), ct + n, &n) != 1 || n != 0
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTamanoTag), tag) != 1) {
        return R::fallo(fallo(Categoria::Interno, "aead.cifrar"));
    }
    return R::exito(std::move(salida));
}

Resultado<BufferSecreto, ErrorSecretStore> descifrarContenedor(const BufferSecreto& clave, QStringView uuid,
                                                               const std::uint8_t* datos, std::size_t tamano)
{
    using R = Resultado<BufferSecreto, ErrorSecretStore>;
    const auto danada = [](const char* diagnostico) {
        return R::fallo(fallo(Categoria::CredencialDanada, diagnostico));
    };
    const auto datosAad = aad(uuid);
    if (clave.tamano() != kTamanoClaveContenedor || !datosAad) {
        return danada("contenedor.clave");
    }
    // Cabecera y longitudes ANTES de reservar memoria.
    if (datos == nullptr || tamano < kTamanoCabecera + kTamanoTag + 1 || tamano > kMaxContenedor) {
        return danada("contenedor.tamano");
    }
    if (std::memcmp(datos, kMagic, 4) != 0 || datos[4] != kVersion || datos[5] != kAlgAes256Gcm
        || datos[6] != kTamanoNonce) {
        return danada("contenedor.cabecera");
    }
    const std::size_t largoCt = leerU32(datos + 7 + kTamanoNonce);
    if (largoCt == 0 || largoCt > kMaxCargaUtil || kTamanoCabecera + largoCt + kTamanoTag != tamano) {
        return danada("contenedor.longitud");
    }
    const std::uint8_t* nonce = datos + 7;
    const std::uint8_t* ct = datos + kTamanoCabecera;
    const std::uint8_t* tag = ct + largoCt;

    BufferSecreto carga(largoCt);
    CtxPtr ctx(EVP_CIPHER_CTX_new());
    int n = 0;
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kTamanoNonce), nullptr) != 1
        || EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, clave.datos(), nonce) != 1
        || EVP_DecryptUpdate(ctx.get(), nullptr, &n, datosAad->data(), static_cast<int>(datosAad->size())) != 1
        || EVP_DecryptUpdate(ctx.get(), carga.datos(), &n, ct, static_cast<int>(largoCt)) != 1
        || static_cast<std::size_t>(n) != largoCt
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTamanoTag),
                               const_cast<std::uint8_t*>(tag))
            != 1) {
        return danada("aead.descifrar");
    }
    if (EVP_DecryptFinal_ex(ctx.get(), carga.datos() + n, &n) != 1) {
        // Tag/AAD invalidos: la carga parcial se limpia al destruir el buffer.
        return danada("aead.autenticacion");
    }
    ERR_clear_error();
    return R::exito(std::move(carga));
}

} // namespace satcfdi::crypto
