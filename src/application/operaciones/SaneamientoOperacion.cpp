#include "application/operaciones/SaneamientoOperacion.h"

#include "application/logging/RegexLogSanitizer.h"

#include <QRegularExpression>

namespace satcfdi::saneamiento {

namespace {

const QRegularExpression& formatoCodigo()
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9:._-]{1,40}$"));
    return re;
}

// RFC (persona moral 3 letras o fisica 4) en cualquier posicion.
const QRegularExpression& rfc()
{
    static const QRegularExpression re(QStringLiteral("[A-ZÑ&]{3,4}[0-9]{6}[A-Z0-9]{3}"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Corrida opaca: 40+ caracteres de alfabeto token/base64/url.
const QRegularExpression& corridaOpaca()
{
    static const QRegularExpression re(QStringLiteral("[A-Za-z0-9+/=_.~-]{40,}"));
    return re;
}

bool esHexConGuiones(QStringView s)
{
    for (const QChar c : s) {
        const bool hex = (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
        if (!hex && c != u'-' && c != u'_') {
            return false;
        }
    }
    return true;
}

} // namespace

QString codigoSeguro(QStringView codigo)
{
    const QString limpio = codigo.trimmed().toString();
    if (!formatoCodigo().match(limpio).hasMatch() || rfc().match(limpio).hasMatch()) {
        return kCodigoNoSeguro;
    }
    // Con ':' solo se acepta un prefijo conocido (faultcode SOAP o clave local
    // de credencial); un prefijo arbitrario podria ser un secreto.
    const qsizetype dosPuntos = limpio.indexOf(u':');
    if (dosPuntos >= 0) {
        static const QStringList kPrefijos = {QStringLiteral("s"),    QStringLiteral("soap"), QStringLiteral("a"),
                                              QStringLiteral("wsse"), QStringLiteral("wsu"),
                                              QStringLiteral("credencial")};
        const QString local = limpio.mid(dosPuntos + 1);
        if (!kPrefijos.contains(limpio.left(dosPuntos)) || local.isEmpty() || local.contains(u':')) {
            return kCodigoNoSeguro;
        }
    }
    return limpio;
}

std::optional<QString> codigoSeguro(const std::optional<QString>& codigo)
{
    if (!codigo) {
        return std::nullopt;
    }
    return codigoSeguro(QStringView(*codigo));
}

QString textoSeguro(QStringView texto, qsizetype limite)
{
    QString t = RegexLogSanitizer::sanearTexto(texto, RegexLogSanitizer::kLimiteEntrada).texto;
    t.replace(rfc(), QStringLiteral("[RFC]"));
    QString salida;
    qsizetype desde = 0;
    auto it = corridaOpaca().globalMatch(t);
    while (it.hasNext()) {
        const auto m = it.next();
        salida += QStringView(t).mid(desde, m.capturedStart() - desde);
        salida += esHexConGuiones(m.capturedView()) ? m.captured() : QStringLiteral("[REDACTED:token]");
        desde = m.capturedEnd();
    }
    salida += QStringView(t).mid(desde);
    return RegexLogSanitizer::sanearTexto(salida, limite).texto;
}

FallaOperacion fallaSegura(FallaOperacion f)
{
    f.codigo = codigoSeguro(f.codigo);
    f.diagnosticoSanitizado = textoSeguro(f.diagnosticoSanitizado);
    return f;
}

ResultadoEnvio envioSeguro(ResultadoEnvio r)
{
    r.codEstatus = codigoSeguro(QStringView(r.codEstatus));
    r.mensaje = textoSeguro(r.mensaje);
    if (r.idSolicitudSat && codigoSeguro(QStringView(*r.idSolicitudSat)) == kCodigoNoSeguro) {
        r.idSolicitudSat.reset(); // un IdSolicitud no persistible no se acepta (EnvioIncierto)
    }
    return r;
}

ResultadoVerificacion verificacionSegura(ResultadoVerificacion r)
{
    r.codEstatus = codigoSeguro(QStringView(r.codEstatus));
    r.mensaje = textoSeguro(r.mensaje);
    r.codigoEstadoSolicitud = codigoSeguro(r.codigoEstadoSolicitud);
    return r;
}

ResultadoDescarga descargaSegura(ResultadoDescarga r)
{
    r.codEstatus = codigoSeguro(QStringView(r.codEstatus));
    r.mensaje = textoSeguro(r.mensaje);
    if (r.advertenciaDurabilidad) {
        r.advertenciaDurabilidad = textoSeguro(*r.advertenciaDurabilidad);
    }
    return r;
}

} // namespace satcfdi::saneamiento
