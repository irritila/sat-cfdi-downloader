#pragma once

// Contenedor cifrado autenticado v1 de una generacion de credencial (T005,
// DS3). Portable, OpenSSL 3 (AES-256-GCM + RAND_bytes). El header no expone
// tipos de OpenSSL.
//
// Formato binario v1 (enteros big-endian):
//   magic[4] = "SCC1" | version[1] = 1 | alg[1] = 1 (AES-256-GCM)
//   | nonceLen[1] = 12 | nonce[12] | ctLen[4] | ciphertext[ctLen] | tag[16]
// AAD canonica: magic | version | alg | credentialUUID (36 ASCII).
// Clave: 32 bytes aleatorios por generacion (vive en Keychain); nonce
// aleatorio de 12 bytes por cifrado (una sola vez por clave en la practica:
// cada generacion se cifra una vez).
//
// Texto plano (carga util), solo en BufferSecreto:
//   certLen[4] | certificadoDer | llaveLen[4] | llaveCifradaDer (PKCS#8 original)
// La llave se guarda TAL CUAL se importo (cifrada con la contrasena SAT): el
// contenedor agrega una segunda capa, no la sustituye.

#include "domain/common/Resultado.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QByteArray>
#include <QStringView>

#include <cstddef>

namespace satcfdi::crypto {

inline constexpr std::size_t kTamanoClaveContenedor = 32;
inline constexpr std::size_t kTamanoNonce = 12;
inline constexpr std::size_t kTamanoTag = 16;
inline constexpr std::size_t kTamanoCabecera = 4 + 1 + 1 + 1 + kTamanoNonce + 4;
// Limite del ciphertext (carga util: dos DER de <= 64 KiB + 8 bytes).
inline constexpr std::size_t kMaxCargaUtil = 2 * 64 * 1024 + 8;
inline constexpr std::size_t kMaxContenedor = kTamanoCabecera + kMaxCargaUtil + kTamanoTag;

// 32 bytes del CSPRNG de OpenSSL. Error: Interno ("rand").
Resultado<BufferSecreto, ErrorSecretStore> generarClaveContenedor();

// Bytes aleatorios no secretos (p. ej. nombres temporales). Error: Interno.
Resultado<QByteArray, ErrorSecretStore> bytesAleatorios(std::size_t n);

// Empaqueta certificado y llave cifrada en una carga util (BufferSecreto).
Resultado<BufferSecreto, ErrorSecretStore> empaquetarCarga(const BufferSecreto& certificadoDer,
                                                           const BufferSecreto& llaveCifradaDer);

// Inverso de empaquetarCarga. Error: CredencialDanada si las longitudes no
// cuadran.
struct CargaContenedor {
    BufferSecreto certificadoDer;
    BufferSecreto llaveCifradaDer;
};
Resultado<CargaContenedor, ErrorSecretStore> desempaquetarCarga(const BufferSecreto& carga);

// Cifra la carga para la generacion `uuid`. El resultado (cabecera +
// ciphertext + tag) no es secreto y se escribe tal cual al archivo.
// Errores: Interno (OpenSSL/aleatorio), FormatoInvalido (tamanos).
Resultado<QByteArray, ErrorSecretStore> cifrarContenedor(const BufferSecreto& clave, QStringView uuid,
                                                         const BufferSecreto& carga);

// Valida la cabecera (antes de reservar memoria) y descifra/autentica.
// Error: CredencialDanada (formato, version, longitudes, tag o AAD/uuid
// incorrectos, clave de tamano invalido).
Resultado<BufferSecreto, ErrorSecretStore> descifrarContenedor(const BufferSecreto& clave, QStringView uuid,
                                                               const std::uint8_t* datos, std::size_t tamano);

} // namespace satcfdi::crypto
