#include "infrastructure/sat/EnmascaradorEvidencia.h"

#include <QByteArray>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace satcfdi::sat::evidencia {

namespace {

bool yaEnmascarado(QStringView valor)
{
    return valor.startsWith(u'[') && valor.endsWith(u']');
}

// Claves cuyo VALOR se enmascara por contexto, sin importar su formato.
bool esClaveDeContexto(QStringView clave)
{
    for (QStringView k : {u"IdSolicitud", u"IdPaquete", u"IdsPaquetes", u"RfcSolicitante", u"RfcEmisor",
                          u"RfcReceptor", u"RfcReceptores", u"RfcACuentaTerceros"}) {
        if (clave.compare(k, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString marcadorContexto(QStringView clave)
{
    if (clave.compare(u"IdSolicitud", Qt::CaseInsensitive) == 0) {
        return QStringLiteral("[IdSolicitud]");
    }
    if (clave.startsWith(u"Id", Qt::CaseInsensitive)) {
        return QStringLiteral("[IdPaquete]");
    }
    return QStringLiteral("[RFC]");
}

QString marcadorElemento(const QString& nombre, const QString& contenido)
{
    if (nombre == u"AutenticaResult") {
        return QStringLiteral("[token]");
    }
    if (nombre == u"BinarySecurityToken" || nombre == u"X509Certificate") {
        return QStringLiteral("[certificado]");
    }
    if (nombre == u"SignatureValue") {
        return QStringLiteral("[firma]");
    }
    if (nombre == u"DigestValue") {
        return QStringLiteral("[digest]");
    }
    if (nombre == u"X509IssuerName") {
        return QStringLiteral("[issuer]");
    }
    if (nombre == u"X509SerialNumber") {
        return QStringLiteral("[serial]");
    }
    if (esClaveDeContexto(nombre)) {
        return marcadorContexto(nombre);
    }
    // Paquete: solo el tamano decodificado.
    const QByteArray b64 = contenido.trimmed().toLatin1();
    const qsizetype bytes = QByteArray::fromBase64(b64).size();
    return QStringLiteral("[paquete:%1 bytes]").arg(bytes);
}

} // namespace

QString enmascarar(QString texto)
{
    // 1. Contenido de elementos sensibles (cualquier prefijo).
    static const QRegularExpression kElementos(QStringLiteral(
        "<((?:[A-Za-z_][\\w.-]*:)?(AutenticaResult|BinarySecurityToken|SignatureValue|X509Certificate|"
        "DigestValue|X509IssuerName|X509SerialNumber|Paquete|IdsPaquetes|IdSolicitud|IdPaquete|RfcSolicitante|"
        "RfcEmisor|RfcReceptor|RfcACuentaTerceros))(\\s[^>]*)?>([^<]*)</\\1>"));
    {
        QString salida;
        qsizetype ultimo = 0;
        auto it = kElementos.globalMatch(texto);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString contenido = m.captured(4);
            salida += QStringView(texto).mid(ultimo, m.capturedStart(4) - ultimo);
            salida += yaEnmascarado(QStringView(contenido).trimmed()) ? contenido
                                                                      : marcadorElemento(m.captured(2), contenido);
            ultimo = m.capturedEnd(4);
        }
        salida += QStringView(texto).mid(ultimo);
        texto = salida;
    }

    // 2. Por contexto: atributos XML (clave="valor") y lineas de texto
    //    (clave: valor, clave=valor, con o sin comillas), sin importar el
    //    formato del valor. Las claves de lista aceptan valores separados
    //    por comas. Valores vacios o ya enmascarados ([...]) se respetan.
    static const QRegularExpression kContexto(QStringLiteral(
        "(?<![A-Za-z0-9_])(IdsPaquetes|IdSolicitud|IdPaquete|RfcSolicitante|RfcEmisor|RfcReceptores|RfcReceptor|"
        "RfcACuentaTerceros)\\s*[:=]\\s*(?:\"([^\"]*)\"|'([^']*)'|((?!\\[)[^\\s\"'|;,<>]+"
        "(?:\\s*,\\s*(?![A-Za-z]+\\s*[:=])[^\\s\"'|;,<>]+)*))"),
        QRegularExpression::CaseInsensitiveOption);
    {
        QString salida;
        qsizetype ultimo = 0;
        auto it = kContexto.globalMatch(texto);
        while (it.hasNext()) {
            const auto m = it.next();
            int grupo = 2;
            while (grupo <= 4 && m.capturedStart(grupo) < 0) {
                ++grupo;
            }
            if (grupo > 4) {
                continue;
            }
            const QStringView valor = m.capturedView(grupo);
            salida += QStringView(texto).mid(ultimo, m.capturedStart(grupo) - ultimo);
            salida += (valor.isEmpty() || yaEnmascarado(valor)) ? valor.toString() : marcadorContexto(m.capturedView(1));
            ultimo = m.capturedEnd(grupo);
        }
        salida += QStringView(texto).mid(ultimo);
        texto = salida;
    }

    // 3. Header WRAP.
    static const QRegularExpression kWrap(QStringLiteral("WRAP\\s+access_token=\"[^\"]*\""));
    texto.replace(kWrap, QStringLiteral("WRAP access_token=\"[token]\""));
    static const QRegularExpression kWrapSinComillas(QStringLiteral("WRAP\\s+access_token=(?!\")[^\\s,]+"));
    texto.replace(kWrapSinComillas, QStringLiteral("WRAP access_token=\"[token]\""));

    // 4. Patrones libres.
    static const QRegularExpression kBase64(QStringLiteral("[A-Za-z0-9+/]{200,}={0,2}"));
    {
        QString salida;
        qsizetype ultimo = 0;
        auto it = kBase64.globalMatch(texto);
        while (it.hasNext()) {
            const auto m = it.next();
            salida += QStringView(texto).mid(ultimo, m.capturedStart(0) - ultimo);
            salida += QStringLiteral("[base64:%1]").arg(m.capturedLength(0));
            ultimo = m.capturedEnd(0);
        }
        salida += QStringView(texto).mid(ultimo);
        texto = salida;
    }
    static const QRegularExpression kUuid(QStringLiteral(
        "\\b[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}(?:_[0-9]+)?\\b"));
    texto.replace(kUuid, QStringLiteral("[Id]"));
    static const QRegularExpression kRfc(QStringLiteral("(?<![A-Za-z0-9&Ñ])[A-ZÑ&]{3,4}[0-9]{6}[A-Z0-9]{3}(?![A-Za-z0-9])"));
    texto.replace(kRfc, QStringLiteral("[RFC]"));
    static const QRegularExpression kSerial(QStringLiteral("(?<![0-9])[0-9]{30,}(?![0-9])"));
    texto.replace(kSerial, QStringLiteral("[serial]"));
    return texto;
}

QString enmascararId(QStringView id)
{
    return id.trimmed().isEmpty() ? QString() : QStringLiteral("[Id]");
}

QString ocultarValores(QString texto, QStringList valores, QStringView marcador)
{
    // Mas largos primero: un valor contenido en otro no deja restos.
    valores.removeIf([](const QString& v) { return v.trimmed().isEmpty(); });
    std::sort(valores.begin(), valores.end(),
              [](const QString& a, const QString& b) { return a.size() > b.size(); });
    for (const QString& v : std::as_const(valores)) {
        texto.replace(v, marcador.toString());
    }
    return texto;
}

QString rfcParaConsola(QStringView rfc)
{
    if (rfc.size() <= 3) {
        return QString(rfc.size(), QLatin1Char('*'));
    }
    return rfc.left(2).toString() + QString(rfc.size() - 3, QLatin1Char('*')) + rfc.right(1).toString();
}

} // namespace satcfdi::sat::evidencia
