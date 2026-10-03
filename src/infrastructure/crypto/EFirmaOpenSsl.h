#pragma once

// Validacion criptografica de la e.firma con OpenSSL 3 (T005, DU1, DS1, DS2,
// DU2). Portable (sin Security.framework). El header NO expone tipos de
// OpenSSL: solo BufferSecreto y tipos del puerto.
//
// Todas las funciones trabajan sobre bytes en memoria (DER), sin PEM ni
// archivos temporales. Ningun diagnostico contiene RFC, rutas ni material:
// solo una etiqueta tecnica y, cuando aplica, el codigo de razon de OpenSSL.

#include "domain/common/Resultado.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QDateTime>
#include <QString>
#include <QStringView>

#include <cstddef>
#include <cstdint>

namespace satcfdi::crypto {

// Limites aplicados ANTES de reservar memoria o invocar el parser ASN.1.
// Un .cer/.key real ocupa ~1-2 KiB.
inline constexpr std::size_t kMaxCertificadoDer = 64 * 1024;
inline constexpr std::size_t kMaxLlaveDer = 64 * 1024;
inline constexpr std::size_t kMaxContrasena = 1024;

// Datos no secretos leidos de un certificado X.509 DER.
struct InfoCertificado {
    MetadataCredencial metadata; // serie (hex mayusculas), notBefore, notAfter (UTC)
    // RFC (parte izquierda de "RFC / CURP" del OID 2.5.4.45), ASCII mayusculas.
    // Vacio si el atributo falta o no es valido (ver rfcValido).
    QString rfc;
    bool rfcValido = false;
    // DU2: algun subject.OU no vacio => CSD, no e.firma.
    bool tieneUnidadOrganizacional = false;
};

// Parsea un X.509 DER completo (sin bytes sobrantes) y extrae metadata, RFC
// y OU. Errores: FormatoInvalido (estructura, tamano, fechas, serie > 64
// hex, varios atributos 2.5.4.45).
Resultado<InfoCertificado, ErrorSecretStore> leerCertificado(const std::uint8_t* der, std::size_t tamano);

// DS1: normaliza el valor del atributo 2.5.4.45. Rechaza NUL/control/no
// ASCII; recorta espacios ASCII exteriores; mayusculas ASCII; si contiene '/',
// toma la parte izquierda (recortada). Devuelve el RFC solo si cumple la
// gramatica del dominio (rfc::esValido); en otro caso, cadena vacia.
QString rfcDesdeValorX500UniqueIdentifier(const std::uint8_t* valor, std::size_t tamano);

// Resultado de validar una e.firma completa.
struct EFirmaValidada {
    MetadataCredencial metadata;
};

// Valida el par .cer/.key para importacion (DS2, DS1, DU2, vigencia local),
// en este orden:
//  1. tamanos y estructura del X.509 DER                -> FormatoInvalido
//  2. estructura EncryptedPrivateKeyInfo (PKCS#8 DER)    -> FormatoInvalido
//  3. descifrado con `contrasena` (PBES1/PBES2)          -> ContrasenaIncorrecta
//  4. EVP_PKEY_private_check / pairwise_check            -> FormatoInvalido
//  5. X509_check_private_key                             -> ParejaIncompatible
//  6. subject.OU no vacio (DU2)                          -> NoEsEFirma
//  7. RFC 2.5.4.45 == rfcEsperado (ya normalizado)       -> RfcNoCoincide
//  8. vigencia: ahora < notBefore -> NoVigenteAun; ahora >= notAfter -> Vencida
// T005.1: `ErrorSecretStore::origen` = Certificado en 1, Llave en 2 y 4,
// Contrasena en 3; Ninguno en el resto.
Resultado<EFirmaValidada, ErrorSecretStore> validarEFirma(const BufferSecreto& certificadoDer,
                                                          const BufferSecreto& llaveCifradaDer,
                                                          const BufferSecreto& contrasena,
                                                          QStringView rfcEsperado,
                                                          const QDateTime& ahoraUtc);

// Descifra el PKCS#8 DER y devuelve el PrivateKeyInfo DER SIN cifrar (para
// MaterialFirma). Errores: FormatoInvalido, ContrasenaIncorrecta.
Resultado<BufferSecreto, ErrorSecretStore> descifrarLlavePkcs8(const BufferSecreto& llaveCifradaDer,
                                                              const BufferSecreto& contrasena);

// Vigencia local: notBefore <= ahora < notAfter.
EstadoCredencial estadoPorVigencia(const MetadataCredencial& metadata, const QDateTime& ahoraUtc);

} // namespace satcfdi::crypto
