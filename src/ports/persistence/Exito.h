#pragma once

namespace satcfdi {

// Valor de exito sin datos para operaciones de escritura de los puertos de
// persistencia: Resultado<Exito, ErrorPersistencia>.
struct Exito {
    friend constexpr bool operator==(Exito, Exito) noexcept { return true; }
};

} // namespace satcfdi
