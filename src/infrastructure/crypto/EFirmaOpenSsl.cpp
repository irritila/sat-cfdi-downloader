#include "infrastructure/crypto/EFirmaOpenSsl.h"

#include "domain/common/Rfc.h"

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pkcs12.h>
#include <openssl/x509.h>

#include <QDate>
#include <QTime>
#include <QTimeZone>

#include <climits>
#include <ctime>
#include <memory>

namespace satcfdi::crypto {

namespace {

using Categoria = ErrorSecretStore::Categoria;

struct LiberarX509 {
    void operator()(X509* p) const noexcept { X509_free(p); }
};
struct LiberarSig {
    void operator()(X509_SIG* p) const noexcept { X509_SIG_free(p); }
};
struct LiberarP8 {
    // El callback ASN.1 de PKCS8_PRIV_KEY_INFO limpia la llave al liberarla.
    void operator()(PKCS8_PRIV_KEY_INFO* p) const noexcept { PKCS8_PRIV_KEY_INFO_free(p); }
};
struct LiberarPkey {
    void operator()(EVP_PKEY* p) const noexcept { EVP_PKEY_free(p); }
};
struct LiberarPkeyCtx {
    void operator()(EVP_PKEY_CTX* p) const noexcept { EVP_PKEY_CTX_free(p); }
};
struct LiberarBn {
    void operator()(BIGNUM* p) const noexcept { BN_free(p); }
};
struct LiberarOpenSsl {
    void operator()(char* p) const noexcept { OPENSSL_free(p); }
};
struct LiberarUtf8 {
    // Buffers de ASN1_STRING_to_UTF8: pueden contener datos del subject; se
    // limpian por higiene aunque no sean secretos.
    std::size_t tamano = 0;
    void operator()(unsigned char* p) const noexcept { OPENSSL_clear_free(p, tamano); }
};

using X509Ptr = std::unique_ptr<X509, LiberarX509>;
using SigPtr = std::unique_ptr<X509_SIG, LiberarSig>;
using P8Ptr = std::unique_ptr<PKCS8_PRIV_KEY_INFO, LiberarP8>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, LiberarPkey>;
using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, LiberarPkeyCtx>;

// Construye el error y vacia la cola de errores de OpenSSL (que podria
// retener contexto). Solo se conserva el codigo de razon.
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

bool tamanoValido(std::size_t tamano, std::size_t maximo)
{
    return tamano > 0 && tamano <= maximo && tamano <= static_cast<std::size_t>(LONG_MAX);
}

std::optional<QDateTime> aUtc(const ASN1_TIME* tiempo)
{
    if (tiempo == nullptr) {
        return std::nullopt;
    }
    std::tm tm{};
    if (ASN1_TIME_to_tm(tiempo, &tm) != 1) {
        return std::nullopt;
    }
    const QDate fecha(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    const QTime hora(tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (!fecha.isValid() || !hora.isValid()) {
        return std::nullopt;
    }
    return QDateTime(fecha, hora, QTimeZone::UTC);
}

// Valor textual de una entrada del nombre (cualquier tipo de cadena ASN.1).
// nullopt si no es una cadena convertible (p. ej. BIT STRING).
template <typename Funcion>
bool conValorUtf8(X509_NAME_ENTRY* entrada, Funcion&& funcion)
{
    const ASN1_STRING* dato = X509_NAME_ENTRY_get_data(entrada);
    if (dato == nullptr) {
        return false;
    }
    unsigned char* utf8 = nullptr;
    const int largo = ASN1_STRING_to_UTF8(&utf8, dato);
    if (largo < 0) {
        ERR_clear_error();
        return false;
    }
    std::unique_ptr<unsigned char, LiberarUtf8> guardia(utf8, LiberarUtf8{static_cast<std::size_t>(largo)});
    funcion(utf8, static_cast<std::size_t>(largo));
    return true;
}

bool tieneOuNoVacio(X509_NAME* nombre)
{
    int posicion = -1;
    while ((posicion = X509_NAME_get_index_by_NID(nombre, NID_organizationalUnitName, posicion)) >= 0) {
        bool noVacio = true; // conservador: valor no legible cuenta como OU
        conValorUtf8(X509_NAME_get_entry(nombre, posicion), [&](const unsigned char* v, std::size_t n) {
            noVacio = false;
            for (std::size_t i = 0; i < n; ++i) {
                if (v[i] != ' ' && v[i] != '\t') {
                    noVacio = true;
                    break;
                }
            }
        });
        if (noVacio) {
            return true;
        }
    }
    return false;
}

struct CertificadoParseado {
    X509Ptr x509;
    InfoCertificado info;
};

Resultado<CertificadoParseado, ErrorSecretStore> parsearCertificado(const std::uint8_t* der, std::size_t tamano)
{
    using R = Resultado<CertificadoParseado, ErrorSecretStore>;
    if (der == nullptr || !tamanoValido(tamano, kMaxCertificadoDer)) {
        return R::fallo(fallo(Categoria::FormatoInvalido, "x509.tamano"));
    }
    const unsigned char* p = der;
    X509Ptr x509(d2i_X509(nullptr, &p, static_cast<long>(tamano)));
    if (!x509 || p != der + tamano) {
        return R::fallo(fallo(Categoria::FormatoInvalido, "x509.parse"));
    }

    CertificadoParseado resultado;
    InfoCertificado& info = resultado.info;

    const auto desde = aUtc(X509_get0_notBefore(x509.get()));
    const auto hasta = aUtc(X509_get0_notAfter(x509.get()));
    if (!desde || !hasta || !(*desde < *hasta)) {
        return R::fallo(fallo(Categoria::FormatoInvalido, "x509.vigencia"));
    }
    info.metadata.vigenteDesde = *desde;
    info.metadata.vigenteHasta = *hasta;

    std::unique_ptr<BIGNUM, LiberarBn> serie(ASN1_INTEGER_to_BN(X509_get0_serialNumber(x509.get()), nullptr));
    if (!serie) {
        return R::fallo(fallo(Categoria::FormatoInvalido, "x509.serie"));
    }
    std::unique_ptr<char, LiberarOpenSsl> hex(BN_bn2hex(serie.get()));
    if (!hex || hex.get()[0] == '-' || hex.get()[0] == '\0') {
        return R::fallo(fallo(Categoria::FormatoInvalido, "x509.serie"));
    }
    info.metadata.numeroSerie = QString::fromLatin1(hex.get()).toUpper();
    if (info.metadata.numeroSerie.size() > 64) {
        return R::fallo(fallo(Categoria::FormatoInvalido, "x509.serie"));
    }

    X509_NAME* sujeto = X509_get_subject_name(x509.get());
    if (sujeto == nullptr) {
        return R::fallo(fallo(Categoria::FormatoInvalido, "x509.subject"));
    }
    info.tieneUnidadOrganizacional = tieneOuNoVacio(sujeto);

    const int indiceRfc = X509_NAME_get_index_by_NID(sujeto, NID_x500UniqueIdentifier, -1);
    if (indiceRfc >= 0) {
        if (X509_NAME_get_index_by_NID(sujeto, NID_x500UniqueIdentifier, indiceRfc) >= 0) {
            // Mas de un 2.5.4.45: ambiguo, no se elige uno.
            return R::fallo(fallo(Categoria::FormatoInvalido, "x509.rfc.multiple"));
        }
        conValorUtf8(X509_NAME_get_entry(sujeto, indiceRfc), [&](const unsigned char* v, std::size_t n) {
            info.rfc = rfcDesdeValorX500UniqueIdentifier(v, n);
        });
        info.rfcValido = !info.rfc.isEmpty();
    }
    ERR_clear_error();
    resultado.x509 = std::move(x509);
    return R::exito(std::move(resultado));
}

// EncryptedPrivateKeyInfo DER -> PKCS8_PRIV_KEY_INFO descifrado.
Resultado<P8Ptr, ErrorSecretStore> descifrarPkcs8(const BufferSecreto& llave, const BufferSecreto& contrasena)
{
    using R = Resultado<P8Ptr, ErrorSecretStore>;
    if (!tamanoValido(llave.tamano(), kMaxLlaveDer)) {
        return R::fallo(fallo(Categoria::FormatoInvalido, "pkcs8.tamano"));
    }
    if (contrasena.tamano() > kMaxContrasena) {
        return R::fallo(fallo(Categoria::ContrasenaIncorrecta, "pkcs8.contrasena.tamano"));
    }
    const unsigned char* p = llave.datos();
    SigPtr sig(d2i_X509_SIG(nullptr, &p, static_cast<long>(llave.tamano())));
    if (!sig || p != llave.datos() + llave.tamano()) {
        return R::fallo(fallo(Categoria::FormatoInvalido, "pkcs8.parse"));
    }
    // Estructura valida: un fallo de descifrado se reporta como contrasena
    // incorrecta (corrupcion del ciphertext es indistinguible, DS2).
    static const char kVacia[] = "";
    const char* clave = contrasena.vacio() ? kVacia : reinterpret_cast<const char*>(contrasena.datos());
    P8Ptr p8(PKCS8_decrypt_ex(sig.get(), clave, static_cast<int>(contrasena.tamano()), nullptr, nullptr));
    if (!p8) {
        return R::fallo(fallo(Categoria::ContrasenaIncorrecta, "pkcs8.decrypt"));
    }
    ERR_clear_error();
    return R::exito(std::move(p8));
}

} // namespace

QString rfcDesdeValorX500UniqueIdentifier(const std::uint8_t* valor, std::size_t tamano)
{
    if (valor == nullptr || tamano == 0 || tamano > 256) {
        return {};
    }
    for (std::size_t i = 0; i < tamano; ++i) {
        if (valor[i] < 0x20 || valor[i] > 0x7E) {
            return {}; // NUL, control o no ASCII
        }
    }
    std::size_t inicio = 0;
    std::size_t fin = tamano;
    // Parte izquierda de "RFC / CURP" (una sola separacion).
    for (std::size_t i = 0; i < tamano; ++i) {
        if (valor[i] == '/') {
            fin = i;
            break;
        }
    }
    while (inicio < fin && valor[inicio] == ' ') {
        ++inicio;
    }
    while (fin > inicio && valor[fin - 1] == ' ') {
        --fin;
    }
    QString rfc;
    rfc.reserve(static_cast<qsizetype>(fin - inicio));
    for (std::size_t i = inicio; i < fin; ++i) {
        char c = static_cast<char>(valor[i]);
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        rfc.append(QLatin1Char(c));
    }
    return rfc::esValido(rfc) ? rfc : QString();
}

Resultado<InfoCertificado, ErrorSecretStore> leerCertificado(const std::uint8_t* der, std::size_t tamano)
{
    auto parseado = parsearCertificado(der, tamano);
    if (!parseado) {
        return Resultado<InfoCertificado, ErrorSecretStore>::fallo(std::move(parseado).error());
    }
    return Resultado<InfoCertificado, ErrorSecretStore>::exito(std::move(parseado).valor().info);
}

EstadoCredencial estadoPorVigencia(const MetadataCredencial& metadata, const QDateTime& ahoraUtc)
{
    if (ahoraUtc < metadata.vigenteDesde) {
        return EstadoCredencial::NoVigenteAun;
    }
    if (ahoraUtc >= metadata.vigenteHasta) {
        return EstadoCredencial::Vencida;
    }
    return EstadoCredencial::Lista;
}

Resultado<EFirmaValidada, ErrorSecretStore> validarEFirma(const BufferSecreto& certificadoDer,
                                                          const BufferSecreto& llaveCifradaDer,
                                                          const BufferSecreto& contrasena,
                                                          QStringView rfcEsperado,
                                                          const QDateTime& ahoraUtc)
{
    using R = Resultado<EFirmaValidada, ErrorSecretStore>;

    // T005.1 (DA4): origen no sensible del error para enfocar el formulario.
    auto cert = parsearCertificado(certificadoDer.datos(), certificadoDer.tamano());
    if (!cert) {
        return R::fallo(std::move(cert).error().conOrigen(OrigenErrorEFirma::Certificado));
    }
    CertificadoParseado parseado = std::move(cert).valor();

    auto p8 = descifrarPkcs8(llaveCifradaDer, contrasena);
    if (!p8) {
        ErrorSecretStore e = std::move(p8).error();
        const OrigenErrorEFirma origen = e.categoria == Categoria::ContrasenaIncorrecta
                                             ? OrigenErrorEFirma::Contrasena
                                             : OrigenErrorEFirma::Llave;
        return R::fallo(std::move(e).conOrigen(origen));
    }
    PkeyPtr llave(EVP_PKCS82PKEY_ex(p8.valor().get(), nullptr, nullptr));
    if (!llave) {
        return R::fallo(fallo(Categoria::FormatoInvalido, "pkcs8.pkey").conOrigen(OrigenErrorEFirma::Llave));
    }
    PkeyCtxPtr ctx(EVP_PKEY_CTX_new_from_pkey(nullptr, llave.get(), nullptr));
    if (!ctx) {
        return R::fallo(fallo(Categoria::Interno, "pkey.ctx"));
    }
    if (EVP_PKEY_private_check(ctx.get()) != 1 || EVP_PKEY_pairwise_check(ctx.get()) != 1) {
        return R::fallo(fallo(Categoria::FormatoInvalido, "pkey.check").conOrigen(OrigenErrorEFirma::Llave));
    }
    if (X509_check_private_key(parseado.x509.get(), llave.get()) != 1) {
        return R::fallo(fallo(Categoria::ParejaIncompatible, "x509.check_private_key"));
    }

    const InfoCertificado& info = parseado.info;
    if (info.tieneUnidadOrganizacional) {
        return R::fallo(fallo(Categoria::NoEsEFirma, "x509.subject.ou"));
    }
    if (!info.rfcValido || rfcEsperado.isEmpty() || info.rfc != rfcEsperado) {
        // El RFC del certificado nunca se incluye en el error.
        return R::fallo(fallo(Categoria::RfcNoCoincide, "x509.rfc"));
    }
    switch (estadoPorVigencia(info.metadata, ahoraUtc)) {
    case EstadoCredencial::NoVigenteAun:
        return R::fallo(fallo(Categoria::NoVigenteAun, "x509.not_before"));
    case EstadoCredencial::Vencida:
        return R::fallo(fallo(Categoria::Vencida, "x509.not_after"));
    default:
        break;
    }
    ERR_clear_error();
    return R::exito(EFirmaValidada{info.metadata});
}

Resultado<BufferSecreto, ErrorSecretStore> descifrarLlavePkcs8(const BufferSecreto& llaveCifradaDer,
                                                              const BufferSecreto& contrasena)
{
    using R = Resultado<BufferSecreto, ErrorSecretStore>;
    auto p8 = descifrarPkcs8(llaveCifradaDer, contrasena);
    if (!p8) {
        return R::fallo(std::move(p8).error());
    }
    // Serializa directamente en un BufferSecreto (sin buffer de OpenSSL).
    const int largo = i2d_PKCS8_PRIV_KEY_INFO(p8.valor().get(), nullptr);
    if (largo <= 0 || static_cast<std::size_t>(largo) > kMaxLlaveDer) {
        return R::fallo(fallo(Categoria::FormatoInvalido, "pkcs8.encode"));
    }
    BufferSecreto salida(static_cast<std::size_t>(largo));
    unsigned char* p = salida.datos();
    if (i2d_PKCS8_PRIV_KEY_INFO(p8.valor().get(), &p) != largo) {
        return R::fallo(fallo(Categoria::Interno, "pkcs8.encode"));
    }
    return R::exito(std::move(salida));
}

} // namespace satcfdi::crypto
