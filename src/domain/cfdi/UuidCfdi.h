#pragma once

#include <QString>
#include <QStringView>

#include <optional>

namespace satcfdi::cfdi {

// UUID (folio fiscal) del TimbreFiscalDigital: 36 caracteres, 8-4-4-4-12
// hexadecimales separados por guion, en mayusculas o minusculas. No exige
// version/variante RFC 4122 (el Anexo 20 solo fija el patron). Distinto de
// `uuid::esCanonico`, que valida los identificadores locales en minusculas.
bool esUuidValido(QStringView texto);

// Valida (sin recortar espacios) y devuelve la forma normalizada en
// MAYUSCULAS, que es como se compara y se indexa. nullopt si no es valido.
std::optional<QString> normalizarUuid(QStringView texto);

} // namespace satcfdi::cfdi
