#include "TestSatGatewayProductivo.h"

#include "SatPruebasComun.h"
#include "ServidorHttpPrueba.h"

#include "fakes/FakePackageStorage.h"
#include "infrastructure/sat/SaneadoRespuestaSat.h"
#include "infrastructure/sat/SatGatewayProductivo.h"

#include <QElapsedTimer>
#include <QTest>
#include <QThread>

#include <atomic>
#include <memory>

using namespace satcfdi;
using satpruebas::ServidorPrueba;
using Modo = satpruebas::ServidorPrueba::Modo;

namespace {

std::optional<satpruebas::MaterialPrueba> g_material;

const QString kRfc = QStringLiteral("AAA010101AAA");
const QString kContraparte = QStringLiteral("BBB020202BBB");
const QString kIdSolicitud = QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5");
const QString kIdPaquete = QStringLiteral("4E80345D-917F-40BB-A98F-4A73939343C5_01");
const QByteArray kToken = QByteArrayLiteral("tok.en-ficticio-9f2c");

QByteArray fixture(const char* nombre)
{
    return satpruebas::leer(QStringLiteral(SATCFDI_SAT_FIXTURES "/") + QString::fromLatin1(nombre));
}

MaterialFirma material()
{
    return g_material->material();
}

TokenSat token()
{
    const QDateTime ahora = QDateTime::currentDateTimeUtc();
    return TokenSat(QString::fromLatin1(kToken), ahora, ahora.addSecs(300));
}

SatGatewayOptions opciones(quint16 puerto, bool https = false)
{
    SatGatewayOptions o;
    o.permitirEndpointsDePrueba = true; // servidor local (el root nunca lo activa)
    for (sat::Operacion op : sat::kOperaciones) {
        o.endpoints[op] = QUrl(QStringLiteral("%1://127.0.0.1:%2/Servicio.svc").arg(https ? u"https" : u"http").arg(puerto));
    }
    return o;
}

SolicitudSat solicitud(TipoDescarga tipo, QStringList contrapartes = {})
{
    SolicitudSat s;
    s.tipo = tipo;
    s.rfcSolicitante = kRfc;
    s.fechaInicial = QStringLiteral("2026-09-01T00:00:00");
    s.fechaFinal = QStringLiteral("2026-09-01T23:59:59");
    s.contrapartes = std::move(contrapartes);
    return s;
}

// Envelope de Descargar (forma de descarga_ok.xml) con un Paquete arbitrario.
QByteArray envelopeDescarga(const QByteArray& base64, const QByteArray& cod = "5000")
{
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
           "<s:Header><h:respuesta CodEstatus=\"" + cod + "\" Mensaje=\"Solicitud Aceptada\" "
           "xmlns:h=\"http://DescargaMasivaTerceros.sat.gob.mx\"/></s:Header><s:Body>"
           "<RespuestaDescargaMasivaTercerosSalida xmlns=\"http://DescargaMasivaTerceros.sat.gob.mx\"><Paquete>"
           + base64 + "</Paquete></RespuestaDescargaMasivaTercerosSalida></s:Body></s:Envelope>";
}

// Valores sensibles que un servidor podria reflejar.
const QString kRfcCompleto = QStringLiteral("XAXX010101000");
const QString kTokenReflejado = QStringLiteral("eyJhbGciOiJSUzI1NiJ9.c2VjcmV0by1yZWZsZWphZG8.ZmlybWE");
QByteArray base64Largo()
{
    return QByteArray(600, 'Q').toBase64();
}
QByteArray textoSensible()
{
    return "RFC " + kRfcCompleto.toUtf8() + " WRAP access_token=\"" + kTokenReflejado.toUtf8() + "\" " + base64Largo();
}

QByteArray faultSensible()
{
    return "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><s:Fault><faultcode>"
           + textoSensible() + "</faultcode><faultstring>" + textoSensible() + "</faultstring><detail>"
           + textoSensible() + "</detail></s:Fault></s:Body></s:Envelope>";
}

void verificarSinSensibles(const QString& texto)
{
    QVERIFY2(!texto.contains(kRfcCompleto), qPrintable(texto));
    QVERIFY2(!texto.contains(kTokenReflejado), qPrintable(texto));
    QVERIFY2(!texto.contains(QString::fromLatin1(base64Largo().left(60))), qPrintable(texto));
    QVERIFY2(!texto.contains(QStringLiteral("access_token=\"e")), qPrintable(texto));
}

void verificarErrorLimpio(const ErrorSatGateway& e)
{
    verificarSinSensibles(e.diagnosticoSanitizado);
    if (e.codigoSat) {
        verificarSinSensibles(*e.codigoSat);
    }
}

QByteArray cabecera(const QByteArray& cabeceras, const QByteArray& nombre)
{
    for (const QByteArray& linea : cabeceras.split('\n')) {
        if (linea.toLower().startsWith(nombre.toLower() + ':')) {
            return linea.mid(nombre.size() + 1).trimmed();
        }
    }
    return {};
}

// Receptor que guarda via FakePackageStorage, como lo hara OperacionesSat.
struct ReceptorAlmacen final : ReceptorPaqueteSat {
    fakes::FakePackageStorage storage;
    Cancelacion cancelacion;
    int invocaciones = 0;
    int chunks = 0;
    qsizetype chunkMaximo = 0;
    std::optional<Resultado<ArchivoFinal, ErrorGuardarZip>> resultado;

    // Cuenta chunks antes de pasarlos al almacenamiento.
    struct Contador final : FuenteZipPorChunks {
        FuenteZipPorChunks& base;
        ReceptorAlmacen& r;
        Contador(FuenteZipPorChunks& b, ReceptorAlmacen& rr) : base(b), r(rr) {}
        Resultado<std::optional<QByteArray>, ErrorFuenteZip> siguiente() override
        {
            auto c = base.siguiente();
            if (c && c.valor()) {
                ++r.chunks;
                r.chunkMaximo = std::max(r.chunkMaximo, c.valor()->size());
            }
            return c;
        }
    };

    void recibir(FuenteZipPorChunks& paquete) override
    {
        ++invocaciones;
        Contador contador(paquete, *this);
        const UbicacionPaquete u{kRfc, QStringLiteral("2026-09-01T00:00:00"),
                                 QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e"), kIdPaquete};
        resultado.emplace(storage.guardarAtomico(u, contador, cancelacion));
    }
};

} // namespace

void TestSatGatewayProductivo::initTestCase()
{
    g_material = satpruebas::generarMaterial();
    QVERIFY(g_material);
}

void TestSatGatewayProductivo::autenticaHeadersYToken()
{
    ServidorPrueba s(Modo::Responder);
    s.cuerpo = fixture("autentica_ok.xml");
    SatGatewayProductivo gw(opciones(s.puerto()));
    const auto r = gw.autenticar(material(), Cancelacion());
    QVERIFY2(r, r ? "" : qPrintable(r.error().diagnosticoSanitizado));
    QCOMPARE(r.valor().expira(), QDateTime(QDate(2026, 10, 4), QTime(12, 5, 0, 123), QTimeZone::UTC));
    QCOMPARE(r.valor().creado().secsTo(r.valor().expira()), 300);
    QCOMPARE(cabecera(s.ultimaCabecera, "SOAPAction"),
             QByteArrayLiteral("\"http://DescargaMasivaTerceros.gob.mx/IAutenticacion/Autentica\""));
    QVERIFY(cabecera(s.ultimaCabecera, "Authorization").isEmpty());
    QVERIFY(cabecera(s.ultimaCabecera, "Content-Type").startsWith("text/xml"));
    QVERIFY(s.ultimoCuerpo.contains("<Autentica xmlns=\"http://DescargaMasivaTerceros.gob.mx\""));
    QVERIFY(s.ultimoCuerpo.contains("xml-exc-c14n")); // firma confirmada por T006
}

void TestSatGatewayProductivo::operacionesConTokenWrapYSoapAction()
{
    ServidorPrueba s(Modo::Responder);
    s.respuestas = {{200, fixture("solicitud_emitidos_ok.xml")},
                    {200, fixture("verificacion_terminada.xml")},
                    {200, fixture("descarga_ok.xml")}};
    SatGatewayProductivo gw(opciones(s.puerto()));
    const TokenSat t = token();
    QVERIFY(gw.crearSolicitud(t, solicitud(TipoDescarga::Emitidos), material(), Cancelacion()));
    QVERIFY(gw.verificarSolicitud(t, {kIdSolicitud, kRfc}, material(), Cancelacion()));
    ReceptorAlmacen receptor;
    QVERIFY(gw.descargarPaquete(t, {kIdPaquete, kRfc}, material(), receptor, Cancelacion()));
    QCOMPARE(s.cabeceras.size(), 3);
    const QByteArray wrap = "WRAP access_token=\"" + kToken + '"';
    const QList<QByteArray> acciones = {
        "\"http://DescargaMasivaTerceros.sat.gob.mx/ISolicitaDescargaService/SolicitaDescargaEmitidos\"",
        "\"http://DescargaMasivaTerceros.sat.gob.mx/IVerificaSolicitudDescargaService/VerificaSolicitudDescarga\"",
        "\"http://DescargaMasivaTerceros.sat.gob.mx/IDescargaMasivaTercerosService/Descargar\""};
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(cabecera(s.cabeceras.at(i), "Authorization"), wrap);
        QCOMPARE(cabecera(s.cabeceras.at(i), "SOAPAction"), acciones.at(i));
    }
    QVERIFY(!s.ultimoCuerpo.contains("Addressing")); // sin WS-Addressing
    QVERIFY(s.ultimoCuerpo.contains("X509IssuerSerial"));
}

void TestSatGatewayProductivo::emitidosYRecibidosAtributosAdr0013()
{
    ServidorPrueba s(Modo::Responder);
    s.cuerpo = fixture("solicitud_emitidos_ok.xml");
    SatGatewayProductivo gw(opciones(s.puerto()));
    const TokenSat t = token();

    QVERIFY(gw.crearSolicitud(t, solicitud(TipoDescarga::Emitidos, {kContraparte}), material(), Cancelacion()));
    const QByteArray emitidos = s.ultimoCuerpo;
    QVERIFY(cabecera(s.ultimaCabecera, "SOAPAction").endsWith("/SolicitaDescargaEmitidos\""));
    QVERIFY(emitidos.contains("<des:SolicitaDescargaEmitidos"));
    QVERIFY(!emitidos.contains("SolicitaDescargaRecibidos"));
    QVERIFY(emitidos.contains("RfcEmisor=\"" + kRfc.toUtf8() + '"'));
    QVERIFY(emitidos.contains("RfcSolicitante=\"" + kRfc.toUtf8() + '"'));
    QVERIFY(!emitidos.contains("RfcReceptor=\""));
    QVERIFY(emitidos.contains("<des:RfcReceptor>" + kContraparte.toUtf8() + "</des:RfcReceptor>"));
    QVERIFY(emitidos.contains("TipoSolicitud=\"CFDI\""));
    QVERIFY(emitidos.contains("EstadoComprobante=\"Vigente\""));
    QVERIFY(emitidos.contains("FechaInicial=\"2026-09-01T00:00:00\""));
    QVERIFY(emitidos.contains("FechaFinal=\"2026-09-01T23:59:59\""));
    QVERIFY(!emitidos.contains("RfcACuentaTerceros"));

    QVERIFY(gw.crearSolicitud(t, solicitud(TipoDescarga::Recibidos, {kContraparte}), material(), Cancelacion()));
    const QByteArray recibidos = s.ultimoCuerpo;
    QVERIFY(cabecera(s.ultimaCabecera, "SOAPAction").endsWith("/SolicitaDescargaRecibidos\""));
    QVERIFY(recibidos.contains("<des:SolicitaDescargaRecibidos"));
    QVERIFY(!recibidos.contains("SolicitaDescargaEmitidos"));
    QVERIFY(recibidos.contains("RfcReceptor=\"" + kRfc.toUtf8() + '"'));
    QVERIFY(recibidos.contains("RfcEmisor=\"" + kContraparte.toUtf8() + '"'));
    QVERIFY(recibidos.contains("RfcSolicitante=\"" + kRfc.toUtf8() + '"'));
    QVERIFY(!recibidos.contains("RfcReceptores"));
    QVERIFY(recibidos.contains("EstadoComprobante=\"Vigente\""));

    // Recibidos sin contraparte: sin RfcEmisor.
    QVERIFY(gw.crearSolicitud(t, solicitud(TipoDescarga::Recibidos), material(), Cancelacion()));
    QVERIFY(!s.ultimoCuerpo.contains("RfcEmisor="));
    // Recibidos con dos contrapartes: Preparacion, sin trafico.
    const int antes = s.peticiones;
    const auto dos = gw.crearSolicitud(t, solicitud(TipoDescarga::Recibidos, {kContraparte, kRfc}), material(),
                                       Cancelacion());
    QVERIFY(!dos);
    QCOMPARE(dos.error().fase, FaseSatGateway::Preparacion);
    QCOMPARE(s.peticiones, antes);
}

void TestSatGatewayProductivo::respuestasExplicitasCreacionYVerificacion()
{
    ServidorPrueba s(Modo::Responder);
    s.respuestas = {{200, fixture("solicitud_emitidos_ok.xml")},
                    {200, fixture("solicitud_recibidos_rechazo.xml")},
                    {200, fixture("verificacion_terminada.xml")}};
    SatGatewayProductivo gw(opciones(s.puerto()));
    const TokenSat t = token();
    const auto ok = gw.crearSolicitud(t, solicitud(TipoDescarga::Emitidos), material(), Cancelacion());
    QVERIFY(ok);
    QCOMPARE(ok.valor().codEstatus, QStringLiteral("5000"));
    QCOMPARE(ok.valor().idSolicitud, std::optional<QString>(kIdSolicitud));
    const auto rechazo = gw.crearSolicitud(t, solicitud(TipoDescarga::Recibidos), material(), Cancelacion());
    QVERIFY(rechazo);
    QCOMPARE(rechazo.valor().codEstatus, QStringLiteral("301"));
    QCOMPARE(rechazo.valor().mensaje, QStringLiteral("XML Mal Formado"));
    QVERIFY(!rechazo.valor().idSolicitud);
    const auto v = gw.verificarSolicitud(t, {kIdSolicitud, kRfc}, material(), Cancelacion());
    QVERIFY(v);
    QCOMPARE(v.valor().codEstatus, QStringLiteral("5000"));
    QCOMPARE(v.valor().estadoSolicitud, std::optional<int>(3));
    QCOMPARE(v.valor().codigoEstadoSolicitud, std::optional<QString>(QStringLiteral("5000")));
    QCOMPARE(v.valor().numeroCfdi, std::optional<qint64>(12));
    QCOMPARE(v.valor().idsPaquete.size(), 2);
}

void TestSatGatewayProductivo::faultYHttpNo200()
{
    ServidorPrueba s(Modo::Responder);
    s.respuestas = {{500, fixture("fault_sintetico.xml")},
                    {500, fixture("fault_sintetico.xml")},
                    {401, QByteArray()},
                    {503, QByteArrayLiteral("<html>no disponible</html>")},
                    {200, QByteArrayLiteral("<no-es-soap/>")}};
    SatGatewayProductivo gw(opciones(s.puerto()));
    const TokenSat t = token();
    // Fault de seguridad en una operacion con token: rechazo INFERIDO.
    const auto fault = gw.verificarSolicitud(t, {kIdSolicitud, kRfc}, material(), Cancelacion());
    QVERIFY(!fault);
    QCOMPARE(fault.error().fase, FaseSatGateway::RespuestaExplicita);
    QCOMPARE(fault.error().estadoHttp, std::optional<int>(500));
    QCOMPARE(fault.error().codigoSat, std::optional<QString>(QStringLiteral("InvalidSecurity"))); // sin prefijo
    QVERIFY(fault.error().tokenRechazado);
    // El mismo Fault en Autentica no es "token rechazado" (no hay token).
    const auto auth = gw.autenticar(material(), Cancelacion());
    QVERIFY(!auth);
    QCOMPARE(auth.error().fase, FaseSatGateway::RespuestaExplicita);
    QVERIFY(!auth.error().tokenRechazado);
    // HTTP 401 sin cuerpo: rechazo inferido.
    const auto http401 = gw.crearSolicitud(t, solicitud(TipoDescarga::Emitidos), material(), Cancelacion());
    QVERIFY(!http401);
    QCOMPARE(http401.error().estadoHttp, std::optional<int>(401));
    QVERIFY(http401.error().tokenRechazado);
    // HTTP no 200 sin Fault y 200 sin resultado: RespuestaExplicita.
    const auto http503 = gw.verificarSolicitud(t, {kIdSolicitud, kRfc}, material(), Cancelacion());
    QVERIFY(!http503);
    QCOMPARE(http503.error().estadoHttp, std::optional<int>(503));
    QVERIFY(!http503.error().codigoSat && !http503.error().tokenRechazado);
    const auto basura = gw.verificarSolicitud(t, {kIdSolicitud, kRfc}, material(), Cancelacion());
    QVERIFY(!basura);
    QCOMPARE(basura.error().fase, FaseSatGateway::RespuestaExplicita);
    QCOMPARE(basura.error().estadoHttp, std::optional<int>(200));
}

void TestSatGatewayProductivo::fasesYDeadlinesPorOperacion()
{
    // Conexion rechazada: AntesDeEnvio.
    quint16 cerrado = 0;
    {
        ServidorPrueba s(Modo::Responder);
        cerrado = s.puerto();
        s.cerrar();
    }
    SatGatewayProductivo rechazada(opciones(cerrado));
    const auto r1 = rechazada.verificarSolicitud(token(), {kIdSolicitud, kRfc}, material(), Cancelacion());
    QVERIFY(!r1);
    QCOMPARE(r1.error().fase, FaseSatGateway::AntesDeEnvio);
    QVERIFY(!r1.error().deadlineVencido);

    // TLS que nunca avanza: vence el deadline sin requestSent -> AntesDeEnvio.
    {
        ServidorPrueba s(Modo::SinLeer);
        SatGatewayOptions o = opciones(s.puerto(), true);
        o.timeoutVerificacion = std::chrono::milliseconds(300);
        SatGatewayProductivo gw(o);
        const auto r = gw.verificarSolicitud(token(), {kIdSolicitud, kRfc}, material(), Cancelacion());
        QVERIFY(!r);
        QCOMPARE(r.error().fase, FaseSatGateway::AntesDeEnvio);
        QVERIFY(r.error().deadlineVencido);
    }
    // Peticion enviada sin respuesta: vence el deadline de ESA operacion.
    {
        ServidorPrueba s(Modo::Silencio);
        SatGatewayOptions o = opciones(s.puerto());
        o.timeoutAutenticacion = std::chrono::milliseconds(60000);
        o.timeoutCreacion = std::chrono::milliseconds(400);
        SatGatewayProductivo gw(o);
        QElapsedTimer reloj;
        reloj.start();
        const auto r = gw.crearSolicitud(token(), solicitud(TipoDescarga::Emitidos), material(), Cancelacion());
        QVERIFY(!r);
        QCOMPARE(r.error().fase, FaseSatGateway::DespuesDeEnvio);
        QVERIFY(r.error().deadlineVencido);
        QVERIFY(reloj.elapsed() < 10000); // uso el timeout de creacion, no otro
        QCOMPARE(s.peticiones, 1);
    }
    // Corte tras recibir la peticion: DespuesDeEnvio sin deadline.
    {
        ServidorPrueba s(Modo::CerrarTrasPeticion);
        SatGatewayProductivo gw(opciones(s.puerto()));
        ReceptorAlmacen receptor;
        const auto r = gw.descargarPaquete(token(), {kIdPaquete, kRfc}, material(), receptor, Cancelacion());
        QVERIFY(!r);
        QCOMPARE(r.error().fase, FaseSatGateway::DespuesDeEnvio);
        QVERIFY(!r.error().deadlineVencido);
        QCOMPARE(receptor.invocaciones, 0);
    }
    // Valores por defecto de D7.
    const SatGatewayOptions d;
    QCOMPARE(d.timeoutAutenticacion, std::chrono::milliseconds(30000));
    QCOMPARE(d.timeoutCreacion, std::chrono::milliseconds(45000));
    QCOMPARE(d.timeoutVerificacion, std::chrono::milliseconds(30000));
    QCOMPARE(d.timeoutDescarga, std::chrono::milliseconds(300000));
}

void TestSatGatewayProductivo::httpIncompletoEsDespuesDeEnvio()
{
    ServidorPrueba s(Modo::Incompleta);
    SatGatewayProductivo gw(opciones(s.puerto()));
    const auto r = gw.crearSolicitud(token(), solicitud(TipoDescarga::Emitidos), material(), Cancelacion());
    QVERIFY(!r);
    QCOMPARE(r.error().fase, FaseSatGateway::DespuesDeEnvio);
    QCOMPARE(s.peticiones, 1);
}

void TestSatGatewayProductivo::cancelacionAbortaLaPeticion()
{
    // Cancelada antes de empezar: sin trafico.
    {
        ServidorPrueba s(Modo::Responder);
        SatGatewayProductivo gw(opciones(s.puerto()));
        const auto r = gw.autenticar(material(), Cancelacion([] { return true; }));
        QVERIFY(!r);
        QVERIFY(r.error().cancelada);
        QCOMPARE(r.error().fase, FaseSatGateway::AntesDeEnvio);
        QCOMPARE(s.conexiones, 0);
    }
    // Cancelada con la peticion ya recibida: aborta y conserva DespuesDeEnvio.
    {
        ServidorPrueba s(Modo::Silencio);
        SatGatewayOptions o = opciones(s.puerto());
        o.timeoutCreacion = std::chrono::milliseconds(60000);
        SatGatewayProductivo gw(o);
        QElapsedTimer reloj;
        reloj.start();
        const auto r = gw.crearSolicitud(token(), solicitud(TipoDescarga::Emitidos), material(),
                                         Cancelacion([&] { return s.peticiones > 0; }));
        QVERIFY(!r);
        QVERIFY(r.error().cancelada);
        QVERIFY(!r.error().deadlineVencido);
        QCOMPARE(r.error().fase, FaseSatGateway::DespuesDeEnvio);
        QVERIFY(reloj.elapsed() < 10000);
    }
}

void TestSatGatewayProductivo::descargaPorChunksAlReceptor()
{
    // Paquete grande (300 KB) para forzar varios chunks de 64 KiB.
    QByteArray zip("PK\x03\x04", 4);
    for (int i = 0; zip.size() < 300 * 1024; ++i) {
        zip.append(char(i * 31 % 251));
    }
    ServidorPrueba s(Modo::Responder);
    s.respuestas = {{200, envelopeDescarga(zip.toBase64())}, {200, fixture("descarga_ok.xml")}};
    SatGatewayProductivo gw(opciones(s.puerto()));
    ReceptorAlmacen receptor;
    const auto r = gw.descargarPaquete(token(), {kIdPaquete, kRfc}, material(), receptor, Cancelacion());
    QVERIFY2(r, r ? "" : qPrintable(r.error().diagnosticoSanitizado));
    QVERIFY(r.valor().paqueteEntregado);
    QCOMPARE(r.valor().bytesPaquete, zip.size());
    QCOMPARE(r.valor().codEstatus, QStringLiteral("5000"));
    QCOMPARE(receptor.invocaciones, 1);
    QVERIFY(receptor.chunks >= 5);
    QVERIFY(receptor.chunkMaximo <= 64 * 1024);
    QVERIFY(receptor.resultado && *receptor.resultado);
    QCOMPARE(receptor.storage.bytesFinal(receptor.resultado->valor().rutaRelativa), std::optional<QByteArray>(zip));

    // Fixture de T006 (64 bytes).
    ReceptorAlmacen otro;
    const auto r2 = gw.descargarPaquete(token(), {kIdPaquete, kRfc}, material(), otro, Cancelacion());
    QVERIFY(r2);
    QCOMPARE(r2.valor().bytesPaquete, 64);
}

void TestSatGatewayProductivo::descargaCanceladaSinArchivoFinal()
{
    // Cancelacion durante la red: el receptor nunca se invoca.
    {
        ServidorPrueba s(Modo::Silencio);
        SatGatewayProductivo gw(opciones(s.puerto()));
        ReceptorAlmacen receptor;
        const auto r = gw.descargarPaquete(token(), {kIdPaquete, kRfc}, material(), receptor,
                                           Cancelacion([&] { return s.peticiones > 0; }));
        QVERIFY(!r);
        QVERIFY(r.error().cancelada);
        QCOMPARE(r.error().fase, FaseSatGateway::DespuesDeEnvio);
        QCOMPARE(receptor.invocaciones, 0);
        QVERIFY(receptor.storage.finales().isEmpty());
    }
    // Cancelacion durante la entrega por chunks: el almacenamiento aborta.
    {
        QByteArray zip(200 * 1024, 'z');
        ServidorPrueba s(Modo::Responder);
        s.cuerpo = envelopeDescarga(zip.toBase64());
        SatGatewayProductivo gw(opciones(s.puerto()));
        ReceptorAlmacen receptor;
        receptor.cancelacion = Cancelacion([&] { return receptor.chunks >= 2; });
        const auto r = gw.descargarPaquete(token(), {kIdPaquete, kRfc}, material(), receptor, Cancelacion());
        QVERIFY(r);
        QCOMPARE(receptor.invocaciones, 1);
        QVERIFY(receptor.resultado && !*receptor.resultado);
        QCOMPARE(receptor.resultado->error().almacenamiento, ErrorAlmacenamiento::Cancelada);
        QVERIFY(receptor.storage.finales().isEmpty());
    }
}

void TestSatGatewayProductivo::descargaSinPaqueteUtilizable()
{
    ServidorPrueba s(Modo::Responder);
    s.respuestas = {{200, fixture("descarga_vencido.xml")},
                    {200, envelopeDescarga(QByteArray())},
                    {200, envelopeDescarga("@@no-es-base64@@")},
                    {200, envelopeDescarga(QByteArray(), "5008")}};
    SatGatewayProductivo gw(opciones(s.puerto()));
    ReceptorAlmacen receptor;
    const auto vencido = gw.descargarPaquete(token(), {kIdPaquete, kRfc}, material(), receptor, Cancelacion());
    QVERIFY(vencido);
    QCOMPARE(vencido.valor().codEstatus, QStringLiteral("5007"));
    QVERIFY(!vencido.valor().paqueteEntregado);
    const auto vacio = gw.descargarPaquete(token(), {kIdPaquete, kRfc}, material(), receptor, Cancelacion());
    QVERIFY(!vacio);
    QCOMPARE(vacio.error().fase, FaseSatGateway::RespuestaExplicita);
    const auto invalido = gw.descargarPaquete(token(), {kIdPaquete, kRfc}, material(), receptor, Cancelacion());
    QVERIFY(!invalido);
    QCOMPARE(invalido.error().fase, FaseSatGateway::RespuestaExplicita);
    const auto maximo = gw.descargarPaquete(token(), {kIdPaquete, kRfc}, material(), receptor, Cancelacion());
    QVERIFY(maximo);
    QCOMPARE(maximo.valor().codEstatus, QStringLiteral("5008"));
    QCOMPARE(receptor.invocaciones, 0);
}

void TestSatGatewayProductivo::preparacionSinTraficoDeRed()
{
    ServidorPrueba s(Modo::Responder);
    s.cuerpo = fixture("verificacion_terminada.xml");
    SatGatewayProductivo gw(opciones(s.puerto()));
    const TokenSat t = token();
    ReceptorAlmacen receptor;
    const auto esPreparacion = [](const auto& r) { return !r && r.error().fase == FaseSatGateway::Preparacion; };
    QVERIFY(esPreparacion(gw.verificarSolicitud(t, {QStringLiteral("no-es-uuid"), kRfc}, material(), Cancelacion())));
    QVERIFY(esPreparacion(gw.verificarSolicitud(t, {kIdSolicitud, QStringLiteral("aaa010101aaa")}, material(),
                                                Cancelacion())));
    QVERIFY(esPreparacion(gw.descargarPaquete(t, {kIdSolicitud, kRfc}, material(), receptor, Cancelacion())));
    SolicitudSat mala = solicitud(TipoDescarga::Emitidos);
    mala.fechaFinal = QStringLiteral("2026-08-01T00:00:00"); // final < inicial
    QVERIFY(esPreparacion(gw.crearSolicitud(t, mala, material(), Cancelacion())));
    mala = solicitud(TipoDescarga::Emitidos, {QStringLiteral("X")});
    QVERIFY(esPreparacion(gw.crearSolicitud(t, mala, material(), Cancelacion())));
    // Material inutilizable: no se puede firmar.
    const MaterialFirma vacio{BufferSecreto(), BufferSecreto(), BufferSecreto()};
    QVERIFY(esPreparacion(gw.autenticar(vacio, Cancelacion())));
    QVERIFY(esPreparacion(gw.verificarSolicitud(t, {kIdSolicitud, kRfc}, vacio, Cancelacion())));
    QCOMPARE(s.conexiones, 0);
}

void TestSatGatewayProductivo::hiloDelEjecutor()
{
    // Uso real: el gateway corre en un hilo propio (el del OperacionExecutor)
    // con su event loop local; el servidor vive en el hilo de la prueba.
    ServidorPrueba s(Modo::Responder);
    s.cuerpo = fixture("verificacion_terminada.xml");
    SatGatewayProductivo gw(opciones(s.puerto()));
    std::optional<Resultado<RespuestaVerificacion, ErrorSatGateway>> r;
    std::unique_ptr<QThread> hilo(QThread::create([&] {
        r.emplace(gw.verificarSolicitud(token(), {kIdSolicitud, kRfc}, material(), Cancelacion()));
    }));
    hilo->start();
    QVERIFY(QTest::qWaitFor([&] { return hilo->isFinished(); }, 15000));
    QVERIFY(r && *r);
    QCOMPARE(r->valor().estadoSolicitud, std::optional<int>(3));
}

void TestSatGatewayProductivo::diagnosticosSinSecretos()
{
    ServidorPrueba s(Modo::Responder);
    s.respuestas = {{500, fixture("fault_sintetico.xml")}, {401, kToken}};
    SatGatewayProductivo gw(opciones(s.puerto()));
    const auto a = gw.verificarSolicitud(token(), {kIdSolicitud, kRfc}, material(), Cancelacion());
    const auto b = gw.crearSolicitud(token(), solicitud(TipoDescarga::Emitidos), material(), Cancelacion());
    QVERIFY(!a && !b);
    for (const QString& d : {a.error().diagnosticoSanitizado, b.error().diagnosticoSanitizado}) {
        QVERIFY(!d.isEmpty());
        QVERIFY2(!d.contains(QString::fromLatin1(kToken)), qPrintable(d));
        QVERIFY(!d.contains(kRfc));
        QVERIFY(!d.contains(kIdSolicitud));
        QVERIFY(!d.contains(QStringLiteral("127.0.0.1")));
        QVERIFY(!d.contains(QStringLiteral("Detalle sintetico"))); // detail del Fault
        QVERIFY(!d.contains(QStringLiteral("An error occurred"))); // faultstring
    }
}

void TestSatGatewayProductivo::faultYCuerposReflejadosNoSeExponen()
{
    ServidorPrueba s(Modo::Responder);
    s.respuestas = {{500, faultSensible()},           // Fault con todo sensible
                    {500, textoSensible()},           // HTTP 500 sin Fault
                    {200, faultSensible()},           // Fault con HTTP 200
                    {401, textoSensible()},           // 401 con cuerpo reflejado
                    {500, faultSensible()}};          // Autentica
    SatGatewayProductivo gw(opciones(s.puerto()));
    const TokenSat t = token();
    const auto f500 = gw.verificarSolicitud(t, {kIdSolicitud, kRfc}, material(), Cancelacion());
    QVERIFY(!f500);
    QCOMPARE(f500.error().codigoSat, std::optional<QString>(sat::saneado::kFaultNoReconocido));
    QVERIFY(!f500.error().tokenRechazado);
    verificarErrorLimpio(f500.error());
    const auto h500 = gw.crearSolicitud(t, solicitud(TipoDescarga::Emitidos), material(), Cancelacion());
    QVERIFY(!h500);
    QVERIFY(!h500.error().codigoSat);
    verificarErrorLimpio(h500.error());
    ReceptorAlmacen receptor;
    const auto f200 = gw.descargarPaquete(t, {kIdPaquete, kRfc}, material(), receptor, Cancelacion());
    QVERIFY(!f200);
    QCOMPARE(f200.error().codigoSat, std::optional<QString>(sat::saneado::kFaultNoReconocido));
    verificarErrorLimpio(f200.error());
    const auto h401 = gw.verificarSolicitud(t, {kIdSolicitud, kRfc}, material(), Cancelacion());
    QVERIFY(!h401);
    QVERIFY(h401.error().tokenRechazado);
    verificarErrorLimpio(h401.error());
    const auto auth = gw.autenticar(material(), Cancelacion());
    QVERIFY(!auth);
    verificarErrorLimpio(auth.error());
    QCOMPARE(receptor.invocaciones, 0);
}

void TestSatGatewayProductivo::respuestasExplicitasSaneadas()
{
    const QByteArray sensibleAttr = textoSensible().replace('"', "'");
    ServidorPrueba s(Modo::Responder);
    s.respuestas = {
        {200, "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
              "<SolicitaDescargaEmitidosResponse xmlns=\"http://DescargaMasivaTerceros.sat.gob.mx\">"
              "<SolicitaDescargaEmitidosResult IdSolicitud=\"" + kRfcCompleto.toUtf8() + "\" CodEstatus=\""
              + sensibleAttr + "\" Mensaje=\"" + sensibleAttr + "\"/></SolicitaDescargaEmitidosResponse></s:Body></s:Envelope>"},
        {200, "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
              "<VerificaSolicitudDescargaResponse xmlns=\"http://DescargaMasivaTerceros.sat.gob.mx\">"
              "<VerificaSolicitudDescargaResult CodEstatus=\"5000\" EstadoSolicitud=\"3\" CodigoEstadoSolicitud=\""
              + sensibleAttr + "\" NumeroCFDIs=\"1\" Mensaje=\"" + sensibleAttr + "\"></VerificaSolicitudDescargaResult>"
              "</VerificaSolicitudDescargaResponse></s:Body></s:Envelope>"},
        {200, "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
              "<VerificaSolicitudDescargaResponse xmlns=\"http://DescargaMasivaTerceros.sat.gob.mx\">"
              "<VerificaSolicitudDescargaResult CodEstatus=\"5000\" EstadoSolicitud=\"3\" CodigoEstadoSolicitud=\"5000\" "
              "NumeroCFDIs=\"1\" Mensaje=\"ok\"><IdsPaquetes>" + kTokenReflejado.toUtf8() + "</IdsPaquetes>"
              "</VerificaSolicitudDescargaResult></VerificaSolicitudDescargaResponse></s:Body></s:Envelope>"},
        {200, envelopeDescarga(QByteArray(), "5007").replace("Mensaje=\"Solicitud Aceptada\"",
                                                            "Mensaje=\"" + sensibleAttr + "\"")},
    };
    SatGatewayProductivo gw(opciones(s.puerto()));
    const TokenSat t = token();
    const auto c = gw.crearSolicitud(t, solicitud(TipoDescarga::Emitidos), material(), Cancelacion());
    QVERIFY(c);
    QCOMPARE(c.valor().codEstatus, sat::saneado::kCodigoNoReconocido);
    QVERIFY(!c.valor().idSolicitud); // forma inesperada: ausente
    QVERIFY(c.valor().mensaje.size() <= 200);
    verificarSinSensibles(c.valor().mensaje);
    const auto v = gw.verificarSolicitud(t, {kIdSolicitud, kRfc}, material(), Cancelacion());
    QVERIFY(v);
    QCOMPARE(v.valor().codigoEstadoSolicitud, std::optional<QString>(sat::saneado::kCodigoNoReconocido));
    verificarSinSensibles(v.valor().mensaje);
    const auto ids = gw.verificarSolicitud(t, {kIdSolicitud, kRfc}, material(), Cancelacion());
    QVERIFY(!ids);
    QCOMPARE(ids.error().fase, FaseSatGateway::RespuestaExplicita);
    verificarErrorLimpio(ids.error());
    ReceptorAlmacen receptor;
    const auto d = gw.descargarPaquete(t, {kIdPaquete, kRfc}, material(), receptor, Cancelacion());
    QVERIFY(d);
    QCOMPARE(d.valor().codEstatus, QStringLiteral("5007"));
    verificarSinSensibles(d.valor().mensaje);
}

void TestSatGatewayProductivo::listaPermitidaDeCodigos()
{
    using namespace sat::saneado;
    QCOMPARE(faultcodePermitido(u"s:Client"), QStringLiteral("Client"));
    QCOMPARE(faultcodePermitido(u"a:InvalidSecurity"), QStringLiteral("InvalidSecurity"));
    QCOMPARE(faultcodePermitido(u"SecretoCorto1234:Client"), QStringLiteral("Client"));
    QCOMPARE(faultcodePermitido(u"FailedAuthentication"), QStringLiteral("FailedAuthentication"));
    QCOMPARE(faultcodePermitido(u"5004"), QStringLiteral("5004"));
    QCOMPARE(faultcodePermitido(u"a:Inventado"), kFaultNoReconocido);
    QCOMPARE(faultcodePermitido(u"XAXX010101000"), kFaultNoReconocido);
    QCOMPARE(faultcodePermitido(u"prefijo-largo-de-mas:Client"), kFaultNoReconocido);
    QCOMPARE(codigoSatPermitido(u"5000"), QStringLiteral("5000"));
    QCOMPARE(codigoSatPermitido(u"301"), QStringLiteral("301"));
    QCOMPARE(codigoSatPermitido(u"99999"), kCodigoNoReconocido);
    QCOMPARE(codigoSatPermitido(u"XAXX010101000"), kCodigoNoReconocido);
    QCOMPARE(mensajePermitido(u"Solicitud Aceptada"), QStringLiteral("Solicitud Aceptada"));
    QCOMPARE(mensajePermitido(u"XML Mal Formado"), QStringLiteral("XML Mal Formado"));
}

void TestSatGatewayProductivo::endpointsSoloOficialesEnProduccion()
{
    using sat::saneado::esEndpointOficial;
    for (sat::Operacion op : sat::kOperaciones) {
        QVERIFY(esEndpointOficial(sat::descriptor(op).endpoint));
    }
    QVERIFY(esEndpointOficial(QUrl(QStringLiteral("https://cfdidescargamasiva.clouda.sat.gob.mx/x.svc"))));
    QVERIFY(!esEndpointOficial(QUrl(QStringLiteral("http://cfdidescargamasiva.clouda.sat.gob.mx/x.svc"))));
    QVERIFY(!esEndpointOficial(QUrl(QStringLiteral("https://clouda.sat.gob.mx.evil.example/x.svc"))));
    QVERIFY(!esEndpointOficial(QUrl(QStringLiteral("https://127.0.0.1/x.svc"))));
    QVERIFY(esEndpointOficial(QUrl(QStringLiteral("https://cfdidescargamasiva.clouda.sat.gob.mx:443/x.svc"))));
    QVERIFY(!esEndpointOficial(QUrl(QStringLiteral("https://cfdidescargamasiva.clouda.sat.gob.mx:8443/x.svc"))));
    QVERIFY(!esEndpointOficial(QUrl(QStringLiteral("https://cfdidescargamasiva.clouda.sat.gob.mx:80/x.svc"))));

    // Sin la opcion de pruebas, un endpoint local se rechaza sin trafico.
    ServidorPrueba s(Modo::Responder);
    s.cuerpo = fixture("verificacion_terminada.xml");
    SatGatewayOptions o = opciones(s.puerto());
    o.permitirEndpointsDePrueba = false;
    QVERIFY(!SatGatewayOptions{}.permitirEndpointsDePrueba);
    SatGatewayProductivo gw(o);
    ReceptorAlmacen receptor;
    const auto esPreparacion = [](const auto& r) { return !r && r.error().fase == FaseSatGateway::Preparacion; };
    QVERIFY(esPreparacion(gw.autenticar(material(), Cancelacion())));
    QVERIFY(esPreparacion(gw.crearSolicitud(token(), solicitud(TipoDescarga::Emitidos), material(), Cancelacion())));
    QVERIFY(esPreparacion(gw.verificarSolicitud(token(), {kIdSolicitud, kRfc}, material(), Cancelacion())));
    QVERIFY(esPreparacion(gw.descargarPaquete(token(), {kIdPaquete, kRfc}, material(), receptor, Cancelacion())));
    QCOMPARE(s.conexiones, 0);
}

void TestSatGatewayProductivo::prefijoQNameHostilNoSeExpone()
{
    const QByteArray fault = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><s:Fault>"
                             "<faultcode xmlns:SecretoCorto1234=\"urn:x\">SecretoCorto1234:InvalidSecurity</faultcode>"
                             "<faultstring>x</faultstring></s:Fault></s:Body></s:Envelope>";
    ServidorPrueba s(Modo::Responder);
    s.respuestas = {{500, fault}, {200, fault}};
    SatGatewayProductivo gw(opciones(s.puerto()));
    const auto a = gw.verificarSolicitud(token(), {kIdSolicitud, kRfc}, material(), Cancelacion());
    const auto b = gw.autenticar(material(), Cancelacion());
    QVERIFY(!a && !b);
    for (const ErrorSatGateway& e : {a.error(), b.error()}) {
        QCOMPARE(e.codigoSat, std::optional<QString>(QStringLiteral("InvalidSecurity")));
        QVERIFY2(!e.diagnosticoSanitizado.contains(QStringLiteral("SecretoCorto1234")), qPrintable(e.diagnosticoSanitizado));
        QVERIFY(e.diagnosticoSanitizado.contains(QStringLiteral("Fault InvalidSecurity")));
    }
    QVERIFY(a.error().tokenRechazado);
}
