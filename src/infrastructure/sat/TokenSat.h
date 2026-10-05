#pragma once

#include "ports/sat/TokenSat.h"

namespace satcfdi::sat {

// T009: el token vive en ports (ports/sat/TokenSat.h) para que SatGateway lo
// exponga sin depender de infraestructura. Alias para el codigo de T006 (CLI,
// cliente HTTP, parser y pruebas).
using TokenSat = satcfdi::TokenSat;

} // namespace satcfdi::sat
