#include "domain/cfdi/UuidCfdi.h"

namespace satcfdi::cfdi {

bool esUuidValido(QStringView texto)
{
    if (texto.size() != 36) {
        return false;
    }
    for (qsizetype i = 0; i < texto.size(); ++i) {
        const QChar c = texto.at(i);
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != u'-') {
                return false;
            }
            continue;
        }
        const bool hex = (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
        if (!hex) {
            return false;
        }
    }
    return true;
}

std::optional<QString> normalizarUuid(QStringView texto)
{
    if (!esUuidValido(texto)) {
        return std::nullopt;
    }
    return texto.toString().toUpper();
}

} // namespace satcfdi::cfdi
