#pragma once

#include <QString>

namespace satcfdi {

// Entrada de "Crear perfil simulado" (T003). Sin credenciales.
struct NuevoPerfilSimuladoRequest {
    QString rfc;         // se normaliza (trim, mayusculas, sin espacios)
    QString razonSocial; // perfil_sat.nombre; trim no vacio
    bool activo = true;
};

} // namespace satcfdi
