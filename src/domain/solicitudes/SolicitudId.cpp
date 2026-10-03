#include "domain/solicitudes/SolicitudId.h"

#include "domain/common/UuidCanonico.h"

namespace satcfdi {

std::optional<SolicitudId> SolicitudId::desdeTexto(QStringView texto)
{
    if (!uuid::esCanonico(texto)) {
        return std::nullopt;
    }
    return SolicitudId(texto.toString());
}

SolicitudId SolicitudId::generar()
{
    return SolicitudId(uuid::generarCanonico());
}

} // namespace satcfdi
