#include "domain/solicitudes/DedupKey.h"

#include <QCryptographicHash>

namespace satcfdi {

namespace {

bool esHexMinuscula(QStringView texto)
{
    for (const QChar c : texto) {
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'))) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<DedupKey> DedupKey::desdeTexto(QStringView texto)
{
    const qsizetype separador = texto.indexOf(u':');
    if (texto.size() < 4 || texto.at(0) != u'v' || separador < 2
        || separador == texto.size() - 1) {
        return std::nullopt;
    }
    const QStringView digitos = texto.sliced(1, separador - 1);
    if (digitos.at(0) == u'0' || digitos.size() > 6) {
        return std::nullopt;
    }
    int version = 0;
    for (const QChar c : digitos) {
        if (c < u'0' || c > u'9') {
            return std::nullopt;
        }
        version = version * 10 + (c.unicode() - u'0');
    }
    const QStringView cuerpo = texto.sliced(separador + 1);
    if (version == 1 && (cuerpo.size() != 64 || !esHexMinuscula(cuerpo))) {
        return std::nullopt;
    }
    return DedupKey(version, texto.toString());
}

DedupKey DedupKey::calcularV1(QByteArrayView serializacionCanonica)
{
    const QByteArray hash =
        QCryptographicHash::hash(serializacionCanonica, QCryptographicHash::Sha256).toHex();
    return DedupKey(1, QStringLiteral("v1:") + QString::fromLatin1(hash));
}

} // namespace satcfdi
