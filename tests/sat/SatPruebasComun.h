#pragma once

// Utilidades de satcfdi_sat_spike_tests (T006). Material de firma TEMPORAL
// generado en cada ejecucion (RSA 2048 + X.509 autofirmado): ningun
// certificado, llave ni e.firma real se versiona.

#include "ports/secrets/SecretStoreTypes.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <memory>
#include <optional>

namespace satpruebas {

// Regenerar goldens exige `--actualizar-goldens` en la linea de comandos Y
// SATCFDI_ACTUALIZAR_GOLDENS en el entorno; ctest no pasa el argumento.
inline bool actualizarGoldens = false;

// Serial con decimal conocido (159 bits, cabe en los 20 octetos de X.509).
inline const char* const kSerialDecimal = "292233162870206001759766198425879490509300";

// Issuer del ejemplo de docs/web-service.md §6.1 en formato DocSat.
inline const QString kIssuerDocSat = QStringLiteral(
    "OID.1.2.840.113549.1.9.2=Responsable: ACDMA, OID.2.5.4.45=SAT970701NN3, L=Coyoacán, "
    "S=Distrito Federal, C=MX, PostalCode=06300, CN=A.C. 2 de pruebas(4096)");

struct MaterialPrueba {
    QByteArray certificadoDer;
    QByteArray llavePkcs8Der;

    satcfdi::MaterialFirma material() const
    {
        return satcfdi::MaterialFirma(
            satcfdi::BufferSecreto::desdeBytes(certificadoDer.constData(), static_cast<std::size_t>(certificadoDer.size())),
            satcfdi::BufferSecreto::desdeBytes(llavePkcs8Der.constData(), static_cast<std::size_t>(llavePkcs8Der.size())),
            satcfdi::BufferSecreto::desdeBytes("no-se-usa", 9));
    }
};

inline bool agregar(X509_NAME* n, const char* campo, const char* valor)
{
    return X509_NAME_add_entry_by_txt(n, campo, MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(valor), -1,
                                      -1, 0)
           == 1;
}

// RSA 2048 + certificado autofirmado. Orden del DN (primero a ultimo) para que
// el formato DocSat (orden inverso) reproduzca kIssuerDocSat.
inline std::optional<MaterialPrueba> generarMaterial()
{
    EVP_PKEY* llave = EVP_RSA_gen(2048);
    X509* cert = X509_new();
    BIGNUM* bn = nullptr;
    std::optional<MaterialPrueba> r;
    do {
        if (llave == nullptr || cert == nullptr || BN_dec2bn(&bn, kSerialDecimal) == 0) {
            break;
        }
        X509_set_version(cert, 2);
        ASN1_INTEGER* serial = BN_to_ASN1_INTEGER(bn, nullptr);
        X509_set_serialNumber(cert, serial);
        ASN1_INTEGER_free(serial);
        X509_gmtime_adj(X509_getm_notBefore(cert), -3600);
        X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
        X509_NAME* nombre = X509_get_subject_name(cert);
        if (!agregar(nombre, "CN", "A.C. 2 de pruebas(4096)") || !agregar(nombre, "postalCode", "06300")
            || !agregar(nombre, "C", "MX") || !agregar(nombre, "ST", "Distrito Federal")
            || !agregar(nombre, "L", "Coyoacán") || !agregar(nombre, "2.5.4.45", "SAT970701NN3")
            || !agregar(nombre, "1.2.840.113549.1.9.2", "Responsable: ACDMA")) {
            break;
        }
        X509_set_issuer_name(cert, nombre);
        X509_set_pubkey(cert, llave);
        if (X509_sign(cert, llave, EVP_sha256()) <= 0) {
            break;
        }
        MaterialPrueba m;
        unsigned char* der = nullptr;
        int n = i2d_X509(cert, &der);
        if (n <= 0) {
            break;
        }
        m.certificadoDer = QByteArray(reinterpret_cast<const char*>(der), n);
        OPENSSL_free(der);
        der = nullptr;
        PKCS8_PRIV_KEY_INFO* p8 = EVP_PKEY2PKCS8(llave);
        n = p8 != nullptr ? i2d_PKCS8_PRIV_KEY_INFO(p8, &der) : -1;
        PKCS8_PRIV_KEY_INFO_free(p8);
        if (n <= 0) {
            break;
        }
        m.llavePkcs8Der = QByteArray(reinterpret_cast<const char*>(der), n);
        OPENSSL_clear_free(der, static_cast<size_t>(n));
        r = std::move(m);
    } while (false);
    BN_free(bn);
    X509_free(cert);
    EVP_PKEY_free(llave);
    return r;
}

// Verifica RSA-SHA1 (PKCS#1 v1.5) de `datos` con la llave publica del certificado.
inline bool verificarRsaSha1(const QByteArray& certificadoDer, const QByteArray& datos, const QByteArray& firma)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(certificadoDer.constData());
    X509* cert = d2i_X509(nullptr, &p, certificadoDer.size());
    if (cert == nullptr) {
        return false;
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = ctx != nullptr && EVP_DigestVerifyInit(ctx, nullptr, EVP_sha1(), nullptr, X509_get0_pubkey(cert)) == 1
              && EVP_DigestVerify(ctx, reinterpret_cast<const unsigned char*>(firma.constData()),
                                  static_cast<size_t>(firma.size()),
                                  reinterpret_cast<const unsigned char*>(datos.constData()),
                                  static_cast<size_t>(datos.size()))
                     == 1;
    EVP_MD_CTX_free(ctx);
    X509_free(cert);
    return ok;
}

inline QByteArray leer(const QString& ruta)
{
    QFile f(ruta);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Texto entre <...nombre ...> y </...nombre> (primera aparicion; sin prefijo).
inline QByteArray contenidoElemento(const QByteArray& xml, const QByteArray& nombre)
{
    const qsizetype ini = xml.indexOf(nombre + ">");
    if (ini < 0) {
        return {};
    }
    const qsizetype desde = ini + nombre.size() + 1;
    const qsizetype fin = xml.indexOf("</", desde);
    return fin < 0 ? QByteArray() : xml.mid(desde, fin - desde);
}

} // namespace satpruebas
