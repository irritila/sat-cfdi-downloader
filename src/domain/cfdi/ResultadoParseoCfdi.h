#pragma once

#include "domain/cfdi/DetalleCfdi.h"
#include "domain/cfdi/ResumenCfdi.h"

#include <optional>

namespace satcfdi {

// Causa tipada de un XML Ilegible. Es segura para logs (no lleva contenido).
enum class CausaIlegible {
    MalFormado,     // error de QXmlStreamReader, documento vacio o incompleto
    Doctype,        // contiene <!DOCTYPE (incluye "billion laughs")
    Codificacion,   // bytes no UTF-8, NUL, BOM UTF-16/32 o declaracion distinta de UTF-8
    Profundidad,    // anidamiento > 64 elementos
    ExcedeLimites,  // mas bytes o elementos que los limites del parser
};

// Resultado de `parsearCfdi`. El estado vive en `resumen.estado`.
// Ilegible/NoEsCfdi: resumen y detalle vacios (no se conservan datos
// parciales de un XML rechazado) y `causaIlegible` solo con Ilegible.
// VersionNoSoportada: solo los datos minimos del resumen.
struct ResultadoParseoCfdi {
    ResumenCfdi resumen;
    DetalleCfdi detalle;
    std::optional<CausaIlegible> causaIlegible;

    EstadoParseo estado() const noexcept { return resumen.estado; }
};

} // namespace satcfdi
