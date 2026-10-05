#pragma once

#include "domain/common/Resultado.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QByteArray>
#include <QString>

namespace satcfdi::sat {

// Primitivas criptograficas de la firma XML del spike (T006, D2): datos no
// secretos del certificado para KeyInfo y firma RSA PKCS#1 v1.5 con SHA-1
// (OpenSSL 3). Consumen `const MaterialFirma&` (D4/D8) sin copiar la llave
// fuera de buffers de OpenSSL. Sin tipos de OpenSSL en el header.

// Formato del X509IssuerName.
// - DocSat: el del ejemplo de docs/web-service.md §6.1 (estilo .NET
//   X500DistinguishedName): RDN en orden inverso, separador ", ", nombres
//   cortos CN, C, L, S, O, OU, E, STREET, T, G, I, SN, SERIALNUMBER, DC,
//   PostalCode y "OID.<oid>" para el resto (p. ej. OID.2.5.4.45); valores con
//   , = + < > # ; " o espacios exteriores van entre comillas ("" escapa ").
// - Rfc4514: cadena RFC 4514/2253 de OpenSSL (alternativa por si SAT la exige).
enum class FormatoIssuer {
    DocSat,
    Rfc4514,
};

struct DatosCertificado {
    QByteArray certificadoBase64; // DER en Base64 (BinarySecurityToken/X509Certificate)
    QString issuer;               // X509IssuerName en el formato pedido
    QString serialDecimal;        // X509SerialNumber en DECIMAL (xs:integer)
};

struct ErrorFirma {
    QString diagnostico; // tecnico y sin material
};

Resultado<DatosCertificado, ErrorFirma> leerDatosCertificado(const BufferSecreto& certificadoDer,
                                                             FormatoIssuer formato);

// Firma RSA PKCS#1 v1.5 + SHA-1 de `datos` con la llave PKCS#8 DER (sin cifrar)
// de `material`. Devuelve los bytes de la firma (sin Base64).
Resultado<QByteArray, ErrorFirma> firmarRsaSha1(const MaterialFirma& material, const QByteArray& datos);

} // namespace satcfdi::sat
