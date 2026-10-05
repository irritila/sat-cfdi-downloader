#include "infrastructure/sat/SaneadoRespuestaSat.h"

#include "infrastructure/sat/EnmascaradorEvidencia.h"

#include <QRegularExpression>
#include <QSet>

namespace satcfdi::sat::saneado {

namespace {

constexpr qsizetype kMaxMensaje = 200;

} // namespace

bool esCodigoSatDocumentado(QStringView codigo)
{
    static const QSet<QString> kDocumentados = {
        QStringLiteral("300"),  QStringLiteral("301"),  QStringLiteral("302"),  QStringLiteral("303"),
        QStringLiteral("304"),  QStringLiteral("305"),  QStringLiteral("404"),  QStringLiteral("5000"),
        QStringLiteral("5001"), QStringLiteral("5002"), QStringLiteral("5003"), QStringLiteral("5004"),
        QStringLiteral("5005"), QStringLiteral("5006"), QStringLiteral("5007"), QStringLiteral("5008"),
        QStringLiteral("5009"), QStringLiteral("5010"), QStringLiteral("5011"), QStringLiteral("5012"),
    };
    return kDocumentados.contains(codigo.trimmed().toString());
}

QString codigoSatPermitido(QStringView codigo)
{
    return esCodigoSatDocumentado(codigo) ? codigo.trimmed().toString() : kCodigoNoReconocido;
}

QString faultcodePermitido(QStringView faultcode)
{
    static const QSet<QString> kLocales = {
        QStringLiteral("Client"),
        QStringLiteral("Server"),
        QStringLiteral("VersionMismatch"),
        QStringLiteral("MustUnderstand"),
        QStringLiteral("Sender"),
        QStringLiteral("Receiver"),
        QStringLiteral("InvalidSecurity"),
        QStringLiteral("InvalidSecurityToken"),
        QStringLiteral("FailedAuthentication"),
        QStringLiteral("FailedCheck"),
        QStringLiteral("SecurityTokenUnavailable"),
        QStringLiteral("UnsupportedSecurityToken"),
        QStringLiteral("UnsupportedAlgorithm"),
        QStringLiteral("MessageExpired"),
        QStringLiteral("ActionNotSupported"),
        QStringLiteral("InternalServiceFault"),
        QStringLiteral("DestinationUnreachable"),
    };
    static const QRegularExpression kQName(QStringLiteral("^(?:([A-Za-z][A-Za-z0-9]{0,15}):)?([A-Za-z]{1,40})$"));
    const QStringView limpio = faultcode.trimmed();
    if (esCodigoSatDocumentado(limpio)) {
        return limpio.toString();
    }
    // El prefijo QName nunca se emite (podria transportar datos): solo el
    // nombre local permitido.
    const auto m = kQName.matchView(limpio);
    if (m.hasMatch() && kLocales.contains(m.captured(2))) {
        return m.captured(2);
    }
    return kFaultNoReconocido;
}

QString mensajePermitido(QStringView mensaje)
{
    QString texto;
    texto.reserve(qMin(mensaje.size(), qsizetype(4096)));
    for (const QChar c : mensaje.left(4096)) {
        texto.append(c.isPrint() || c == QLatin1Char(' ') ? c : QLatin1Char(' '));
    }
    texto = evidencia::enmascarar(texto.simplified());
    // Tokens y blobs opacos que no tengan un patron reconocible.
    static const QRegularExpression kOpaco(QStringLiteral("[A-Za-z0-9+/=._~-]{32,}"));
    texto.replace(kOpaco, QStringLiteral("[oculto]"));
    if (texto.size() > kMaxMensaje) {
        texto.truncate(kMaxMensaje);
    }
    return texto;
}

bool esIdSolicitud(QStringView id)
{
    static const QRegularExpression kUuid(QStringLiteral(
        "^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"));
    return kUuid.matchView(id).hasMatch();
}

bool esIdPaquete(QStringView id)
{
    static const QRegularExpression kPaquete(QStringLiteral(
        "^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}_[0-9]{1,4}$"));
    return kPaquete.matchView(id).hasMatch();
}

bool esEndpointOficial(const QUrl& url)
{
    const QString host = url.host().toLower();
    return url.isValid() && url.scheme() == QStringLiteral("https") && url.userInfo().isEmpty()
           && (url.port() == -1 || url.port() == 443) && host.endsWith(QStringLiteral(".clouda.sat.gob.mx"));
}

} // namespace satcfdi::sat::saneado
