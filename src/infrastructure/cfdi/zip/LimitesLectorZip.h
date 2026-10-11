#pragma once

// Limites del lector de paquetes ZIP de CFDI (T015 D4). UNICO lugar donde se
// definen sus valores productivos. Las pruebas pueden inyectar limites
// reducidos en LectorZipLibzip; la app usa siempre `productivos()`.

#include <QtGlobal>

namespace satcfdi {

struct LimitesLectorZip {
    static constexpr qint64 kMiB = qint64{1024} * 1024;

    // Por entrada (se omite con su motivo y el resto continua).
    qint64 maxBytesEntrada = 16 * kMiB;    // descomprimido, bytes reales
    qint64 razonCompresionMax = 200;       // descomprimido / comprimido
    qint64 umbralRazonBytes = 1 * kMiB;    // la razon solo aplica por encima de esto

    // Por ZIP (error del paquete completo).
    qint64 maxBytesTotal = 1024 * kMiB;    // descomprimido acumulado, bytes reales
    qint64 maxEntradas = 20000;

    // Tamano del trozo de lectura (no es un limite de seguridad).
    qint64 bytesPorTrozo = 64 * 1024;

    static constexpr LimitesLectorZip productivos() { return LimitesLectorZip{}; }
};

} // namespace satcfdi
