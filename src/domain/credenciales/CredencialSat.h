#pragma once

#include "domain/perfiles/PerfilId.h"

#include <QDateTime>
#include <QString>

#include <optional>

namespace satcfdi {

// Registro persistible de credencial_sat (T001 + migracion 002, T005).
// Relacion perfil 1 a 0..1; el reemplazo actualiza la misma fila (conserva
// `id` y `registradaEn`) y el borrado es fisico.
//
// Las referencias son textos OPACOS `scs1:<uuid>:<rol>` (ver
// ports/secrets/SecretStoreTypes.h, CredencialRef); nunca rutas ni secretos.
// La metadata no secreta es anulable solo para filas previas a 002; toda
// escritura nueva la exige completa (el repositorio rechaza lo contrario).
struct CredencialSat {
    QString id;                               // id (UUID canonico de la fila)
    PerfilId perfilSatId;                     // perfil_sat_id
    QString certificadoRef;                   // certificado_ref
    QString llavePrivadaRef;                  // llave_privada_ref
    QString contrasenaRef;                    // contrasena_ref
    std::optional<QString> numeroSerie;       // numero_serie (002)
    std::optional<QDateTime> vigenteDesde;    // vigente_desde (002), UTC
    std::optional<QDateTime> vigenteHasta;    // vigente_hasta (002), UTC
    QDateTime registradaEn;                   // registrada_en
    QDateTime actualizadaEn;                  // actualizada_en

    bool metadataCompleta() const
    {
        return numeroSerie && !numeroSerie->trimmed().isEmpty() && vigenteDesde
               && vigenteDesde->isValid() && vigenteHasta && vigenteHasta->isValid();
    }

    friend bool operator==(const CredencialSat&, const CredencialSat&) = default;
};

} // namespace satcfdi
