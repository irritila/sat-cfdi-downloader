#include "TestSobresRespuestas.h"

#include "infrastructure/sat/EnmascaradorEvidencia.h"
#include "infrastructure/sat/RespuestasSat.h"
#include "infrastructure/sat/SobresSat.h"

#include <QDir>
#include <QRegularExpression>
#include <QTest>
#include <QTimeZone>

using namespace satcfdi;
using namespace satcfdi::sat;

namespace {

QByteArray fixture(const char* nombre)
{
    return satpruebas::leer(QStringLiteral(SATCFDI_SAT_FIXTURES "/") + QString::fromLatin1(nombre));
}

ParametrosSolicitud emitidos()
{
    ParametrosSolicitud p;
    p.rfcSolicitante = QStringLiteral("EKU9003173C9");
    p.rfcEmisor = QStringLiteral("EKU9003173C9");
    p.fechaInicial = QDateTime(QDate(2026, 9, 1), QTime(0, 0));
    p.fechaFinal = QDateTime(QDate(2026, 9, 1), QTime(23, 59, 59));
    return p;
}

ParametrosSolicitud recibidos()
{
    ParametrosSolicitud p = emitidos();
    p.rfcEmisor.clear();
    p.rfcReceptor = QStringLiteral("EKU9003173C9");
    return p;
}

Resultado<SobreSat, ErrorSobre> construir(const QString& caso, const MaterialFirma& m)
{
    if (caso.startsWith(u"autentica")) {
        ContextoSobre ctx;
        ctx.ahoraUtc = QDateTime(QDate(2026, 10, 4), QTime(12, 0, 0, 123), QTimeZone::UTC);
        ctx.idToken = QStringLiteral("uuid-00000000-0000-4000-8000-000000000001-1");
        OpcionesFirma o = OpcionesFirma::porDefecto(Operacion::Autentica);
        o.wsAddressing = caso == u"autentica_ws_addressing";
        return construirAutentica(m, ctx, o);
    }
    if (caso == u"emitidos") {
        return construirSolicitudEmitidos(emitidos(), m);
    }
    if (caso == u"recibidos") {
        return construirSolicitudRecibidos(recibidos(), m);
    }
    if (caso == u"verificacion") {
        return construirVerificacion(QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5"),
                                     QStringLiteral("EKU9003173C9"), m);
    }
    return construirDescarga(QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5_01"),
                             QStringLiteral("EKU9003173C9"), m);
}

} // namespace

void TestSobresRespuestas::initTestCase()
{
    m_material = satpruebas::generarMaterial();
    QVERIFY(m_material);
}

void TestSobresRespuestas::goldensSanitizadosDeCadaSobre_data()
{
    QTest::addColumn<QString>("caso");
    for (const char* c : {"autentica", "autentica_ws_addressing", "emitidos", "recibidos", "verificacion", "descarga"}) {
        QTest::newRow(c) << QString::fromLatin1(c);
    }
}

void TestSobresRespuestas::goldensSanitizadosDeCadaSobre()
{
    QFETCH(QString, caso);
    const MaterialFirma m = m_material->material();
    auto sobre = construir(caso, m);
    QVERIFY2(sobre, sobre ? "" : qPrintable(sobre.error().diagnostico));
    const QByteArray crudo = sobre.valor().xml;
    const QString saneado = evidencia::enmascarar(QString::fromUtf8(crudo));

    // Nada recuperable: ni certificado, ni firma, ni fragmentos largos de ellos.
    const QByteArray cert = m_material->certificadoDer.toBase64();
    const QByteArray firma = satpruebas::contenidoElemento(crudo, "SignatureValue");
    QVERIFY(firma.size() > 100);
    for (const QByteArray& secreto : {cert, firma}) {
        for (qsizetype i = 0; i + 32 <= secreto.size(); i += 32) {
            QVERIFY(!saneado.contains(QString::fromLatin1(secreto.mid(i, 32))));
        }
    }
    QVERIFY(!saneado.contains(QStringLiteral("EKU9003173C9")));
    QVERIFY(!saneado.contains(QStringLiteral("4e80345d")));
    QVERIFY(!saneado.contains(QString::fromLatin1(satpruebas::kSerialDecimal)));
    QVERIFY(!saneado.contains(QStringLiteral("ACDMA")));
    QCOMPARE(evidencia::enmascarar(saneado), saneado); // idempotente

    // Sin atributos vacios (el unico ="" es Reference URI="" en las peticiones).
    QCOMPARE(crudo.count("=\"\""), caso.startsWith(u"autentica") ? 0 : 1);
    QCOMPARE(crudo.contains("<To s:mustUnderstand=\"1\""), caso == u"autentica_ws_addressing");

    const QString ruta = QStringLiteral(SATCFDI_SAT_GOLDENS "/") + caso + QStringLiteral(".xml");
    // Solo con argumento explicito Y variable de entorno (nunca bajo ctest).
    if (satpruebas::actualizarGoldens && qEnvironmentVariableIsSet("SATCFDI_ACTUALIZAR_GOLDENS")) {
        QFile f(ruta);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(saneado.toUtf8() + '\n');
    }
    const QByteArray golden = satpruebas::leer(ruta);
    QVERIFY2(!golden.isEmpty(), qPrintable(ruta));
    QCOMPARE(saneado.toUtf8() + '\n', golden);
}

void TestSobresRespuestas::filtrosOpcionalesYOrdenDeAtributos()
{
    const MaterialFirma m = m_material->material();
    ParametrosSolicitud p = emitidos();
    p.tipoComprobante = QStringLiteral("I");
    p.complemento = QStringLiteral("nomina12");
    p.rfcReceptores = {QStringLiteral("xaxx010101000")};
    auto sobre = construirSolicitudEmitidos(p, m);
    QVERIFY(sobre);
    const QByteArray xml = sobre.valor().xml;
    QVERIFY(xml.contains("<des:solicitud Complemento=\"nomina12\" EstadoComprobante=\"Vigente\" "
                         "FechaInicial=\"2026-09-01T00:00:00\" FechaFinal=\"2026-09-01T23:59:59\" "
                         "RfcEmisor=\"EKU9003173C9\" RfcSolicitante=\"EKU9003173C9\" TipoComprobante=\"I\" "
                         "TipoSolicitud=\"CFDI\">"
                         "<des:RfcReceptores><des:RfcReceptor>XAXX010101000</des:RfcReceptor></des:RfcReceptores>"
                         "<Signature"));
    // Sin filtros opcionales: no aparecen.
    auto minimo = construirSolicitudRecibidos(recibidos(), m);
    QVERIFY(minimo);
    for (const char* ausente : {"Complemento=", "TipoComprobante=", "RfcACuentaTerceros=", "RfcEmisor=",
                                "RfcReceptores"}) {
        QVERIFY2(!minimo.valor().xml.contains(ausente), ausente);
    }
    QVERIFY(minimo.valor().xml.contains("RfcReceptor=\"EKU9003173C9\" RfcSolicitante=\"EKU9003173C9\""));
    QVERIFY(minimo.valor().xml.startsWith("<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                                          "xmlns:des=\"http://DescargaMasivaTerceros.sat.gob.mx\""));
    // Escapado de valores.
    ParametrosSolicitud amp = emitidos();
    amp.complemento = QStringLiteral("a&b\"<");
    QVERIFY(construirSolicitudEmitidos(amp, m).valor().xml.contains("Complemento=\"a&amp;b&quot;&lt;\""));
}

void TestSobresRespuestas::parametrosInvalidos()
{
    const MaterialFirma m = m_material->material();
    ParametrosSolicitud p = emitidos();
    p.rfcSolicitante = QStringLiteral("NO-RFC");
    QCOMPARE(construirSolicitudEmitidos(p, m).error().tipo, ErrorSobre::Tipo::Parametros);
    p = emitidos();
    p.fechaFinal = p.fechaInicial.addDays(-1);
    QCOMPARE(construirSolicitudEmitidos(p, m).error().diagnostico, QStringLiteral("solicitud.fechas"));
    QCOMPARE(construirSolicitudRecibidos(emitidos(), m).error().diagnostico, QStringLiteral("solicitud.recibidos_rfc"));
    QCOMPARE(construirSolicitudEmitidos(recibidos(), m).error().diagnostico, QStringLiteral("solicitud.emitidos_rfc"));
    QVERIFY(!construirVerificacion(QStringLiteral(" "), QStringLiteral("EKU9003173C9"), m));
    QVERIFY(!construirDescarga(QStringLiteral("x"), QStringLiteral("x"), m));
    QVERIFY(!construirAutentica(m, ContextoSobre{}));
    // Material sin llave valida: error de firma, no excepcion.
    const MaterialFirma roto(BufferSecreto::desdeBytes(m_material->certificadoDer.constData(),
                                                       static_cast<std::size_t>(m_material->certificadoDer.size())),
                             BufferSecreto::desdeBytes("no", 2), BufferSecreto());
    QCOMPARE(construirVerificacion(QStringLiteral("a"), QStringLiteral("EKU9003173C9"), roto).error().tipo,
             ErrorSobre::Tipo::Firma);
}

void TestSobresRespuestas::parsearAutenticaConTtl()
{
    auto r = parsearAutentica(fixture("autentica_ok.xml"));
    QVERIFY(r);
    const TokenSat& t = r.valor().token;
    QVERIFY(!t.vacio());
    QCOMPARE(t.creado(), QDateTime(QDate(2026, 10, 4), QTime(12, 0, 0, 123), QTimeZone::UTC));
    QCOMPARE(t.expira(), QDateTime(QDate(2026, 10, 4), QTime(12, 5, 0, 123), QTimeZone::UTC));
    QVERIFY(t.vigenteEn(t.creado()));
    QVERIFY(!t.vigenteEn(t.expira()));
    QVERIFY(t.encabezadoAutorizacion().startsWith("WRAP access_token=\"eyJhbGci"));
    QVERIFY(t.encabezadoAutorizacion().endsWith("\""));
    static_assert(!std::is_copy_constructible_v<TokenSat>);
}

void TestSobresRespuestas::parsearSolicitudes()
{
    auto ok = parsearSolicitud(fixture("solicitud_emitidos_ok.xml"));
    QVERIFY(ok);
    QCOMPARE(ok.valor().idSolicitud, QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5"));
    QCOMPARE(ok.valor().rfcSolicitante, QStringLiteral("EKU9003173C9"));
    QCOMPARE(ok.valor().codEstatus, QStringLiteral("5000"));
    QCOMPARE(ok.valor().mensaje, QStringLiteral("Solicitud Aceptada"));
    auto rechazo = parsearSolicitud(fixture("solicitud_recibidos_rechazo.xml"));
    QVERIFY(rechazo);
    QCOMPARE(rechazo.valor().codEstatus, QStringLiteral("301"));
    QVERIFY(rechazo.valor().idSolicitud.isEmpty());
}

void TestSobresRespuestas::parsearVerificacionSinConfundirCodigos()
{
    auto t = parsearVerificacion(fixture("verificacion_terminada.xml"));
    QVERIFY(t);
    QCOMPARE(t.valor().codEstatus, QStringLiteral("5000"));
    QCOMPARE(t.valor().estadoSolicitud, std::optional<int>(3));
    QCOMPARE(t.valor().codigoEstadoSolicitud, QStringLiteral("5000"));
    QCOMPARE(t.valor().numeroCfdis, std::optional<int>(12));
    QCOMPARE(t.valor().idsPaquetes, (QStringList{QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5_01"),
                                                 QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5_02")}));
    auto d = parsearVerificacion(fixture("verificacion_codigos_distintos.xml"));
    QVERIFY(d);
    QCOMPARE(d.valor().codEstatus, QStringLiteral("5000"));
    QCOMPARE(d.valor().estadoSolicitud, std::optional<int>(5));
    QCOMPARE(d.valor().codigoEstadoSolicitud, QStringLiteral("5002"));
    QCOMPARE(d.valor().numeroCfdis, std::optional<int>(0));
    QVERIFY(d.valor().idsPaquetes.isEmpty());
}

void TestSobresRespuestas::parsearDescargaConPaquete()
{
    auto ok = parsearDescarga(fixture("descarga_ok.xml"));
    QVERIFY(ok);
    QCOMPARE(ok.valor().codEstatus, QStringLiteral("5000"));
    QCOMPARE(ok.valor().tamanoPaquete, qsizetype(64));
    QVERIFY(ok.valor().paquete.startsWith("PK\x03\x04"));
    auto vencido = parsearDescarga(fixture("descarga_vencido.xml"));
    QVERIFY(vencido);
    QCOMPARE(vencido.valor().codEstatus, QStringLiteral("5007"));
    QCOMPARE(vencido.valor().tamanoPaquete, qsizetype(0));
}

void TestSobresRespuestas::parsearFaultSintetico()
{
    const QByteArray cuerpo = fixture("fault_sintetico.xml");
    QVERIFY(cuerpo.contains("sintetico"));
    for (const auto& error : {parsearAutentica(cuerpo).error(), ErrorRespuesta(parsearSolicitud(cuerpo).error()),
                              ErrorRespuesta(parsearVerificacion(cuerpo).error()),
                              ErrorRespuesta(parsearDescarga(cuerpo).error())}) {
        QCOMPARE(error.tipo, ErrorRespuesta::Tipo::Fault);
        QVERIFY(error.fault);
        QCOMPARE(error.fault->codigo, QStringLiteral("a:InvalidSecurity"));
        QCOMPARE(error.fault->mensaje, QStringLiteral("An error occurred when verifying security for the message."));
        QCOMPARE(error.fault->detalle, QStringLiteral("Detalle sintetico"));
    }
    QVERIFY(parsearFault(cuerpo));
    QVERIFY(!parsearFault(fixture("verificacion_terminada.xml")));
}

void TestSobresRespuestas::respuestasInvalidas()
{
    QCOMPARE(parsearVerificacion("<a>").error().tipo, ErrorRespuesta::Tipo::XmlMalFormado);
    QCOMPARE(parsearSolicitud(fixture("verificacion_terminada.xml")).error().tipo, ErrorRespuesta::Tipo::SinResultado);
    QCOMPARE(parsearVerificacion("<r xmlns=\"http://DescargaMasivaTerceros.sat.gob.mx\">"
                                 "<VerificaSolicitudDescargaResponse><VerificaSolicitudDescargaResult "
                                 "EstadoSolicitud=\"x\"/></VerificaSolicitudDescargaResponse></r>")
                 .error()
                 .tipo,
             ErrorRespuesta::Tipo::ValorInvalido);
    QCOMPARE(parsearAutentica("<AutenticaResponse xmlns=\"http://DescargaMasivaTerceros.gob.mx\"><AutenticaResult/>"
                              "</AutenticaResponse>")
                 .error()
                 .tipo,
             ErrorRespuesta::Tipo::ValorInvalido);
    QCOMPARE(parsearAutentica("<AutenticaResponse xmlns=\"http://DescargaMasivaTerceros.gob.mx\"><AutenticaResult>"
                              "a<b/>c</AutenticaResult></AutenticaResponse>")
                 .error()
                 .tipo,
             ErrorRespuesta::Tipo::ValorInvalido);
    QCOMPARE(parsearDescarga("<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                             "xmlns:h=\"http://DescargaMasivaTerceros.sat.gob.mx\"><s:Header><h:respuesta "
                             "CodEstatus=\"5000\"/></s:Header><s:Body><h:RespuestaDescargaMasivaTercerosSalida>"
                             "<h:Paquete>@@@</h:Paquete></h:RespuestaDescargaMasivaTercerosSalida></s:Body></s:Envelope>")
                 .error()
                 .tipo,
             ErrorRespuesta::Tipo::ValorInvalido);
    QCOMPARE(parsearAutentica("<!DOCTYPE r [<!ENTITY x \"y\">]><r>&x;</r>").error().tipo,
             ErrorRespuesta::Tipo::XmlMalFormado);
}

void TestSobresRespuestas::enmascaradoControlPositivoEIdempotente()
{
    const QString token = QStringLiteral("eyJhbGciOiJodHRwOi8vZmljdGljaW8iLCJ0eXAiOiJKV1QifQ.ZmljdGljaW8.ZmlybWE");
    const QString largo = QString(240, QLatin1Char('Q'));
    const QString evidenciaCruda =
        QStringLiteral("Authorization: WRAP access_token=\"") + token + QStringLiteral("\"\n")
        + QStringLiteral("<AutenticaResult>") + token + QStringLiteral("</AutenticaResult>")
        + QStringLiteral("<o:BinarySecurityToken u:Id=\"x\">MIIBcert</o:BinarySecurityToken>")
        + QStringLiteral("<SignatureValue>c2lnbmF0dXJl</SignatureValue><X509Certificate>MIIBcert2</X509Certificate>")
        + QStringLiteral("<X509IssuerName>OID.2.5.4.45=SAT970701NN3, CN=AC</X509IssuerName>")
        + QStringLiteral("<X509SerialNumber>292233162870206001759766198425879490509300</X509SerialNumber>")
        + QStringLiteral("<Paquete>UEsDBA==</Paquete><IdsPaquetes>ABCDEF01-917F-40BB-A98F-4A73939343C5_01</IdsPaquetes>")
        + QStringLiteral("<x IdSolicitud=\"4E80345D-917F-40BB-A98F-4A73939343C5\" IdPaquete=\"p-1\" "
                         "RfcSolicitante=\"EKU9003173C9\" RfcEmisor=\"AAA010101AAA\" Vacio=\"\"/>")
        + QStringLiteral("texto libre con RFC GODE561231GR8, id 4e80345d-917f-40bb-a98f-4a73939343c5 y ") + largo;
    // Control positivo: la entrada SI contiene los valores sensibles.
    for (const QString& v : {token, QStringLiteral("MIIBcert"), QStringLiteral("c2lnbmF0dXJl"),
                             QStringLiteral("SAT970701NN3"), QStringLiteral("292233162870206001759766198425879490509300"),
                             QStringLiteral("UEsDBA=="), QStringLiteral("EKU9003173C9"), QStringLiteral("GODE561231GR8"),
                             QStringLiteral("4E80345D"), QStringLiteral("4e80345d"), QStringLiteral("p-1"), largo}) {
        QVERIFY(evidenciaCruda.contains(v));
    }
    const QString saneado = evidencia::enmascarar(evidenciaCruda);
    for (const QString& v : {token, QStringLiteral("MIIBcert"), QStringLiteral("c2lnbmF0dXJl"),
                             QStringLiteral("SAT970701NN3"), QStringLiteral("292233162870206001759766198425879490509300"),
                             QStringLiteral("UEsDBA=="), QStringLiteral("EKU9003173C9"), QStringLiteral("GODE561231GR8"),
                             QStringLiteral("4E80345D"), QStringLiteral("4e80345d"), QStringLiteral("p-1"), largo}) {
        QVERIFY2(!saneado.contains(v), qPrintable(v.left(20)));
    }
    for (const char* marcador : {"WRAP access_token=\"[token]\"", "<AutenticaResult>[token]</AutenticaResult>",
                                 "[certificado]", "<SignatureValue>[firma]</SignatureValue>", "[issuer]", "[serial]",
                                 "<Paquete>[paquete:4 bytes]</Paquete>", "<IdsPaquetes>[IdPaquete]</IdsPaquetes>",
                                 "IdSolicitud=\"[IdSolicitud]\"", "IdPaquete=\"[IdPaquete]\"", "RfcSolicitante=\"[RFC]\"",
                                 "RfcEmisor=\"[RFC]\"", "Vacio=\"\"", "RFC [RFC]", "id [Id]", "[base64:240]"}) {
        QVERIFY2(saneado.contains(QString::fromUtf8(marcador)), marcador);
    }
    QCOMPARE(evidencia::enmascarar(saneado), saneado);
    QCOMPARE(evidencia::rfcParaConsola(u"EKU9003173C9"), QStringLiteral("EK*********9"));
    QCOMPARE(evidencia::enmascarar(evidencia::rfcParaConsola(u"EKU9003173C9")), QStringLiteral("EK*********9"));
}

void TestSobresRespuestas::enmascaradoDeSobreConMaterialReal()
{
    // Respuestas reales tipicas: el token y el paquete desaparecen.
    const QString auth = evidencia::enmascarar(QString::fromUtf8(fixture("autentica_ok.xml")));
    QVERIFY(!auth.contains(QStringLiteral("eyJhbGci")));
    QVERIFY(auth.contains(QStringLiteral("<AutenticaResult>[token]</AutenticaResult>")));
    const QString desc = evidencia::enmascarar(QString::fromUtf8(fixture("descarga_ok.xml")));
    QVERIFY(desc.contains(QStringLiteral("<Paquete>[paquete:64 bytes]</Paquete>")));
    const QString verif = evidencia::enmascarar(QString::fromUtf8(fixture("verificacion_terminada.xml")));
    QVERIFY(!verif.contains(QStringLiteral("4e80345d")));
    QVERIFY(verif.contains(QStringLiteral("CodEstatus=\"5000\" EstadoSolicitud=\"3\" CodigoEstadoSolicitud=\"5000\"")));
}

void TestSobresRespuestas::parserExigeNamespaceYContenedor()
{
    QCOMPARE(parsearVerificacion(fixture("verificacion_namespace_incorrecto.xml")).error().tipo,
             ErrorRespuesta::Tipo::SinResultado);
    auto ids = parsearVerificacion(fixture("verificacion_ids_fuera_del_resultado.xml"));
    QVERIFY(ids);
    QCOMPARE(ids.valor().idsPaquetes, QStringList{QStringLiteral("dentro-1")});
    QCOMPARE(parsearSolicitud(fixture("solicitud_contenedor_incorrecto.xml")).error().tipo,
             ErrorRespuesta::Tipo::SinResultado);
    QCOMPARE(parsearAutentica(fixture("autentica_namespace_incorrecto.xml")).error().tipo,
             ErrorRespuesta::Tipo::SinResultado);
    auto desc = parsearDescarga(fixture("descarga_paquete_fuera_del_contenedor.xml"));
    QVERIFY(desc);
    QCOMPARE(desc.valor().codEstatus, QStringLiteral("5000")); // el del Header, no el del Body
    QCOMPARE(desc.valor().tamanoPaquete, qsizetype(0));        // Paquete fuera de su contenedor
}

void TestSobresRespuestas::autenticaLimpiaElCuerpo()
{
    QByteArray cuerpo = fixture("autentica_ok.xml");
    QVERIFY(cuerpo.contains("eyJhbGci"));
    auto r = parsearAutenticaYLimpiar(cuerpo);
    QVERIFY(r);
    QVERIFY(cuerpo.isEmpty());
    QVERIFY(r.valor().token.encabezadoAutorizacion().contains("eyJhbGci"));
    // Compartido: no se sobrescribe la otra copia, solo se suelta la propia.
    QByteArray original = fixture("autentica_ok.xml");
    QByteArray compartido = original;
    QVERIFY(parsearAutenticaYLimpiar(compartido));
    QVERIFY(compartido.isEmpty());
    QVERIFY(original.contains("eyJhbGci"));
}

void TestSobresRespuestas::enmascaradoPorContextoIdsOpacos()
{
    const QStringList crudos{
        QStringLiteral("IdSolicitud: ABC"),
        QStringLiteral("IdPaquete: paquete-interno"),
        QStringLiteral("idpaquete=paquete_02 | estado=ok"),
        QStringLiteral("IdsPaquetes: p-1, p-2, p-3"),
        QStringLiteral("RfcSolicitante=\"XEXX\" y RfcEmisor: 'mi rfc'"),
        QStringLiteral("RfcReceptores: A1, B2 RfcACuentaTerceros=Z9"),
        QStringLiteral("<IdSolicitud>ABC</IdSolicitud><IdPaquete>paquete-interno</IdPaquete>"
                       "<RfcSolicitante>XEXX</RfcSolicitante>"),
        QStringLiteral("<x IdSolicitud=\"ABC\" IdPaquete=\"paquete-interno\"/>"),
    };
    const QStringList secretos{QStringLiteral("ABC"), QStringLiteral("paquete-interno"), QStringLiteral("paquete_02"),
                               QStringLiteral("p-1"), QStringLiteral("p-2"), QStringLiteral("p-3"),
                               QStringLiteral("XEXX"), QStringLiteral("mi rfc"), QStringLiteral("A1"),
                               QStringLiteral("B2"), QStringLiteral("Z9")};
    const QString todo = crudos.join(QLatin1Char('\n'));
    // Control positivo: cada valor esta en la entrada y NO es UUID ni RFC.
    for (const QString& v : secretos) {
        QVERIFY2(todo.contains(v), qPrintable(v));
    }
    const QString saneado = evidencia::enmascarar(todo);
    for (const QString& v : secretos) {
        QVERIFY2(!saneado.contains(v), qPrintable(v + QStringLiteral(" en ") + saneado));
    }
    for (const char* esperado : {"IdSolicitud: [IdSolicitud]", "IdPaquete: [IdPaquete]", "idpaquete=[IdPaquete] | estado=ok",
                                 "IdsPaquetes: [IdPaquete]", "RfcSolicitante=\"[RFC]\"", "RfcEmisor: '[RFC]'",
                                 "RfcReceptores: [RFC] RfcACuentaTerceros=[RFC]", "<IdSolicitud>[IdSolicitud]</IdSolicitud>",
                                 "<IdPaquete>[IdPaquete]</IdPaquete>", "<RfcSolicitante>[RFC]</RfcSolicitante>",
                                 "IdSolicitud=\"[IdSolicitud]\" IdPaquete=\"[IdPaquete]\""}) {
        QVERIFY2(saneado.contains(QString::fromUtf8(esperado)), esperado);
    }
    QCOMPARE(evidencia::enmascarar(saneado), saneado);

    // Valores conocidos por la CLI en textos sin clave (p. ej. rutas).
    const QString ruta = QStringLiteral("/salida/paquete-interno.zip y paquete-interno-2.zip");
    const QString oculta = evidencia::ocultarValores(ruta, {QStringLiteral("paquete-interno"),
                                                            QStringLiteral("paquete-interno-2"), QString()});
    QCOMPARE(oculta, QStringLiteral("/salida/[oculto].zip y [oculto].zip"));
    QCOMPARE(evidencia::ocultarValores(oculta, {QStringLiteral("paquete-interno")}), oculta);
    QCOMPARE(evidencia::enmascarar(oculta), oculta);
    QCOMPARE(evidencia::enmascararId(u"paquete-interno"), QStringLiteral("[Id]"));
    QCOMPARE(evidencia::enmascararId(u"  "), QString());
}
