#include "application/logging/RegexLogSanitizer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <functional>
#include <optional>

namespace satcfdi {

using namespace Qt::StringLiterals;

namespace {

using Opciones = QRegularExpression::PatternOptions;

constexpr Opciones kOpciones = QRegularExpression::CaseInsensitiveOption
                               | QRegularExpression::UseUnicodePropertiesOption;
constexpr Opciones kOpcionesBloque = kOpciones | QRegularExpression::DotMatchesEverythingOption;

QString marcador(QStringView categoria)
{
    return u"[REDACTED:"_s + categoria + u']';
}

// Un valor que ya es EXACTAMENTE un marcador del catalogo cerrado no se vuelve
// a redactar (idempotencia). Un prefijo parecido ("[REDACTED:x-secreto") no
// cuenta: se redacta.
bool esMarcadorExacto(QStringView valor)
{
    static const QRegularExpression re(
        uR"(^\[(?:REDACTED:(?:token|password|clave|secret|firma|certificado|pem|paquete|base64:[1-9][0-9]*)|TRUNCATED:[1-9][0-9]*)\]$)"_s);
    return re.matchView(valor).hasMatch();
}

// Reemplazo de una coincidencia: texto nuevo y marcador aplicado, o nullopt
// para conservar la coincidencia tal cual.
struct Reemplazo {
    QString texto;
    QString marcador;
};
using Reemplazador = std::function<std::optional<Reemplazo>(const QRegularExpressionMatch&)>;

QString reemplazar(const QString& texto, const QRegularExpression& re, const Reemplazador& f,
                   QStringList& marcadores)
{
    QString salida;
    qsizetype ultimo = 0;
    bool cambio = false;
    auto it = re.globalMatch(texto);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const std::optional<Reemplazo> r = f(m);
        if (!r) {
            continue;
        }
        salida.append(QStringView(texto).sliced(ultimo, m.capturedStart() - ultimo));
        salida.append(r->texto);
        marcadores.append(r->marcador);
        ultimo = m.capturedEnd();
        cambio = true;
    }
    if (!cambio) {
        return texto;
    }
    salida.append(QStringView(texto).sliced(ultimo));
    return salida;
}

QString reemplazarFijo(const QString& texto, const QRegularExpression& re, const QString& fijo,
                       QStringList& marcadores)
{
    return reemplazar(
        texto, re, [&](const QRegularExpressionMatch&) { return Reemplazo{fijo, fijo}; }, marcadores);
}

// --- Bloques XML sensibles -------------------------------------------------

struct BloqueXml {
    QRegularExpression cerrado;
    QRegularExpression abierto; // sin cierre (texto truncado): hasta el final
    QString marcador;
};

BloqueXml bloque(QStringView etiqueta, QStringView categoria)
{
    // Prefijo de namespace opcional (ds:, s:, etc.), nombre exacto; la
    // etiqueta autocerrada (<Paquete/>) no tiene contenido y se conserva.
    const QString e = etiqueta.toString();
    return {QRegularExpression(uR"(<((?:[\w.-]+:)?)%1(?=[\s/>])[^>]*(?<!/)>.*?</\1%1\s*>)"_s.arg(e),
                               kOpcionesBloque),
            QRegularExpression(uR"(<((?:[\w.-]+:)?)%1(?=[\s/>])[^>]*(?<!/)>.*)"_s.arg(e), kOpcionesBloque),
            marcador(categoria)};
}

const QList<BloqueXml>& bloquesXml()
{
    // Orden: Signature (contiene SignatureValue y X509Certificate) primero.
    static const QList<BloqueXml> bloques = {
        bloque(u"Signature", u"firma"),
        bloque(u"SignatureValue", u"firma"),
        bloque(u"X509Certificate", u"certificado"),
        bloque(u"Paquete", u"paquete"),
    };
    return bloques;
}

// --- PEM ---------------------------------------------------------------------

const QRegularExpression& pemCerrado()
{
    static const QRegularExpression re(uR"(-----BEGIN [A-Z0-9 ]+-----.*?-----END [A-Z0-9 ]+-----)"_s,
                                       kOpcionesBloque);
    return re;
}

const QRegularExpression& pemAbierto()
{
    static const QRegularExpression re(uR"(-----BEGIN [A-Z0-9 ]+-----.*)"_s, kOpcionesBloque);
    return re;
}

// --- Authorization y Bearer --------------------------------------------------

const QRegularExpression& authorization()
{
    // Cabecera completa: todo el valor hasta fin de linea.
    static const QRegularExpression re(
        uR"((?<![\p{L}\p{N}])(authorization)(["']?\s*[:=][ \t]*)([^\r\n]*))"_s, kOpciones);
    return re;
}

const QRegularExpression& bearer()
{
    // Valor: credencial, o algo entre corchetes MAS cualquier sufijo pegado
    // ("[REDACTED:token]S3cr3t" se redacta completo: fail-closed).
    static const QRegularExpression re(
        uR"((?<![\p{L}\p{N}])(bearer)\s+(\[[^\]\r\n]*\][^\s,;"'<>{}()]*|[A-Za-z0-9._~+/=-]+))"_s, kOpciones);
    return re;
}

// --- Pares clave/valor -------------------------------------------------------

const QString& claves()
{
    static const QString c =
        uR"(access[_ -]?token|api[_ -]?key|apikey|token|password|passwd|contrase(?:n|ñ)a|clave|pin|secret)"_s;
    return c;
}

QString categoriaDeClave(QString clave)
{
    clave = clave.toLower();
    if (clave.contains(u"token")) {
        return u"token"_s;
    }
    if (clave.startsWith(u"pass") || clave.startsWith(u"contrase")) {
        return u"password"_s;
    }
    if (clave == u"clave" || clave == u"pin") {
        return u"clave"_s;
    }
    return u"secret"_s; // secret, api key
}

const QRegularExpression& elementoXmlSensible()
{
    // <password>valor</password>, con prefijo opcional.
    static const QRegularExpression re(
        uR"((<((?:[\w.-]+:)?)(%1)(?:\s[^>]*)?>)([^<]*)(</\2\3\s*>))"_s.arg(claves()), kOpciones);
    return re;
}

const QRegularExpression& parClaveValor()
{
    // clave = valor | "clave": "valor" | clave: 'valor' | clave=[valor].
    // Un valor entre comillas o corchetes incluye cualquier sufijo pegado
    // hasta el delimitador: "[REDACTED:token]S3cr3t" no es un marcador exacto
    // y se redacta completo (fail-closed).
    static const QRegularExpression re(
        uR"((?<![\p{L}\p{N}])(["']?)(%1)\1(\s*[:=]\s*)((?:"[^"\r\n]*"|'[^'\r\n]*'|\[[^\]\r\n]*\])[^\s,;&"'<>{}()]*|[^\s,;&"'<>{}()]+))"_s
            .arg(claves()),
        kOpciones);
    return re;
}

// --- base64 huerfano -------------------------------------------------------

const QRegularExpression& base64()
{
    // Corrida maxima (greedy, sin lookahead) mas relleno '=' de cualquier
    // longitud; grupo 1 = cuerpo sin relleno.
    static const QRegularExpression re(uR"((?<![A-Za-z0-9+/_-])([A-Za-z0-9+/_-]{64,})=*)"_s);
    return re;
}

// Hex puro (con separadores - o _): SHA-256, dedup_key, cadenas de UUID.
bool esHexPuro(QStringView s)
{
    for (const QChar c : s) {
        const bool hex = (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')
                         || (c >= u'A' && c <= u'F') || c == u'-' || c == u'_';
        if (!hex) {
            return false;
        }
    }
    return true;
}

// --- Truncado ----------------------------------------------------------------

// Ajusta el corte para no partir un par sustituto ni un marcador existente.
qsizetype ajustarCorte(const QString& t, qsizetype corte)
{
    if (corte > 0 && corte < t.size() && t.at(corte - 1).isHighSurrogate()) {
        --corte;
    }
    const qsizetype inicio = corte > 0 ? t.lastIndexOf(u'[', corte - 1) : -1;
    if (inicio >= 0) {
        const QStringView desde = QStringView(t).sliced(inicio);
        if (desde.startsWith(u"[REDACTED:") || desde.startsWith(u"[TRUNCATED:")) {
            const qsizetype fin = t.indexOf(u']', inicio);
            if (fin < 0 || fin >= corte) {
                corte = inicio;
            }
        }
    }
    return corte;
}

QString aplicarReglas(QString t, QStringList& marcadores)
{
    for (const BloqueXml& b : bloquesXml()) {
        t = reemplazarFijo(t, b.cerrado, b.marcador, marcadores);
        t = reemplazarFijo(t, b.abierto, b.marcador, marcadores);
    }
    t = reemplazarFijo(t, pemCerrado(), marcador(u"pem"), marcadores);
    t = reemplazarFijo(t, pemAbierto(), marcador(u"pem"), marcadores);

    const QString token = marcador(u"token");
    t = reemplazar(
        t, authorization(),
        [&](const QRegularExpressionMatch& m) -> std::optional<Reemplazo> {
            if (esMarcadorExacto(m.capturedView(3).trimmed())) {
                return std::nullopt;
            }
            return Reemplazo{m.captured(1) + m.captured(2) + token, token};
        },
        marcadores);
    t = reemplazar(
        t, bearer(),
        [&](const QRegularExpressionMatch& m) -> std::optional<Reemplazo> {
            if (esMarcadorExacto(m.capturedView(2))) {
                return std::nullopt;
            }
            return Reemplazo{m.captured(1) + u' ' + token, token};
        },
        marcadores);

    t = reemplazar(
        t, elementoXmlSensible(),
        [](const QRegularExpressionMatch& m) -> std::optional<Reemplazo> {
            if (esMarcadorExacto(m.capturedView(4))) {
                return std::nullopt;
            }
            const QString mk = marcador(categoriaDeClave(m.captured(3)));
            return Reemplazo{m.captured(1) + mk + m.captured(5), mk};
        },
        marcadores);
    t = reemplazar(
        t, parClaveValor(),
        [](const QRegularExpressionMatch& m) -> std::optional<Reemplazo> {
            if (esMarcadorExacto(m.capturedView(4))) {
                return std::nullopt;
            }
            const QString mk = marcador(categoriaDeClave(m.captured(2)));
            return Reemplazo{m.captured(1) + m.captured(2) + m.captured(1) + m.captured(3) + mk, mk};
        },
        marcadores);

    // base64: solo si no es hex puro.
    t = reemplazar(
        t, base64(),
        [](const QRegularExpressionMatch& m) -> std::optional<Reemplazo> {
            if (esHexPuro(m.capturedView(1))) {
                return std::nullopt;
            }
            const QString mk = u"[REDACTED:base64:%1]"_s.arg(m.capturedLength());
            return Reemplazo{mk, mk};
        },
        marcadores);
    return t;
}

std::optional<QString> sanearOpcional(const std::optional<QString>& valor, qsizetype limite)
{
    if (!valor) {
        return std::nullopt;
    }
    QString s = RegexLogSanitizer::sanearTexto(valor->trimmed(), limite).texto;
    if (s.isEmpty()) {
        return std::nullopt;
    }
    return s;
}

void ponerSiHay(QJsonObject& o, QLatin1StringView clave, const std::optional<QString>& valor)
{
    if (valor && !valor->isEmpty()) {
        o.insert(clave, *valor);
    }
}

QJsonObject filtrosJson(const SolicitudCanonica& c)
{
    using L = RegexLogSanitizer;
    QJsonObject f;
    f.insert("tipo_cfdi"_L1, c.tipoCfdi());
    f.insert("rfc_solicitante"_L1, c.rfcSolicitante());
    ponerSiHay(f, "rfc_emisor"_L1, c.rfcEmisor());
    ponerSiHay(f, "rfc_receptor"_L1, c.rfcReceptor());
    if (!c.rfcReceptores().isEmpty()) {
        f.insert("rfc_receptores"_L1, QJsonArray::fromStringList(c.rfcReceptores()));
    }
    f.insert("fecha_inicial_sat"_L1, c.fechaInicialSat());
    f.insert("fecha_final_sat"_L1, c.fechaFinalSat());
    ponerSiHay(f, "tipo_comprobante"_L1, c.tipoComprobante());
    ponerSiHay(f, "complemento"_L1, sanearOpcional(c.complemento(), L::kLimiteMensaje));
    return f;
}

} // namespace

RegexLogSanitizer::TextoSaneado RegexLogSanitizer::sanearTexto(QStringView texto, qsizetype limite)
{
    TextoSaneado r;
    qsizetype recortados = 0;
    QString t;
    if (texto.size() > kLimiteEntrada) {
        qsizetype corte = kLimiteEntrada;
        if (texto.at(corte - 1).isHighSurrogate()) {
            --corte;
        }
        recortados = texto.size() - corte;
        t = texto.first(corte).toString();
    } else {
        t = texto.toString();
    }

    t = aplicarReglas(std::move(t), r.marcadores);

    if (recortados == 0 && t.size() <= limite) {
        r.texto = std::move(t);
        return r;
    }

    qsizetype conservar = qMin(t.size(), limite);
    for (;;) {
        conservar = ajustarCorte(t, conservar);
        const qsizetype n = recortados + (t.size() - conservar);
        const QString mk = u"[TRUNCATED:%1]"_s.arg(n);
        if (conservar + mk.size() <= limite || conservar == 0) {
            r.texto = t.left(conservar) + mk;
            r.caracteresRecortados = n;
            r.marcadores.append(mk);
            return r;
        }
        conservar = qMax<qsizetype>(0, limite - mk.size());
    }
}

LogEntradaSaneada RegexLogSanitizer::sanitizar(const LogEntradaCruda& entrada) const
{
    LogEntradaSaneada s;
    s.id = entrada.id;
    s.solicitudMasivaId = entrada.solicitudId;
    s.tipoEvento = entrada.tipoEvento;
    s.origen = entrada.origen;
    s.creadoEn = entrada.creadoEn;

    QJsonObject payload;
    const std::optional<QString> codigo = sanearOpcional(entrada.codigoSat, kLimiteMensaje);
    const std::optional<QString> mensajeSat = sanearOpcional(entrada.mensajeSat, kLimiteMensaje);

    // ck_log_codigo_sat: origen <=> codigo; mensaje_sat => codigo.
    if (codigo && entrada.origenCodigoSat) {
        s.origenCodigoSat = entrada.origenCodigoSat;
        s.codigoSat = codigo;
        s.mensajeSat = mensajeSat;
    } else {
        ponerSiHay(payload, "codigo_sat"_L1, codigo);
        ponerSiHay(payload, "mensaje_sat"_L1, mensajeSat);
    }

    ponerSiHay(payload, "id_solicitud_sat"_L1, sanearOpcional(entrada.idSolicitudSat, kLimiteMensaje));
    ponerSiHay(payload, "id_paquete_sat"_L1, sanearOpcional(entrada.idPaqueteSat, kLimiteMensaje));
    if (entrada.operacionSat) {
        payload.insert("operacion_sat"_L1, claveEstable(*entrada.operacionSat));
    }
    if (entrada.dedupKey && !entrada.dedupKey->esNula()) {
        payload.insert("dedup_key"_L1, entrada.dedupKey->texto());
    }
    if (entrada.filtros) {
        payload.insert("filtros"_L1, filtrosJson(*entrada.filtros));
    }
    ponerSiHay(payload, "mensaje"_L1, sanearOpcional(entrada.mensaje, kLimiteMensaje));
    ponerSiHay(payload, "detalle"_L1, sanearOpcional(entrada.detalle, kLimiteDetalle));

    if (!payload.isEmpty()) {
        s.payloadResumenJson = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }
    return s;
}

} // namespace satcfdi
