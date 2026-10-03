#include "domain/common/UuidCanonico.h"

#include <QUuid>

namespace satcfdi::uuid {

bool esCanonico(QStringView texto)
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
        const bool digito = c >= u'0' && c <= u'9';
        const bool hexMinuscula = c >= u'a' && c <= u'f';
        if (!digito && !hexMinuscula) {
            return false;
        }
    }
    return true;
}

QString generarCanonico()
{
    // QUuid::WithoutBraces produce hexadecimal en minusculas.
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace satcfdi::uuid
