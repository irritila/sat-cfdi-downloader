#include "infrastructure/sat/FirmaXml.h"

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <QStringList>

#include <memory>

namespace satcfdi::sat {

namespace {

template <typename T, void (*F)(T*)>
struct Liberar {
    void operator()(T* p) const noexcept { F(p); }
};
using X509Ptr = std::unique_ptr<X509, Liberar<X509, X509_free>>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, Liberar<EVP_PKEY, EVP_PKEY_free>>;
using MdCtxPtr = std::unique_ptr<EVP_MD_CTX, Liberar<EVP_MD_CTX, EVP_MD_CTX_free>>;
using BioPtr = std::unique_ptr<BIO, Liberar<BIO, BIO_free_all>>;
using BnPtr = std::unique_ptr<BIGNUM, Liberar<BIGNUM, BN_free>>;

void liberarOpenSsl(char* p) { OPENSSL_free(p); }
using CadenaOpenSsl = std::unique_ptr<char, Liberar<char, liberarOpenSsl>>;

QString nombreCortoDocSat(const ASN1_OBJECT* obj)
{
    switch (OBJ_obj2nid(obj)) {
    case NID_commonName: return QStringLiteral("CN");
    case NID_countryName: return QStringLiteral("C");
    case NID_localityName: return QStringLiteral("L");
    case NID_stateOrProvinceName: return QStringLiteral("S");
    case NID_organizationName: return QStringLiteral("O");
    case NID_organizationalUnitName: return QStringLiteral("OU");
    case NID_pkcs9_emailAddress: return QStringLiteral("E");
    case NID_streetAddress: return QStringLiteral("STREET");
    case NID_title: return QStringLiteral("T");
    case NID_givenName: return QStringLiteral("G");
    case NID_initials: return QStringLiteral("I");
    case NID_surname: return QStringLiteral("SN");
    case NID_serialNumber: return QStringLiteral("SERIALNUMBER");
    case NID_domainComponent: return QStringLiteral("DC");
    case NID_postalCode: return QStringLiteral("PostalCode");
    default: break;
    }
    char oid[128];
    const int n = OBJ_obj2txt(oid, sizeof(oid), obj, 1);
    if (n <= 0 || n >= static_cast<int>(sizeof(oid))) {
        return QStringLiteral("OID.?");
    }
    return QStringLiteral("OID.") + QString::fromLatin1(oid);
}

QString valorDocSat(const QString& v)
{
    static const QString kEspeciales = QStringLiteral(",=+<>#;\"\n");
    bool comillas = v.isEmpty() || v.front().isSpace() || v.back().isSpace();
    for (QChar c : v) {
        if (kEspeciales.contains(c)) {
            comillas = true;
            break;
        }
    }
    if (!comillas) {
        return v;
    }
    QString escapado = v;
    escapado.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escapado + QLatin1Char('"');
}

Resultado<QString, ErrorFirma> issuerDocSat(const X509_NAME* nombre)
{
    using R = Resultado<QString, ErrorFirma>;
    QStringList partes;
    for (int i = X509_NAME_entry_count(nombre) - 1; i >= 0; --i) {
        const X509_NAME_ENTRY* e = X509_NAME_get_entry(nombre, i);
        unsigned char* utf8 = nullptr;
        const int largo = ASN1_STRING_to_UTF8(&utf8, X509_NAME_ENTRY_get_data(e));
        if (largo < 0) {
            return R::fallo({QStringLiteral("x509.issuer.valor")});
        }
        const QString valor = QString::fromUtf8(reinterpret_cast<const char*>(utf8), largo);
        OPENSSL_free(utf8);
        partes.append(nombreCortoDocSat(X509_NAME_ENTRY_get_object(e)) + QLatin1Char('=') + valorDocSat(valor));
    }
    return R::exito(partes.join(QStringLiteral(", ")));
}

Resultado<QString, ErrorFirma> issuerRfc4514(const X509_NAME* nombre)
{
    using R = Resultado<QString, ErrorFirma>;
    BioPtr bio(BIO_new(BIO_s_mem()));
    if (!bio || X509_NAME_print_ex(bio.get(), nombre, 0, XN_FLAG_RFC2253 & ~ASN1_STRFLGS_ESC_MSB) < 0) {
        return R::fallo({QStringLiteral("x509.issuer.rfc4514")});
    }
    char* datos = nullptr;
    const long largo = BIO_get_mem_data(bio.get(), &datos);
    return R::exito(QString::fromUtf8(datos, static_cast<qsizetype>(largo)));
}

} // namespace

Resultado<DatosCertificado, ErrorFirma> leerDatosCertificado(const BufferSecreto& certificadoDer,
                                                             FormatoIssuer formato)
{
    using R = Resultado<DatosCertificado, ErrorFirma>;
    const unsigned char* p = certificadoDer.datos();
    X509Ptr x509(d2i_X509(nullptr, &p, static_cast<long>(certificadoDer.tamano())));
    if (!x509 || p != certificadoDer.datos() + certificadoDer.tamano()) {
        return R::fallo({QStringLiteral("x509.parse")});
    }
    DatosCertificado datos;
    datos.certificadoBase64 =
        QByteArray(reinterpret_cast<const char*>(certificadoDer.datos()), static_cast<qsizetype>(certificadoDer.tamano()))
            .toBase64();

    const X509_NAME* issuer = X509_get_issuer_name(x509.get());
    auto texto = formato == FormatoIssuer::DocSat ? issuerDocSat(issuer) : issuerRfc4514(issuer);
    if (!texto) {
        return R::fallo(std::move(texto).error());
    }
    datos.issuer = std::move(texto).valor();

    BnPtr bn(ASN1_INTEGER_to_BN(X509_get0_serialNumber(x509.get()), nullptr));
    if (!bn) {
        return R::fallo({QStringLiteral("x509.serial")});
    }
    CadenaOpenSsl decimal(BN_bn2dec(bn.get()));
    if (!decimal) {
        return R::fallo({QStringLiteral("x509.serial.decimal")});
    }
    datos.serialDecimal = QString::fromLatin1(decimal.get());
    return R::exito(std::move(datos));
}

Resultado<QByteArray, ErrorFirma> firmarRsaSha1(const MaterialFirma& material, const QByteArray& datos)
{
    using R = Resultado<QByteArray, ErrorFirma>;
    const BufferSecreto& der = material.llavePrivadaDer();
    const unsigned char* p = der.datos();
    PkeyPtr llave(d2i_AutoPrivateKey(nullptr, &p, static_cast<long>(der.tamano())));
    if (!llave) {
        return R::fallo({QStringLiteral("llave.parse")});
    }
    if (EVP_PKEY_get_base_id(llave.get()) != EVP_PKEY_RSA) {
        return R::fallo({QStringLiteral("llave.no_rsa")});
    }
    MdCtxPtr ctx(EVP_MD_CTX_new());
    EVP_PKEY_CTX* pctx = nullptr;
    if (!ctx || EVP_DigestSignInit(ctx.get(), &pctx, EVP_sha1(), nullptr, llave.get()) != 1
        || EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PADDING) <= 0) {
        return R::fallo({QStringLiteral("firma.init")});
    }
    size_t largo = 0;
    const auto* entrada = reinterpret_cast<const unsigned char*>(datos.constData());
    if (EVP_DigestSign(ctx.get(), nullptr, &largo, entrada, static_cast<size_t>(datos.size())) != 1) {
        return R::fallo({QStringLiteral("firma.largo")});
    }
    QByteArray firma(static_cast<qsizetype>(largo), '\0');
    if (EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(firma.data()), &largo, entrada,
                       static_cast<size_t>(datos.size()))
        != 1) {
        return R::fallo({QStringLiteral("firma.rsa_sha1")});
    }
    firma.resize(static_cast<qsizetype>(largo));
    return R::exito(std::move(firma));
}

} // namespace satcfdi::sat
