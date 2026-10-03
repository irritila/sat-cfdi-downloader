#include "domain/perfiles/PerfilId.h"

#include "domain/common/UuidCanonico.h"

namespace satcfdi {

std::optional<PerfilId> PerfilId::desdeTexto(QStringView texto)
{
    if (!uuid::esCanonico(texto)) {
        return std::nullopt;
    }
    return PerfilId(texto.toString());
}

PerfilId PerfilId::generar()
{
    return PerfilId(uuid::generarCanonico());
}

} // namespace satcfdi
