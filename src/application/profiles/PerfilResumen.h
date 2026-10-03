#pragma once

#include "domain/perfiles/PerfilId.h"

#include <QString>

namespace satcfdi {

// Vista de solo lectura de un perfil SAT para selectores de UI.
struct PerfilResumen {
    PerfilId id;
    QString rfc;
    QString razonSocial;
    bool activo = true;
};

} // namespace satcfdi
