#include "domain/common/Rfc.h"

namespace satcfdi::rfc {

namespace {

bool esLetraRfc(QChar c)
{
    return (c >= u'A' && c <= u'Z') || c == u'&' || c == QChar(0x00D1); // Ñ
}

bool esDigito(QChar c)
{
    return c >= u'0' && c <= u'9';
}

} // namespace

QString normalizar(QStringView texto)
{
    QString resultado;
    resultado.reserve(texto.size());
    for (const QChar c : texto) {
        if (!c.isSpace()) {
            resultado.append(c);
        }
    }
    return resultado.toUpper();
}

bool esValido(QStringView rfc)
{
    const qsizetype letras = rfc.size() - 9;
    if (letras != 3 && letras != 4) {
        return false;
    }
    for (qsizetype i = 0; i < rfc.size(); ++i) {
        const QChar c = rfc.at(i);
        if (i < letras) {
            if (!esLetraRfc(c)) {
                return false;
            }
        } else if (i < letras + 6) {
            if (!esDigito(c)) {
                return false;
            }
        } else if (!((c >= u'A' && c <= u'Z') || esDigito(c))) {
            return false;
        }
    }
    return true;
}

std::optional<QString> normalizarYValidar(QStringView texto)
{
    QString n = normalizar(texto);
    if (!esValido(n)) {
        return std::nullopt;
    }
    return n;
}

} // namespace satcfdi::rfc
