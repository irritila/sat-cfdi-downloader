#pragma once

#include "domain/perfiles/PerfilId.h"

#include <QDateTime>
#include <QString>

#include <optional>

namespace satcfdi {

// Registros persistibles de perfil_sat (sqlite-physical-model.md). `rfc` va
// normalizado (domain/common/Rfc.h). Timestamps UTC.

// INSERT de perfil. Sin credencial (credencial_sat es T005).
struct NuevoPerfilSat {
    PerfilId id;              // id
    QString rfc;              // rfc (normalizado y valido)
    QString nombre;           // nombre (trim no vacio)
    bool activo = true;       // activo
    QDateTime creadoEn;       // creado_en
    QDateTime actualizadoEn;  // actualizado_en (= creadoEn al insertar)
};

struct PerfilSat {
    PerfilId id;                          // id
    QString rfc;                          // rfc
    QString nombre;                       // nombre
    bool activo = true;                   // activo
    QDateTime creadoEn;                   // creado_en
    QDateTime actualizadoEn;              // actualizado_en
    std::optional<QDateTime> eliminadoEn; // eliminado_en
};

} // namespace satcfdi
