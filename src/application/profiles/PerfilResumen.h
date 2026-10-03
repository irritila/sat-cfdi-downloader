#pragma once

#include "domain/perfiles/PerfilId.h"

#include <QString>

namespace satcfdi {

// Vista de solo lectura de un perfil SAT no eliminado (T005.1, DA1). Sin
// estado de credencial (ver PerfilConPreparacion). `nombre` = perfil_sat.nombre.
struct PerfilResumen {
    PerfilId id;
    QString rfc;    // normalizado, inmutable tras crear
    QString nombre; // descriptivo, editable
    bool activo = true;

    friend bool operator==(const PerfilResumen&, const PerfilResumen&) = default;
};

} // namespace satcfdi
