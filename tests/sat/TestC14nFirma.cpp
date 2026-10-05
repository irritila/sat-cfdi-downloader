// Criterios 1 y 2 de T006: vectores C14N (bytes + SHA-1 calculados fuera de
// la implementacion: Python hashlib sobre las formas canonicas del ejemplo de
// W3C exc-c14n §2.2 y del ejemplo de Autentica de docs/web-service.md §4.2) y
// firmas RSA-SHA1 con llave temporal verificadas por OpenSSL.

#include "TestC14nFirma.h"

#include "SatPruebasComun.h"

#include "infrastructure/sat/FirmaXml.h"
#include "infrastructure/sat/SobresSat.h"
#include "infrastructure/sat/XmlC14n.h"

#include <QTest>
#include <QTimeZone>

using namespace satcfdi;
using namespace satcfdi::sat;

namespace {

const QByteArray kNsDsig = "http://www.w3.org/2000/09/xmldsig#";
const QByteArray kNsServicios = "http://DescargaMasivaTerceros.sat.gob.mx";

QByteArray c14n(const QByteArray& xml, VarianteC14n v, const SelectorNodo& s)
{
    auto r = canonicalizar(xml, v, s);
    return r ? r.valor() : QByteArray("<<error:" + r.error().diagnostico.toLatin1() + ">>");
}

ParametrosSolicitud parametros()
{
    ParametrosSolicitud p;
    p.rfcSolicitante = QStringLiteral("EKU9003173C9");
    p.rfcEmisor = QStringLiteral("EKU9003173C9");
    p.fechaInicial = QDateTime(QDate(2026, 9, 1), QTime(0, 0));
    p.fechaFinal = QDateTime(QDate(2026, 9, 1), QTime(23, 59, 59));
    return p;
}

// Verifica Signature: SignatureValue sobre SignedInfo canonico y digest de la
// referencia. Devuelve vacio si todo cuadra, o el motivo.
QString verificarFirma(const QByteArray& sobre, const QByteArray& cert, VarianteC14n variante,
                       const SelectorNodo& referencia)
{
    auto si = canonicalizar(sobre, variante, SelectorNodo::porNombre(kNsDsig, "SignedInfo"));
    if (!si) {
        return QStringLiteral("SignedInfo no canonicalizable");
    }
    const QByteArray firma = QByteArray::fromBase64(satpruebas::contenidoElemento(sobre, "SignatureValue"));
    if (!satpruebas::verificarRsaSha1(cert, si.valor(), firma)) {
        return QStringLiteral("OpenSSL no verifica SignatureValue");
    }
    auto nodo = canonicalizar(sobre, variante, referencia);
    if (!nodo) {
        return QStringLiteral("nodo referenciado no canonicalizable");
    }
    if (sha1Base64(nodo.valor()) != satpruebas::contenidoElemento(sobre, "DigestValue")) {
        return QStringLiteral("DigestValue no corresponde al nodo referenciado");
    }
    return {};
}

} // namespace

void TestC14nFirma::initTestCase()
{
    m_material = satpruebas::generarMaterial();
    QVERIFY(m_material);
}

void TestC14nFirma::vectoresSubconjuntoExcC14nW3c()
{
    // W3C exc-c14n §2.2, sin espacios.
    const QByteArray xml = "<n0:local xmlns:n0=\"foo:bar\" xmlns:n3=\"ftp://example.org\">"
                           "<n1:elem2 xmlns:n1=\"http://example.net\" xml:lang=\"en\">"
                           "<n3:stuff xmlns:n3=\"ftp://example.org\"/></n1:elem2></n0:local>";
    const auto selector = SelectorNodo::porNombre("http://example.net", "elem2");
    const QByteArray inclusiva = c14n(xml, VarianteC14n::Inclusiva, selector);
    const QByteArray exclusiva = c14n(xml, VarianteC14n::Exclusiva, selector);
    QCOMPARE(inclusiva, QByteArray("<n1:elem2 xmlns:n0=\"foo:bar\" xmlns:n1=\"http://example.net\" "
                                   "xmlns:n3=\"ftp://example.org\" xml:lang=\"en\"><n3:stuff></n3:stuff></n1:elem2>"));
    QCOMPARE(exclusiva, QByteArray("<n1:elem2 xmlns:n1=\"http://example.net\" xml:lang=\"en\">"
                                   "<n3:stuff xmlns:n3=\"ftp://example.org\"></n3:stuff></n1:elem2>"));
    QCOMPARE(sha1Base64(inclusiva), QByteArray("gkY37hzuxk66SPR9Lxqt8J5BtAA="));
    QCOMPARE(sha1Base64(exclusiva), QByteArray("ZxRDVQVMGzHnh4ltD6lYFohtBes="));
}

void TestC14nFirma::vectorDocumentoCompleto()
{
    const QByteArray xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!-- fuera -->"
                           "<a xmlns=\"urn:x\" b=\"2\" a=\"1\"><c>&lt;&amp;&gt;\"</c><!-- dentro --><d/></a>";
    const QByteArray esperado = "<a xmlns=\"urn:x\" a=\"1\" b=\"2\"><c>&lt;&amp;&gt;\"</c><d></d></a>";
    for (VarianteC14n v : {VarianteC14n::Inclusiva, VarianteC14n::Exclusiva}) {
        const QByteArray r = c14n(xml, v, SelectorNodo::documento());
        QCOMPARE(r, esperado);
        QCOMPARE(sha1Base64(r), QByteArray("7KZBauveDJEX1ImNrLAnudJ/knY="));
    }
}

void TestC14nFirma::vectorEnvelopedExcluyeFirma()
{
    const QByteArray xml = "<s:root xmlns:s=\"urn:s\"><p:op xmlns:p=\"urn:p\" a=\"1\" B=\"2\"><p:x>t</p:x>"
                           "<ds:Signature xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\"><ds:SignedInfo/>"
                           "</ds:Signature></p:op></s:root>";
    const auto selector = SelectorNodo::porNombre("urn:p", "op", true);
    const QByteArray inclusiva = c14n(xml, VarianteC14n::Inclusiva, selector);
    const QByteArray exclusiva = c14n(xml, VarianteC14n::Exclusiva, selector);
    QCOMPARE(inclusiva, QByteArray("<p:op xmlns:p=\"urn:p\" xmlns:s=\"urn:s\" B=\"2\" a=\"1\"><p:x>t</p:x></p:op>"));
    QCOMPARE(exclusiva, QByteArray("<p:op xmlns:p=\"urn:p\" B=\"2\" a=\"1\"><p:x>t</p:x></p:op>"));
    QCOMPARE(sha1Base64(inclusiva), QByteArray("PGU7sdI7qKmeTTeIKckpHtb+d+g="));
    QCOMPARE(sha1Base64(exclusiva), QByteArray("gNSZjzDa3t3g1S5vZlmyw11F+uM="));
    // Sin excluir, la firma si forma parte del conjunto.
    QVERIFY(c14n(xml, VarianteC14n::Exclusiva, SelectorNodo::porNombre("urn:p", "op")).contains("Signature"));
}

void TestC14nFirma::vectorTimestampDocSat()
{
    // Ejemplo de Autentica de docs/web-service.md §4.2. La doc transcribe el
    // DigestValue como "Ij+Epaya2U5D/sSncl6BHkkTRWo=" (l minuscula); el valor
    // correcto de esa forma canonica tiene I mayuscula (discrepancia de
    // transcripcion a registrar en la evidencia).
    const QByteArray xml =
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "xmlns:u=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\">"
        "<s:Header><o:Security s:mustUnderstand=\"1\" "
        "xmlns:o=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
        "<u:Timestamp u:Id=\"_0\"><u:Created>2018-05-09T21:21:42.953Z</u:Created>"
        "<u:Expires>2018-05-09T21:26:42.953Z</u:Expires></u:Timestamp></o:Security></s:Header>"
        "<s:Body/></s:Envelope>";
    const QByteArray canon = c14n(xml, VarianteC14n::Exclusiva, SelectorNodo::porId("_0"));
    QCOMPARE(sha1Base64(canon), QByteArray("Ij+Epaya2U5D/sSncI6BHkkTRWo="));
}

void TestC14nFirma::erroresDeEntrada()
{
    const auto noEncontrado = canonicalizar("<a/>", VarianteC14n::Inclusiva, SelectorNodo::porId("x"));
    QVERIFY(!noEncontrado);
    QCOMPARE(noEncontrado.error().diagnostico, QStringLiteral("xml.nodo_no_encontrado"));
    const auto duplicado =
        canonicalizar("<a><b Id=\"x\"/><c Id=\"x\"/></a>", VarianteC14n::Inclusiva, SelectorNodo::porId("x"));
    QCOMPARE(duplicado.error().diagnostico, QStringLiteral("xml.id_duplicado"));
    QVERIFY(!canonicalizar("<a>", VarianteC14n::Inclusiva, SelectorNodo::documento()));
    const auto doctype = canonicalizar("<!DOCTYPE a [<!ENTITY e \"x\">]><a>&e;</a>", VarianteC14n::Inclusiva,
                                       SelectorNodo::documento());
    QVERIFY(!doctype);
    QCOMPARE(uriAlgoritmo(VarianteC14n::Exclusiva), QStringLiteral("http://www.w3.org/2001/10/xml-exc-c14n#"));
    QCOMPARE(uriAlgoritmo(VarianteC14n::Inclusiva),
             QStringLiteral("http://www.w3.org/TR/2001/REC-xml-c14n-20010315"));
}

void TestC14nFirma::datosCertificadoIssuerYSerialDecimal()
{
    const MaterialFirma m = m_material->material();
    auto doc = leerDatosCertificado(m.certificadoDer(), FormatoIssuer::DocSat);
    QVERIFY(doc);
    QCOMPARE(doc.valor().issuer, satpruebas::kIssuerDocSat);
    QCOMPARE(doc.valor().serialDecimal, QString::fromLatin1(satpruebas::kSerialDecimal));
    QCOMPARE(QByteArray::fromBase64(doc.valor().certificadoBase64), m_material->certificadoDer);
    auto rfc4514 = leerDatosCertificado(m.certificadoDer(), FormatoIssuer::Rfc4514);
    QVERIFY(rfc4514);
    QVERIFY2(rfc4514.valor().issuer.startsWith(QStringLiteral("unstructuredName=Responsable: ACDMA,")),
             qPrintable(rfc4514.valor().issuer));
    QVERIFY(rfc4514.valor().issuer.endsWith(QStringLiteral("CN=A.C. 2 de pruebas(4096)")));
    QVERIFY(!leerDatosCertificado(BufferSecreto::desdeBytes("no", 2), FormatoIssuer::DocSat));
}

void TestC14nFirma::firmaAutenticaTimestamp()
{
    const MaterialFirma m = m_material->material();
    for (VarianteC14n v : {VarianteC14n::Exclusiva, VarianteC14n::Inclusiva}) {
        ContextoSobre ctx;
        ctx.ahoraUtc = QDateTime(QDate(2026, 10, 4), QTime(12, 0, 0, 123), QTimeZone::UTC);
        ctx.idToken = QStringLiteral("uuid-00000000-0000-4000-8000-000000000001-1");
        OpcionesFirma o;
        o.c14n = v;
        auto sobre = construirAutentica(m, ctx, o);
        QVERIFY2(sobre, sobre ? "" : qPrintable(sobre.error().diagnostico));
        const QByteArray xml = sobre.valor().xml;
        QVERIFY(xml.contains("<Reference URI=\"#_0\">"));
        QVERIFY(xml.contains("<u:Created>2026-10-04T12:00:00.123Z</u:Created>"));
        QVERIFY(xml.contains("<u:Expires>2026-10-04T12:05:00.123Z</u:Expires>"));
        QVERIFY(xml.contains("<o:Reference ValueType=\"http://docs.oasis-open.org/wss/2004/01/"
                             "oasis-200401-wss-x509-token-profile-1.0#X509v3\" URI=\"#" + ctx.idToken.toLatin1()));
        QVERIFY(xml.contains("u:Id=\"" + ctx.idToken.toLatin1() + "\""));
        QVERIFY(xml.contains("<CanonicalizationMethod Algorithm=\"" + uriAlgoritmo(v).toLatin1()));
        QVERIFY(xml.contains("\">" + m_material->certificadoDer.toBase64() + "</o:BinarySecurityToken>"));
        const QString motivo = verificarFirma(xml, m_material->certificadoDer, v, SelectorNodo::porId("_0"));
        QVERIFY2(motivo.isEmpty(), qPrintable(motivo));
        QCOMPARE(sobre.valor().digestBase64, satpruebas::contenidoElemento(xml, "DigestValue"));
    }
}

void TestC14nFirma::firmaEnvelopedDeCadaPeticion()
{
    const MaterialFirma m = m_material->material();
    struct Caso {
        const char* nombre;
        QByteArray nodo;
        std::function<Resultado<SobreSat, ErrorSobre>(const OpcionesFirma&)> construir;
    };
    ParametrosSolicitud recibidos = parametros();
    recibidos.rfcEmisor.clear();
    recibidos.rfcReceptor = QStringLiteral("EKU9003173C9");
    const QList<Caso> casos{
        {"emitidos", "solicitud", [&](const OpcionesFirma& o) { return construirSolicitudEmitidos(parametros(), m, o); }},
        {"recibidos", "solicitud", [&](const OpcionesFirma& o) { return construirSolicitudRecibidos(recibidos, m, o); }},
        {"verificacion", "solicitud",
         [&](const OpcionesFirma& o) {
             return construirVerificacion(QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5"),
                                          QStringLiteral("EKU9003173C9"), m, o);
         }},
        {"descarga", "peticionDescarga",
         [&](const OpcionesFirma& o) {
             return construirDescarga(QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5_01"),
                                      QStringLiteral("EKU9003173C9"), m, o);
         }},
    };
    for (const Caso& caso : casos) {
        for (VarianteC14n v : {VarianteC14n::Inclusiva, VarianteC14n::Exclusiva}) {
            OpcionesFirma o;
            o.c14n = v;
            auto sobre = caso.construir(o);
            QVERIFY2(sobre, caso.nombre);
            const QByteArray xml = sobre.valor().xml;
            // La firma va dentro del nodo de peticion (enveloped) y la
            // referencia es URI="" con transform enveloped-signature.
            const qsizetype inicioNodo = xml.indexOf("<des:" + caso.nodo + " ");
            const qsizetype firma = xml.indexOf("<Signature xmlns=\"http://www.w3.org/2000/09/xmldsig#\">");
            const qsizetype finNodo = xml.indexOf("</des:" + caso.nodo + ">");
            QVERIFY2(inicioNodo >= 0 && inicioNodo < firma && firma < finNodo, caso.nombre);
            QVERIFY(xml.contains("<Reference URI=\"\"><Transforms><Transform Algorithm=\""
                                 "http://www.w3.org/2000/09/xmldsig#enveloped-signature\"/>"));
            QCOMPARE(xml.contains("xml-exc-c14n#\"/></Transforms>"), v == VarianteC14n::Exclusiva);
            QVERIFY(xml.contains("<X509SerialNumber>" + QByteArray(satpruebas::kSerialDecimal) + "</X509SerialNumber>"));
            QVERIFY(xml.contains("<X509IssuerName>" + satpruebas::kIssuerDocSat.toUtf8() + "</X509IssuerName>"));
            // Sin atributos vacios: el unico ="" es Reference URI="".
            QCOMPARE(xml.count("=\"\""), 1);
            const QString motivo = verificarFirma(xml, m_material->certificadoDer, v,
                                                  SelectorNodo::porNombre(kNsServicios, caso.nodo, true));
            QVERIFY2(motivo.isEmpty(), qPrintable(QString::fromLatin1(caso.nombre) + u": " + motivo));
        }
    }
}

void TestC14nFirma::declaradaDistintaDeCalculada()
{
    // "Declara inclusiva, calcula exclusiva" (variante de prueba para la
    // corrida real): el digest corresponde a la exclusiva.
    const MaterialFirma m = m_material->material();
    OpcionesFirma o;
    o.c14n = VarianteC14n::Exclusiva;
    o.c14nDeclarada = VarianteC14n::Inclusiva;
    auto sobre = construirVerificacion(QStringLiteral("abc"), QStringLiteral("EKU9003173C9"), m, o);
    QVERIFY(sobre);
    QVERIFY(sobre.valor().xml.contains("<CanonicalizationMethod Algorithm=\"http://www.w3.org/TR/2001/REC-xml-c14n-20010315\"/>"));
    QVERIFY(!sobre.valor().xml.contains("xml-exc-c14n"));
    const QString motivo = verificarFirma(sobre.valor().xml, m_material->certificadoDer, VarianteC14n::Exclusiva,
                                          SelectorNodo::porNombre(kNsServicios, "solicitud", true));
    QVERIFY2(motivo.isEmpty(), qPrintable(motivo));
}
