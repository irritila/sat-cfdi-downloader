#pragma once

#include "domain/cfdi/ResultadoParseoCfdi.h"

#include <QByteArrayView>

namespace satcfdi {

// Limites del parser (T015 D5, T015.1). Constantes en un solo lugar.
struct LimitesParserCfdi {
    // Tamano maximo del XML; coincide con el limite por entrada del lector ZIP.
    static constexpr qsizetype kMaxBytes = qsizetype(16) * 1024 * 1024;
    // Profundidad maxima de anidamiento (la raiz es el nivel 1).
    static constexpr int kProfundidadMaxima = 64;
    // Elementos totales del documento; si se excede -> Ilegible(ExcedeLimites).
    // 16 MiB / ~64 bytes por elemento minimo; un CFDI real tiene cientos.
    static constexpr int kMaxElementos = 250'000;
    // Listas del detalle: al excederse se dejan de guardar, se cuentan en
    // DetalleCfdi::elementosOmitidosPorLimite y el estado queda Parcial.
    static constexpr int kMaxConceptos = 20'000;
    static constexpr int kMaxPagos = 5'000;
    static constexpr int kMaxDocumentosPago = 20'000; // suma de todos los pagos
    static constexpr int kMaxRelacionados = 5'000;
    static constexpr int kMaxImpuestos = 1'000;       // traslados + retenciones
    // Complementos distintos nombrados y campos invalidos registrados.
    static constexpr int kMaxComplementosNombrados = 32;
    static constexpr int kMaxCamposInvalidos = 64;
};

// Version del parser (T015.2, T015 D2): se guarda con el indice de cada
// paquete; al cambiarla, los paquetes indexados con otra version se
// reindexan al arrancar. Incrementarla cuando cambie lo que produce el resumen.
inline constexpr int kVersionParserCfdi = 1;

// Parsea un CFDI con QXmlStreamReader. Funcion pura y reentrante: sin estado
// global, sin E/S y sin logs (D11); segura desde cualquier hilo.
//
//  - Rechaza (Ilegible) antes de tokenizar: entrada vacia o > kMaxBytes,
//    BOM distinto de UTF-8, bytes no UTF-8 o NUL, cualquier "<!DOCTYPE"
//    (sin distinguir mayusculas, en cualquier parte: antes de que se procese
//    una sola entidad). Despues: declaracion `encoding` distinta de UTF-8,
//    profundidad > 64, > kMaxElementos, o error de buena formacion.
//  - Raiz distinta de Comprobante en cfd/4 o cfd/3 -> NoEsCfdi.
//  - 4.0 (cfd/4, Version="4.0") y 3.3 (cfd/3, Version="3.3") -> Completo o
//    Parcial. Otra version (3.2 con `version` en minuscula) ->
//    VersionNoSoportada con datos minimos.
ResultadoParseoCfdi parsearCfdi(QByteArrayView xml);

} // namespace satcfdi
