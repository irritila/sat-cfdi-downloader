#include "TestRegexLogSanitizer.h"

#include "application/logging/RegexLogSanitizer.h"
#include "domain/common/UuidCanonico.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>
#include <QTimeZone>

#include <algorithm>

using namespace satcfdi;
using namespace Qt::StringLiterals;

namespace {

using S = RegexLogSanitizer;

// Marcador exacto del catalogo cerrado de T003.
const QRegularExpression kCatalogo(
    uR"(^\[(?:REDACTED:(?:token|password|clave|secret|firma|certificado|pem|paquete|base64:[1-9][0-9]*)|TRUNCATED:[1-9][0-9]*)\]$)"_s);
// Cualquier cosa con forma de marcador.
const QRegularExpression kFormaMarcador(uR"(\[(?:REDACTED|TRUNCATED):[^\]]*\])"_s);

// Cadena del alfabeto base64 con letras no hex (nunca "hex puro").
QString b64(qsizetype n)
{
    static const QString alfabeto = u"Zm9vYmFyQmF6K3Fxeg/WXo+"_s;
    QString s;
    s.reserve(n);
    for (qsizetype i = 0; i < n; ++i) {
        s.append(alfabeto.at(i % alfabeto.size()));
    }
    return s;
}

QString hex(qsizetype n, bool mayusculas = false)
{
    static const QString digitos = u"0123456789abcdef"_s;
    QString s;
    for (qsizetype i = 0; i < n; ++i) {
        s.append(digitos.at((i * 7 + 3) % 16));
    }
    return mayusculas ? s.toUpper() : s;
}

QString repetir(QStringView pieza, qsizetype minimo)
{
    QString s;
    while (s.size() < minimo) {
        s.append(pieza);
    }
    return s;
}

QString sanear(const QString& texto, qsizetype limite = S::kLimiteDetalle)
{
    return S::sanearTexto(texto, limite).texto;
}

bool utf16Valido(const QString& s)
{
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s.at(i).isHighSurrogate()) {
            if (i + 1 >= s.size() || !s.at(i + 1).isLowSurrogate()) {
                return false;
            }
            ++i;
        } else if (s.at(i).isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

const QDateTime kCreado = QDateTime(QDate(2026, 10, 3), QTime(12, 0, 0, 250), QTimeZone(QTimeZone::UTC));

LogEntradaCruda entradaBase()
{
    LogEntradaCruda e;
    e.id = uuid::generarCanonico();
    e.solicitudId = SolicitudId::generar();
    e.tipoEvento = TipoEventoLog::VerificacionRealizada;
    e.origen = OrigenLog::Worker;
    e.creadoEn = kCreado;
    return e;
}

QJsonObject payloadDe(const LogEntradaSaneada& s)
{
    return s.payloadResumenJson ? QJsonDocument::fromJson(s.payloadResumenJson->toUtf8()).object()
                                : QJsonObject();
}

struct Caso {
    const char* nombre;
    QString entrada;
    QString esperado;
};

// Marcador exacto concatenado con un secreto, para cada clave sensible,
// elemento XML, Authorization y Bearer. Todos contienen kSecretoSufijo.
const QString kSecretoSufijo = u"S3cr3tSAT"_s;

QList<Caso> casosMarcadorConSufijo()
{
    QList<Caso> c;
    static const QList<QPair<const char*, const char*>> claves = {
        {"token", "token"},          {"access_token", "token"}, {"access token", "token"},
        {"password", "password"},    {"passwd", "password"},    {"contrasena", "password"},
        {"contraseña", "password"},  {"clave", "clave"},        {"pin", "clave"},
        {"secret", "secret"},        {"api_key", "secret"},     {"api key", "secret"},
        {"apikey", "secret"},
    };
    static QList<QByteArray> nombres; // QTest::newRow guarda el puntero
    auto agregar = [&](const QString& nombre, QString entrada, QString esperado) {
        nombres.append(nombre.toUtf8());
        c.append({nombres.constLast().constData(), std::move(entrada), std::move(esperado)});
    };
    for (const auto& [claveC, categoriaC] : claves) {
        const QString clave = QString::fromUtf8(claveC);
        const QString mk = u"[REDACTED:"_s + QString::fromUtf8(categoriaC) + u']';
        agregar(clave + u" sufijo"_s, clave + u"=[REDACTED:token]"_s + kSecretoSufijo + u" fin"_s,
                clave + u'=' + mk + u" fin"_s);
        agregar(clave + u" sufijo otro marcador"_s, clave + u": [REDACTED:base64:80]"_s + kSecretoSufijo,
                clave + u": "_s + mk);
        agregar(clave + u" sufijo comillas"_s, clave + u"=\"[REDACTED:token]\""_s + kSecretoSufijo,
                clave + u'=' + mk);
        agregar(clave + u" json sufijo"_s,
                u"{\""_s + clave + u"\": \"[REDACTED:pem]"_s + kSecretoSufijo + u"\"}"_s,
                u"{\""_s + clave + u"\": "_s + mk + u'}');
    }
    agregar(u"xml sufijo"_s, u"<clave>[REDACTED:clave]"_s + kSecretoSufijo + u"</clave>"_s,
            u"<clave>[REDACTED:clave]</clave>"_s);
    agregar(u"Authorization sufijo"_s, u"Authorization: [REDACTED:token]"_s + kSecretoSufijo + u"\nok"_s,
            u"Authorization: [REDACTED:token]\nok"_s);
    agregar(u"Authorization sufijo tras espacio"_s, u"authorization=[REDACTED:token] "_s + kSecretoSufijo,
            u"authorization=[REDACTED:token]"_s);
    agregar(u"Bearer sufijo"_s, u"usa Bearer [REDACTED:token]"_s + kSecretoSufijo + u" ya"_s,
            u"usa Bearer [REDACTED:token] ya"_s);
    agregar(u"Bearer corchetes"_s, u"bearer [x]"_s + kSecretoSufijo, u"bearer [REDACTED:token]"_s);
    return c;
}

QList<Caso> casosRedaccion()
{
    QList<Caso> c;
    auto fila = [&](const char* n, QString e, QString r) { c.append({n, std::move(e), std::move(r)}); };

    // XML sensible.
    fila("ds:Signature con hijos", u"<ds:Signature xmlns:ds=\"x\"><ds:SignatureValue>QUJD</ds:SignatureValue>"
           "<ds:X509Certificate>MIIB</ds:X509Certificate></ds:Signature> fin"_s, u"[REDACTED:firma] fin"_s);
    fila("SignatureValue", u"a<SignatureValue>abc</SignatureValue>b"_s, u"a[REDACTED:firma]b"_s);
    fila("X509Certificate", u"<X509Certificate>MIIB</X509Certificate>"_s, u"[REDACTED:certificado]"_s);
    fila("Paquete", u"<Paquete>UEsDBA==</Paquete> ok"_s, u"[REDACTED:paquete] ok"_s);
    fila("Paquete con atributos y ns", u"<des:Paquete IdPaquete=\"X\">UEsD</des:Paquete>"_s, u"[REDACTED:paquete]"_s);
    fila("Paquete sin cierre", u"resp <s:Paquete>UEsDBBQ"_s, u"resp [REDACTED:paquete]"_s);
    fila("multilinea", u"<SignatureValue>\nabc\n</SignatureValue>"_s, u"[REDACTED:firma]"_s);

    // PEM.
    fila("PEM certificado", u"x -----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE----- y"_s, u"x [REDACTED:pem] y"_s);
    fila("PEM llave sin cierre", u"k: -----BEGIN ENCRYPTED PRIVATE KEY-----\nMIIE"_s, u"k: [REDACTED:pem]"_s);

    // Pares clave/valor.
    fila("token", u"token=abc123 resto"_s, u"token=[REDACTED:token] resto"_s);
    fila("access_token", u"access_token: eyJhbGc"_s, u"access_token: [REDACTED:token]"_s);
    fila("access token json", u"{\"access token\":\"x\"}"_s, u"{\"access token\":[REDACTED:token]}"_s);
    fila("query", u"url?token=abc&x=1"_s, u"url?token=[REDACTED:token]&x=1"_s);
    fila("password json", u"\"password\": \"mi clave\""_s, u"\"password\": [REDACTED:password]"_s);
    fila("passwd comilla simple", u"passwd='x'"_s, u"passwd=[REDACTED:password]"_s);
    fila("contrasena", u"contrasena=abc"_s, u"contrasena=[REDACTED:password]"_s);
    fila("contraseña", u"Contraseña: abc"_s, u"Contraseña: [REDACTED:password]"_s);
    fila("clave", u"clave=FIEL1234"_s, u"clave=[REDACTED:clave]"_s);
    fila("pin", u"PIN = 1234"_s, u"PIN = [REDACTED:clave]"_s);
    fila("secret", u"secret=s3cr3t;"_s, u"secret=[REDACTED:secret];"_s);
    fila("api_key", u"api_key=AKIA"_s, u"api_key=[REDACTED:secret]"_s);
    fila("API Key", u"API Key: k"_s, u"API Key: [REDACTED:secret]"_s);
    fila("apikey", u"apikey=k"_s, u"apikey=[REDACTED:secret]"_s);
    fila("valor entre corchetes", u"password=[entre corchetes]"_s, u"password=[REDACTED:password]"_s);
    fila("elemento xml", u"<password>x y</password>"_s, u"<password>[REDACTED:password]</password>"_s);
    fila("elemento xml con ns", u"<a:Clave t=\"1\">x</a:Clave>"_s, u"<a:Clave t=\"1\">[REDACTED:clave]</a:Clave>"_s);

    // Authorization y Bearer.
    fila("Authorization Bearer", u"Authorization: Bearer eyJ.abc.def\nsiguiente"_s, u"Authorization: [REDACTED:token]\nsiguiente"_s);
    fila("Authorization Basic", u"Authorization: Basic dXNlcjpwYXNz"_s, u"Authorization: [REDACTED:token]"_s);
    fila("authorization json", u"{\"authorization\": \"WRAP access_token=x\"}"_s, u"{\"authorization\": [REDACTED:token]"_s);
    fila("bearer suelto", u"usa bearer abc.def-ghi ya"_s, u"usa bearer [REDACTED:token] ya"_s);

    // Authorization en JSON de una linea con campos posteriores: se redacta
    // hasta fin de linea (limitacion aceptada, no bloqueante; DC4/02-seguridad).
    fila("authorization json con campos posteriores",
         u"{\"Authorization\":\"Bearer s\",\"x\":1,\"rfc\":\"EKU9003173C9\"}"_s,
         u"{\"Authorization\":[REDACTED:token]"_s);

    // Marcador exacto con sufijo pegado (hallazgo bloqueante de seguridad):
    // el sufijo nunca se preserva.
    c.append(casosMarcadorConSufijo());
    return c;
}

QList<Caso> casosBase64()
{
    return {
        {"64", b64(64), u"[REDACTED:base64:64]"_s},
        {"88 con relleno", b64(86) + u"=="_s, u"[REDACTED:base64:88]"_s},
        {"relleno largo", b64(64) + u"=== fin"_s, u"[REDACTED:base64:67] fin"_s},
        {"tras igual", u"dato="_s + b64(70), u"dato=[REDACTED:base64:70]"_s},
        {"url-safe", repetir(u"ab-_Zx", 66), u"[REDACTED:base64:66]"_s},
        {"dos bloques", b64(64) + u' ' + b64(100), u"[REDACTED:base64:64] [REDACTED:base64:100]"_s},
        {"en xml no sensible", u"<Dato>"_s + b64(80) + u"</Dato>"_s, u"<Dato>[REDACTED:base64:80]</Dato>"_s},
        {"hex con relleno se preserva", hex(64) + u"="_s, hex(64) + u"="_s},
    };
}

} // namespace

void TestRegexLogSanitizer::redacta_data()
{
    QTest::addColumn<QString>("entrada");
    QTest::addColumn<QString>("esperado");
    for (const Caso& c : casosRedaccion()) {
        QTest::newRow(c.nombre) << c.entrada << c.esperado;
    }
}

void TestRegexLogSanitizer::redacta()
{
    QFETCH(QString, entrada);
    QFETCH(QString, esperado);
    const S::TextoSaneado r = S::sanearTexto(entrada, S::kLimiteDetalle);
    QCOMPARE(r.texto, esperado);
    QVERIFY(!r.marcadores.isEmpty());
    QCOMPARE(r.caracteresRecortados, 0);
}

void TestRegexLogSanitizer::preserva_data()
{
    QTest::addColumn<QString>("texto");
    QTest::newRow("vacio") << QString();
    QTest::newRow("RFC") << u"RFC EKU9003173C9 y XAXX010101000, persona fisica CACX7605101P8"_s;
    QTest::newRow("IdSolicitud") << u"IdSolicitud=4e80345d-917f-40bb-a98f-4a73939343c5"_s;
    QTest::newRow("IdPaquete") << u"IdPaquete 4E80345D-917F-40BB-A98F-4A73939343C5_01"_s;
    QTest::newRow("codigos") << u"CodEstatus=5000 Mensaje=Solicitud Aceptada EstadoSolicitud=3 CodigoEstadoSolicitud=5004"_s;
    QTest::newRow("fechas") << u"Periodo 2026-09-01T00:00:00 a 2026-09-30T23:59:59; creado 2026-10-03T12:00:00.250Z"_s;
    QTest::newRow("dedup_key v1") << (u"dedup_key=v1:"_s + hex(64));
    QTest::newRow("SHA-256 minusculas") << (u"sha256 "_s + hex(64) + u" ok"_s);
    QTest::newRow("SHA-256 mayusculas") << hex(64, true);
    QTest::newRow("hex largo") << hex(128);
    QTest::newRow("separador") << repetir(u"-", 80);
    QTest::newRow("palabras parecidas")
        << u"opinion=buena, pinza: 3, clavel=rojo, tokens del lexer, secretaria: Ana, passwords olvidados"_s;
    QTest::newRow("unicode") << u"Hola 😀 mundo, contraseñas no; Ñandú"_s;
    QTest::newRow("Paquete autocerrado") << u"<Paquete/> vacio"_s;
    QTest::newRow("base64 corto") << b64(63);
}

void TestRegexLogSanitizer::preserva()
{
    QFETCH(QString, texto);
    const S::TextoSaneado r = S::sanearTexto(texto, S::kLimiteDetalle);
    QCOMPARE(r.texto, texto);
    QVERIFY(r.marcadores.isEmpty());
}

void TestRegexLogSanitizer::base64Huerfano_data()
{
    QTest::addColumn<QString>("entrada");
    QTest::addColumn<QString>("esperado");
    for (const Caso& c : casosBase64()) {
        QTest::newRow(c.nombre) << c.entrada << c.esperado;
    }
}

void TestRegexLogSanitizer::base64Huerfano()
{
    QFETCH(QString, entrada);
    QFETCH(QString, esperado);
    QCOMPARE(sanear(entrada), esperado);
}

void TestRegexLogSanitizer::marcadoresPertenecenAlCatalogo()
{
    const QString texto =
        u"<ds:Signature>x</ds:Signature> <X509Certificate>y</X509Certificate> <Paquete>z</Paquete> "
        "-----BEGIN CERTIFICATE-----\nq\n-----END CERTIFICATE----- token=a password=b clave=c secret=d "_s
        + b64(70) + u" Authorization: Bearer t\n"_s + repetir(u"relleno ", 600);
    const S::TextoSaneado r = S::sanearTexto(texto, S::kLimiteMensaje);

    for (const QString& m : r.marcadores) {
        QVERIFY2(kCatalogo.match(m).hasMatch(), qPrintable(m));
    }
    auto it = kFormaMarcador.globalMatch(r.texto);
    int enTexto = 0;
    while (it.hasNext()) {
        const QString m = it.next().captured();
        QVERIFY2(kCatalogo.match(m).hasMatch(), qPrintable(m));
        ++enTexto;
    }
    QVERIFY(enTexto > 0);
    for (const char* c : {"[REDACTED:firma", "[REDACTED:certificado", "[REDACTED:paquete", "[REDACTED:pem",
                          "[REDACTED:token", "[REDACTED:password", "[REDACTED:clave", "[REDACTED:secret",
                          "[REDACTED:base64", "[TRUNCATED:"}) {
        const QString prefijo = QString::fromLatin1(c);
        QVERIFY2(std::any_of(r.marcadores.cbegin(), r.marcadores.cend(),
                             [&](const QString& m) { return m.startsWith(prefijo); }),
                 c);
    }
    QVERIFY(r.texto.size() <= S::kLimiteMensaje);
}

void TestRegexLogSanitizer::marcadorConSufijoNoSePreserva_data()
{
    QTest::addColumn<QString>("entrada");
    QTest::addColumn<QString>("esperado");
    for (const Caso& c : casosMarcadorConSufijo()) {
        QTest::newRow(c.nombre) << c.entrada << c.esperado;
    }
}

void TestRegexLogSanitizer::marcadorConSufijoNoSePreserva()
{
    QFETCH(QString, entrada);
    QFETCH(QString, esperado);
    const S::TextoSaneado una = S::sanearTexto(entrada, S::kLimiteMensaje);
    QCOMPARE(una.texto, esperado);
    QVERIFY(!una.texto.contains(kSecretoSufijo));
    QVERIFY(!una.marcadores.isEmpty());
    // Idempotente y sin volver a reportar.
    const S::TextoSaneado dos = S::sanearTexto(una.texto, S::kLimiteMensaje);
    QCOMPARE(dos.texto, una.texto);
    QVERIFY(dos.marcadores.isEmpty());
    // Tambien por sanitizar(): el sufijo no llega al payload ni a mensaje_sat.
    LogEntradaCruda e = entradaBase();
    e.mensaje = entrada;
    e.detalle = entrada;
    e.origenCodigoSat = OrigenCodigoSat::Verificacion;
    e.codigoSat = u"5000"_s;
    e.mensajeSat = entrada;
    const LogEntradaSaneada s = RegexLogSanitizer().sanitizar(e);
    QVERIFY(!s.payloadResumenJson->contains(kSecretoSufijo));
    QVERIFY(!s.mensajeSat->contains(kSecretoSufijo));
}

void TestRegexLogSanitizer::marcadorFalsoNoEvitaRedaccion()
{
    QCOMPARE(sanear(u"password=\"[REDACTED:x-secreto]\""_s), u"password=[REDACTED:password]"_s);
    QCOMPARE(sanear(u"token=[REDACTED:token2]"_s), u"token=[REDACTED:token]"_s);
    QCOMPARE(sanear(u"Authorization: [REDACTED:token] secreto"_s), u"Authorization: [REDACTED:token]"_s);
    QCOMPARE(sanear(u"<clave>[REDACTED:pem]x</clave>"_s), u"<clave>[REDACTED:clave]</clave>"_s);
    // Un marcador exacto se conserva y no se reporta.
    const S::TextoSaneado r = S::sanearTexto(u"clave=[REDACTED:base64:80]"_s, 500);
    QCOMPARE(r.texto, u"clave=[REDACTED:base64:80]"_s);
    QVERIFY(r.marcadores.isEmpty());
}

void TestRegexLogSanitizer::idempotente_data()
{
    QTest::addColumn<QString>("texto");
    QTest::addColumn<qsizetype>("limite");

    for (const QList<Caso>& casos : {casosRedaccion(), casosBase64()}) {
        for (const Caso& c : casos) {
            QTest::addRow("%s / 500", c.nombre) << c.entrada << S::kLimiteMensaje;
            QTest::addRow("%s / 40", c.nombre) << c.entrada << qsizetype(40);
        }
    }
    const QString base = u"token=abc password=\"x y\" <Paquete>z</Paquete> "_s + b64(90)
                         + u" Authorization: Basic q\nRFC EKU9003173C9 v1:"_s + hex(64) + u' ';
    for (qsizetype limite : {qsizetype(40), qsizetype(64), qsizetype(77), S::kLimiteMensaje, S::kLimiteDetalle}) {
        QTest::addRow("mezcla truncada %lld", qlonglong(limite)) << repetir(base, 9000) << limite;
    }
    QTest::addRow("corte junto a clave") << u"xxxxxxxxx token=abcdefghijklmnop"_s << qsizetype(25);
    QTest::addRow("emoji") << repetir(u"😀a ", 700) << S::kLimiteMensaje;
    QTest::addRow("mayor a 1 MiB") << repetir(u"clave=x ", S::kLimiteEntrada + 10) << S::kLimiteDetalle;
}

void TestRegexLogSanitizer::idempotente()
{
    QFETCH(QString, texto);
    QFETCH(qsizetype, limite);
    const S::TextoSaneado una = S::sanearTexto(texto, limite);
    const S::TextoSaneado dos = S::sanearTexto(una.texto, limite);
    QCOMPARE(dos.texto, una.texto);
    QVERIFY(dos.marcadores.isEmpty());
    QCOMPARE(dos.caracteresRecortados, 0);
    QVERIFY(una.texto.size() <= limite);
}

void TestRegexLogSanitizer::limitesDeMensajeYDetalle_data()
{
    QTest::addColumn<QString>("texto");
    QTest::addColumn<qsizetype>("limite");
    QTest::addColumn<bool>("trunca");
    const QString frase = u"hola mundo "_s;
    QTest::newRow("mensaje exacto") << repetir(frase, 500).left(500) << S::kLimiteMensaje << false;
    QTest::newRow("mensaje 501") << repetir(frase, 501).left(501) << S::kLimiteMensaje << true;
    QTest::newRow("mensaje largo") << repetir(frase, 5000) << S::kLimiteMensaje << true;
    QTest::newRow("detalle exacto") << repetir(frase, 8192).left(8192) << S::kLimiteDetalle << false;
    QTest::newRow("detalle 8193") << repetir(frase, 8193).left(8193) << S::kLimiteDetalle << true;
    QTest::newRow("detalle largo") << repetir(frase, 20000) << S::kLimiteDetalle << true;
}

void TestRegexLogSanitizer::limitesDeMensajeYDetalle()
{
    QFETCH(QString, texto);
    QFETCH(qsizetype, limite);
    QFETCH(bool, trunca);

    const S::TextoSaneado r = S::sanearTexto(texto, limite);
    QVERIFY(r.texto.size() <= limite);
    if (!trunca) {
        QCOMPARE(r.texto, texto);
        QCOMPARE(r.caracteresRecortados, 0);
        return;
    }
    const QString mk = u"[TRUNCATED:%1]"_s.arg(r.caracteresRecortados);
    QVERIFY(r.texto.endsWith(mk));
    QCOMPARE(r.marcadores, QStringList{mk});
    const QString conservado = r.texto.chopped(mk.size());
    QVERIFY(texto.startsWith(conservado));
    QCOMPARE(conservado.size() + r.caracteresRecortados, texto.size());
    QCOMPARE(r.texto.size(), limite); // aprovecha el limite completo
}

void TestRegexLogSanitizer::truncadoNoPartePareSustituto()
{
    for (qsizetype k = 460; k <= 500; ++k) {
        const QString texto = repetir(u"z ", k).left(k) + repetir(u"😀", 80);
        const S::TextoSaneado r = S::sanearTexto(texto, S::kLimiteMensaje);
        QVERIFY(r.texto.size() <= S::kLimiteMensaje);
        QVERIFY2(utf16Valido(r.texto), qPrintable(QString::number(k)));
        QCOMPARE(QString::fromUtf8(r.texto.toUtf8()), r.texto);
        const QString conservado = r.texto.chopped(u"[TRUNCATED:%1]"_s.arg(r.caracteresRecortados).size());
        QCOMPARE(conservado.size() + r.caracteresRecortados, texto.size());
    }
}

void TestRegexLogSanitizer::truncadoNoParteMarcador()
{
    const QString texto = repetir(u"token=abc ", 400);
    for (qsizetype limite = 30; limite <= 80; ++limite) {
        const S::TextoSaneado r = S::sanearTexto(texto, limite);
        QVERIFY(r.texto.size() <= limite);
        auto it = kFormaMarcador.globalMatch(r.texto);
        qsizetype cubiertos = 0;
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QVERIFY2(kCatalogo.match(m.captured()).hasMatch(), qPrintable(r.texto));
            cubiertos += m.capturedLength();
        }
        // Ningun fragmento de marcador fuera de marcadores completos.
        QCOMPARE(r.texto.count(u'['), r.texto.count(u']'));
        QVERIFY(cubiertos > 0);
    }
}

void TestRegexLogSanitizer::entradaMayorAUnMiB()
{
    // La clave sensible cae despues de 1 MiB: se descarta sin evaluarse.
    QString texto = repetir(u"palabra ", S::kLimiteEntrada + 100);
    texto.append(u"password=filtrado");
    const S::TextoSaneado r = S::sanearTexto(texto, S::kLimiteDetalle);
    QVERIFY(r.texto.size() <= S::kLimiteDetalle);
    QVERIFY(!r.texto.contains(u"filtrado"));
    const QString mk = u"[TRUNCATED:%1]"_s.arg(r.caracteresRecortados);
    QVERIFY(r.texto.endsWith(mk));
    QCOMPARE(r.texto.size() - mk.size() + r.caracteresRecortados, texto.size());

    // Corte de entrada sobre un par sustituto.
    const QString conEmoji = repetir(u"z ", S::kLimiteEntrada).left(S::kLimiteEntrada - 1) + u"😀cola"_s;
    const S::TextoSaneado e = S::sanearTexto(conEmoji, S::kLimiteEntrada);
    QVERIFY(utf16Valido(e.texto));
    QVERIFY(e.texto.size() <= S::kLimiteEntrada);
    const QString mk2 = u"[TRUNCATED:%1]"_s.arg(e.caracteresRecortados);
    QCOMPARE(e.texto.size() - mk2.size() + e.caracteresRecortados, conEmoji.size());
}

void TestRegexLogSanitizer::sanitizarCopiaMetadataYArmaPayload()
{
    LogEntradaCruda e = entradaBase();
    e.idSolicitudSat = u"4e80345d-917f-40bb-a98f-4a73939343c5"_s;
    e.idPaqueteSat = u"4E80345D-917F-40BB-A98F-4A73939343C5_01"_s;
    e.operacionSat = OperacionSat::SolicitaDescargaEmitidos;
    e.dedupKey = DedupKey::calcularV1("abc");
    e.mensaje = u"  Verificacion realizada.  "_s;
    e.detalle = u"EstadoSolicitud=3"_s;

    const LogEntradaSaneada s = RegexLogSanitizer().sanitizar(e);
    QCOMPARE(s.id, e.id);
    QCOMPARE(s.solicitudMasivaId, e.solicitudId);
    QCOMPARE(s.tipoEvento, TipoEventoLog::VerificacionRealizada);
    QCOMPARE(s.origen, OrigenLog::Worker);
    QCOMPARE(s.creadoEn, kCreado);
    QVERIFY(!s.origenCodigoSat && !s.codigoSat && !s.mensajeSat);

    const QJsonObject p = payloadDe(s);
    QCOMPARE(p.value(u"id_solicitud_sat").toString(), *e.idSolicitudSat);
    QCOMPARE(p.value(u"id_paquete_sat").toString(), *e.idPaqueteSat);
    QCOMPARE(p.value(u"operacion_sat").toString(), u"SolicitaDescargaEmitidos"_s);
    QCOMPARE(p.value(u"dedup_key").toString(), e.dedupKey->texto());
    QCOMPARE(p.value(u"mensaje").toString(), u"Verificacion realizada."_s);
    QCOMPARE(p.value(u"detalle").toString(), u"EstadoSolicitud=3"_s);
    QVERIFY(!p.contains(u"filtros"));
    QVERIFY(!s.payloadResumenJson->contains(u'\n')); // JSON compacto
}

void TestRegexLogSanitizer::sanitizarRespetaCkLogCodigoSat()
{
    const RegexLogSanitizer sanitizer;
    {
        LogEntradaCruda e = entradaBase();
        e.origenCodigoSat = OrigenCodigoSat::Verificacion;
        e.codigoSat = u"5000"_s;
        e.mensajeSat = u"Solicitud Aceptada token=abc"_s;
        const LogEntradaSaneada s = sanitizer.sanitizar(e);
        QCOMPARE(s.origenCodigoSat, std::optional(OrigenCodigoSat::Verificacion));
        QCOMPARE(s.codigoSat, std::optional<QString>(u"5000"_s));
        QCOMPARE(s.mensajeSat, std::optional<QString>(u"Solicitud Aceptada token=[REDACTED:token]"_s));
        QVERIFY(!s.payloadResumenJson);
    }
    {
        // Codigo sin origen: ambos al payload.
        LogEntradaCruda e = entradaBase();
        e.codigoSat = u"5004"_s;
        e.mensajeSat = u"No se encontro"_s;
        const LogEntradaSaneada s = sanitizer.sanitizar(e);
        QVERIFY(!s.origenCodigoSat && !s.codigoSat && !s.mensajeSat);
        const QJsonObject p = payloadDe(s);
        QCOMPARE(p.value(u"codigo_sat").toString(), u"5004"_s);
        QCOMPARE(p.value(u"mensaje_sat").toString(), u"No se encontro"_s);
    }
    {
        // Origen y mensaje sin codigo (o codigo en blanco): sin columnas SAT.
        LogEntradaCruda e = entradaBase();
        e.origenCodigoSat = OrigenCodigoSat::Descarga;
        e.codigoSat = u"   "_s;
        e.mensajeSat = u"Mensaje"_s;
        const LogEntradaSaneada s = sanitizer.sanitizar(e);
        QVERIFY(!s.origenCodigoSat && !s.codigoSat && !s.mensajeSat);
        const QJsonObject p = payloadDe(s);
        QVERIFY(!p.contains(u"codigo_sat"));
        QCOMPARE(p.value(u"mensaje_sat").toString(), u"Mensaje"_s);
    }
    {
        // Codigo y origen sin mensaje.
        LogEntradaCruda e = entradaBase();
        e.origenCodigoSat = OrigenCodigoSat::Creacion;
        e.codigoSat = u"301"_s;
        const LogEntradaSaneada s = sanitizer.sanitizar(e);
        QCOMPARE(s.codigoSat, std::optional<QString>(u"301"_s));
        QVERIFY(!s.mensajeSat);
    }
}

void TestRegexLogSanitizer::sanitizarSinDatosDejaPayloadNulo()
{
    LogEntradaCruda e = entradaBase();
    e.mensaje = u"   "_s;
    e.idSolicitudSat = QString();
    e.detalle = QString();
    const LogEntradaSaneada s = RegexLogSanitizer().sanitizar(e);
    QVERIFY(!s.payloadResumenJson);
    QVERIFY(!s.codigoSat && !s.mensajeSat && !s.origenCodigoSat);
}

void TestRegexLogSanitizer::sanitizarRedactaTextoLibreYFiltros()
{
    EntradaSolicitudCanonica entrada;
    entrada.tipoDescarga = TipoDescarga::Emitidos;
    entrada.rfcPerfil = u"EKU9003173C9"_s;
    entrada.fechaInicial = QDate(2026, 9, 1);
    entrada.fechaFinal = QDate(2026, 9, 30);
    entrada.rfcContrapartes = {u"XAXX010101000"_s};
    entrada.tipoComprobante = u"I"_s;
    const auto canonica = SolicitudCanonica::normalizar(entrada);
    QVERIFY(canonica.esExito());

    LogEntradaCruda e = entradaBase();
    e.filtros = canonica.valor();
    e.mensaje = u"Fallo con Authorization: Bearer abc.def\n"_s + repetir(u"x ", 1000);
    e.detalle = u"<s:Envelope><des:Paquete>"_s + b64(5000) + u"</des:Paquete></s:Envelope>"_s
                + u"\n-----BEGIN RSA PRIVATE KEY-----\nMIIE\n-----END RSA PRIVATE KEY-----"_s;
    e.mensajeSat = u"password=hunter2"_s;

    const LogEntradaSaneada s = RegexLogSanitizer().sanitizar(e);
    const QString json = *s.payloadResumenJson;
    for (const char* secreto : {"abc.def", "hunter2", "MIIE", "Zm9vYmFy"}) {
        QVERIFY2(!json.contains(QString::fromLatin1(secreto)), secreto);
    }
    const QJsonObject p = payloadDe(s);
    QVERIFY(p.value(u"mensaje").toString().size() <= S::kLimiteMensaje);
    QVERIFY(p.value(u"mensaje").toString().contains(u"[TRUNCATED:"));
    QCOMPARE(p.value(u"detalle").toString(),
             u"<s:Envelope>[REDACTED:paquete]</s:Envelope>\n[REDACTED:pem]"_s);
    QCOMPARE(p.value(u"mensaje_sat").toString(), u"password=[REDACTED:password]"_s);

    const QJsonObject f = p.value(u"filtros").toObject();
    QCOMPARE(f.value(u"tipo_cfdi").toString(), canonica.valor().tipoCfdi());
    QCOMPARE(f.value(u"rfc_solicitante").toString(), u"EKU9003173C9"_s);
    QCOMPARE(f.value(u"rfc_emisor").toString(), u"EKU9003173C9"_s);
    QCOMPARE(f.value(u"rfc_receptores").toArray(), QJsonArray{u"XAXX010101000"_s});
    QCOMPARE(f.value(u"fecha_inicial_sat").toString(), u"2026-09-01T00:00:00"_s);
    QCOMPARE(f.value(u"fecha_final_sat").toString(), u"2026-09-30T23:59:59"_s);
    QCOMPARE(f.value(u"tipo_comprobante").toString(), u"I"_s);
    QVERIFY(!f.contains(u"complemento"));
}
