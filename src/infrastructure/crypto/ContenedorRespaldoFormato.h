#pragma once

// Formato binario AFB1 v1 (T025.1, ADR 0021). Enteros big-endian.
//
// Cabecera en claro (204 bytes):
//   off  0  magic[4] = "AFB1"
//   off  4  version u8 = 1
//   off  5  suite u8 = 1 (Argon2id + HKDF-SHA256 + AES-256-GCM)
//   off  6  reservado u16 = 0
//   off  8  idInstalacion[16] (UUID RFC 4122, D17)
//   off 24  idRespaldo[16] (aleatorio)
//   off 40  creadoEnMs i64 (UTC, ms desde epoch)
//   off 48  argon2 memoriaKib u32 | iteraciones u32 | paralelismo u32
//   off 60  sal[16]
//   off 76  tamanoBloque u32 = 1 MiB
//   off 80  prefijoNonce[4] (aleatorio)
//   off 84  envoltura contrasena:   nonce[12] | DEK cifrada[32] | tag[16]
//   off 144 envoltura recuperacion: nonce[12] | DEK cifrada[32] | tag[16]
//
// Envolturas: AES-256-GCM con la KEK; nonce aleatorio; AAD = cabecera[0..84) |
// rol u8 (1 contrasena, 2 recuperacion). La DEK es aleatoria por respaldo.
//
// Datos: flujo logico de registros partido en bloques de 1 MiB de texto
// plano. Cada bloque en disco lleva un marco explicito (C1):
//   largo u32 | final u8 | ciphertext[largo] | tag[16]
// Un bloque no final mide exactamente 1 MiB; el final mide 1..1 MiB y es el
// unico con final=1; tras el debe venir el fin de archivo real. Bloque i:
// AES-256-GCM(DEK), nonce = prefijoNonce | i u64, AAD = cabecera[0..204) |
// i u64 | largo u32 | final u8. Asi un bloque alterado, reordenado, repetido,
// un marco cambiado o un archivo truncado no autentican, y el lector nunca
// deduce el final del dispositivo (atEnd).
//
// Registros del flujo logico (en este orden):
//   1 METADATOS  u32 n | bytes[n]                       (exactamente uno, primero)
//   2 ENTRADA    clase u8 (0 = archivo regular) | u16 nRuta | rutaUtf8 |
//                u64 tamano | datos[tamano] | sha256[32]
//   3 FALTANTE   u16 nRuta | rutaUtf8
//   4 MANIFIESTO u32 cuenta | cuenta x (estado u8 (0 presente, 1 faltante) |
//                u16 nRuta | rutaUtf8 | u64 tamano | sha256[32]) (ultimo; despues
//                termina el flujo)
// El manifiesto debe repetir exactamente las entradas y faltantes vistos.
// Limites: respaldo::kMaxEntradas y respaldo::kMaxBytesManifiesto (filas).
//
// Este header es INTERNO de infraestructura. Las funciones de abajo existen
// para que las pruebas de formato fabriquen flujos hostiles con cifrado valido
// (rutas `..`, absolutas, clases de entrada no regulares, manifiestos
// incoherentes); el codigo de produccion usa ContenedorRespaldoOpenSsl.

#include "ports/respaldo/ContenedorRespaldo.h"

#include <QByteArray>
#include <QList>

#include <cstddef>
#include <cstdint>

QT_BEGIN_NAMESPACE
class QIODevice;
QT_END_NAMESPACE

namespace satcfdi::crypto::afb1 {

inline constexpr std::uint8_t kMagic[4] = {'A', 'F', 'B', '1'};
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::uint8_t kSuite = 1;
inline constexpr std::size_t kTamanoNonce = 12;
inline constexpr std::size_t kTamanoTag = 16;
inline constexpr std::size_t kTamanoDek = 32;
inline constexpr std::size_t kTamanoPrefijoNonce = 4;
inline constexpr std::size_t kTamanoEnvoltura = kTamanoNonce + kTamanoDek + kTamanoTag;
inline constexpr std::size_t kOffsetEnvolturas = 84;
inline constexpr std::size_t kTamanoCabecera = kOffsetEnvolturas + 2 * kTamanoEnvoltura; // 204
inline constexpr std::size_t kTamanoMarco = 4 + 1;
inline constexpr std::size_t kTamanoBloqueCifrado = kTamanoMarco + respaldo::kTamanoBloque + kTamanoTag;

inline constexpr std::uint8_t kRegistroMetadatos = 1;
inline constexpr std::uint8_t kRegistroEntrada = 2;
inline constexpr std::uint8_t kRegistroFaltante = 3;
inline constexpr std::uint8_t kRegistroManifiesto = 4;
inline constexpr std::uint8_t kClaseArchivoRegular = 0;

// Serializadores de registros. La ruta va como bytes crudos para poder probar
// UTF-8 invalido y rutas hostiles.
QByteArray registroMetadatos(const QByteArray& metadatos);
QByteArray registroEntrada(std::uint8_t clase, const QByteArray& rutaUtf8, const QByteArray& datos,
                           const QByteArray& sha256);
QByteArray registroFaltante(const QByteArray& rutaUtf8);

struct FilaManifiestoCruda {
    std::uint8_t estado = 0;
    QByteArray rutaUtf8;
    std::uint64_t tamano = 0;
    QByteArray sha256; // 32 bytes (ceros si faltante)
};
QByteArray registroManifiesto(const QList<FilaManifiestoCruda>& filas);

QByteArray sha256(const QByteArray& datos);

// Escribe una cabecera valida (DEK nueva envuelta por ambas KEK) y
// `flujoLogico` cifrado en bloques, sin validar su contenido.
Resultado<Exito, respaldo::ErrorRespaldo> escribirFlujoCrudo(QIODevice& destino,
                                                             const respaldo::KekRespaldo& kekContrasena,
                                                             const respaldo::KekRespaldo& kekRecuperacion,
                                                             const respaldo::DatosNuevoRespaldo& datos,
                                                             const QByteArray& flujoLogico);

} // namespace satcfdi::crypto::afb1
