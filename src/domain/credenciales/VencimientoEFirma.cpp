#include "domain/credenciales/VencimientoEFirma.h"

namespace satcfdi::vencimientoefirma {

std::optional<int> diasParaVencer(const QDateTime& vigenteHasta, const QDateTime& ahoraUtc)
{
    if (!vigenteHasta.isValid() || !ahoraUtc.isValid() || vigenteHasta <= ahoraUtc) {
        return std::nullopt;
    }
    const qint64 dias = ahoraUtc.secsTo(vigenteHasta) / 86400;
    if (dias > kUmbralAviso) {
        return std::nullopt;
    }
    return static_cast<int>(dias);
}

int umbralPara(int dias)
{
    return dias <= kUmbralUrgente ? kUmbralUrgente : kUmbralAviso;
}

} // namespace satcfdi::vencimientoefirma
