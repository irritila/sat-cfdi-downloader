#include "TestClienteHttpSat.h"

#include "SatPruebasComun.h"

#include "infrastructure/sat/ClienteHttpSat.h"
#include "infrastructure/sat/RespuestasSat.h"

#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace satcfdi;
using namespace satcfdi::sat;

namespace {

QByteArray fixture(const char* nombre)
{
    return satpruebas::leer(QStringLiteral(SATCFDI_SAT_FIXTURES "/") + QString::fromLatin1(nombre));
}

// Servidor HTTP/1.1 minimo para pruebas. Cuenta peticiones COMPLETAS
// (encabezados + Content-Length) y actua segun el modo.
class ServidorPrueba : public QObject {
public:
    enum class Modo {
        Responder,          // responde `estado` + `cuerpo` y cierra
        CerrarTrasPeticion, // lee la peticion completa y corta sin responder
        Silencio,           // lee la peticion y nunca responde
        SinLeer,            // acepta TCP y no lee nada (TLS nunca avanza)
        Manual,             // guarda la conexion; la prueba llama responderPendiente()
    };

    explicit ServidorPrueba(Modo modo) : m_modo(modo)
    {
        QObject::connect(&m_servidor, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = m_servidor.nextPendingConnection()) {
                s->setParent(this);
                ++conexiones;
                if (m_modo == Modo::SinLeer) {
                    continue;
                }
                QObject::connect(s, &QTcpSocket::readyRead, this, [this, s]() { leer(s); });
            }
        });
        m_servidor.listen(QHostAddress::LocalHost);
    }

    quint16 puerto() const { return m_servidor.serverPort(); }
    void cerrar() { m_servidor.close(); }

    int conexiones = 0;
    int peticiones = 0;
    QByteArray ultimaCabecera;
    QByteArray ultimoCuerpo;
    QStringList bitacora;
    int estado = 200;
    QByteArray cuerpo;

    int pendientes() const { return static_cast<int>(m_pendientes.size()); }
    void responderPendiente()
    {
        QTcpSocket* s = m_pendientes.takeFirst();
        bitacora.append(QStringLiteral("respondida"));
        responder(s);
    }

private:
    void leer(QTcpSocket* s)
    {
        QByteArray& buf = m_buffers[s];
        buf += s->readAll();
        const qsizetype finCabecera = buf.indexOf("\r\n\r\n");
        if (finCabecera < 0) {
            return;
        }
        const QByteArray cabecera = buf.left(finCabecera);
        qsizetype largo = 0;
        for (const QByteArray& linea : cabecera.split('\n')) {
            if (linea.toLower().startsWith("content-length:")) {
                largo = linea.mid(15).trimmed().toLongLong();
            }
        }
        if (buf.size() < finCabecera + 4 + largo) {
            return;
        }
        ++peticiones;
        ultimaCabecera = cabecera;
        ultimoCuerpo = buf.mid(finCabecera + 4, largo);
        bitacora.append(QStringLiteral("recibida:") + QString::fromUtf8(ultimoCuerpo));
        buf.clear();
        switch (m_modo) {
        case Modo::Responder: responder(s); break;
        case Modo::CerrarTrasPeticion: s->abort(); break;
        case Modo::Manual: m_pendientes.append(s); break;
        case Modo::Silencio:
        case Modo::SinLeer: break;
        }
    }

    void responder(QTcpSocket* s)
    {
        QByteArray r = "HTTP/1.1 " + QByteArray::number(estado) + (estado == 200 ? " OK" : " Internal Server Error")
                       + "\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: "
                       + QByteArray::number(cuerpo.size()) + "\r\nConnection: close\r\n\r\n" + cuerpo;
        s->write(r);
        s->disconnectFromHost();
    }

    Modo m_modo;
    QTcpServer m_servidor;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QList<QTcpSocket*> m_pendientes;
};

PeticionSat peticion(quint16 puerto, const QByteArray& sobre = "<s:Envelope/>", bool https = false)
{
    PeticionSat p;
    p.url = QUrl(QStringLiteral("%1://127.0.0.1:%2/Servicio.svc").arg(https ? u"https" : u"http").arg(puerto));
    p.soapAction = descriptor(Operacion::VerificaSolicitudDescarga).soapAction;
    p.sobre = sobre;
    return p;
}

std::optional<ResultadoHttp> esperar(QFuture<ResultadoHttp> f, int ms = 15000)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, ms) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

} // namespace

void TestClienteHttpSat::antesDeEnvioConexionRechazada()
{
    quint16 puerto = 0;
    {
        ServidorPrueba s(ServidorPrueba::Modo::Responder);
        puerto = s.puerto();
        s.cerrar();
    }
    ClienteHttpSat cliente;
    const auto r = esperar(cliente.enviar(peticion(puerto)));
    QVERIFY(r);
    QCOMPARE(r->fase, FaseResultado::AntesDeEnvio);
    QVERIFY(!r->deadlineVencido);
    QVERIFY(!r->estadoHttp);
    QVERIFY(r->cuerpo.isEmpty());
    QCOMPARE(r->diagnostico, QStringLiteral("ConnectionRefusedError"));
}

void TestClienteHttpSat::antesDeEnvioDeadlineSinRequestSent()
{
    // HTTPS contra un TCP que acepta y nunca contesta: el handshake TLS no
    // termina, requestSent no ocurre y vence el deadline.
    // El handshake no puede completarse nunca (el servidor no lee), asi que el
    // resultado no depende del valor del deadline ni de la carga: solo cuanto
    // tarda. Se exige ademas que el servidor haya aceptado la conexion.
    ServidorPrueba s(ServidorPrueba::Modo::SinLeer);
    ClienteHttpSat cliente;
    cliente.establecerDeadline(std::chrono::milliseconds(1500));
    auto f = cliente.enviar(peticion(s.puerto(), "<s:Envelope/>", true));
    QVERIFY(QTest::qWaitFor([&] { return s.conexiones >= 1; }, 10000));
    const auto r = esperar(f, 30000);
    QVERIFY(r);
    QCOMPARE(r->fase, FaseResultado::AntesDeEnvio);
    QVERIFY(r->deadlineVencido);
    QCOMPARE(s.peticiones, 0);
}

void TestClienteHttpSat::despuesDeEnvioCorteSinReenvio()
{
    ServidorPrueba s(ServidorPrueba::Modo::CerrarTrasPeticion);
    ClienteHttpSat cliente;
    const auto r = esperar(cliente.enviar(peticion(s.puerto(), "<s:Envelope>una</s:Envelope>")));
    QVERIFY(r);
    QCOMPARE(r->fase, FaseResultado::DespuesDeEnvio);
    QVERIFY(!r->deadlineVencido);
    QVERIFY(!r->estadoHttp);
    // Ni el cliente ni Qt reenviaron la peticion tras el corte.
    QCOMPARE(s.peticiones, 1);
}

void TestClienteHttpSat::despuesDeEnvioDeadline()
{
    // Margen holgado (3 s) para que la peticion llegue antes del deadline aun
    // bajo carga; la condicion observable (peticion completa en el servidor)
    // se verifica ANTES de que venza el deadline.
    ServidorPrueba s(ServidorPrueba::Modo::Silencio);
    ClienteHttpSat cliente;
    cliente.establecerDeadline(std::chrono::milliseconds(3000));
    auto f = cliente.enviar(peticion(s.puerto()));
    QVERIFY(QTest::qWaitFor([&] { return s.peticiones == 1; }, 2500));
    QVERIFY(!f.isFinished());
    const auto r = esperar(f, 30000);
    QVERIFY(r);
    QCOMPARE(r->fase, FaseResultado::DespuesDeEnvio);
    QVERIFY(r->deadlineVencido);
    QCOMPARE(s.peticiones, 1);
}

void TestClienteHttpSat::respuestaExplicitaConResultadoYHeaders()
{
    ServidorPrueba s(ServidorPrueba::Modo::Responder);
    s.cuerpo = fixture("verificacion_terminada.xml");
    const TokenSat token(u"tok.en-ficticio", QDateTime(), QDateTime());
    ClienteHttpSat cliente;
    const QByteArray sobre = "<s:Envelope>verifica</s:Envelope>";
    const auto r = esperar(cliente.enviar(peticion(s.puerto(), sobre), &token));
    QVERIFY(r);
    QCOMPARE(r->fase, FaseResultado::RespuestaExplicita);
    QCOMPARE(r->estadoHttp, std::optional<int>(200));
    auto v = parsearVerificacion(r->cuerpo);
    QVERIFY(v);
    QCOMPARE(v.valor().idsPaquetes.size(), 2);
    // Encabezados enviados.
    const QByteArray cab = s.ultimaCabecera;
    QVERIFY(cab.startsWith("POST /Servicio.svc HTTP/1.1"));
    QVERIFY(cab.contains("Authorization: WRAP access_token=\"tok.en-ficticio\""));
    // Qt puede normalizar el nombre del header a minusculas (HTTP no distingue).
    const QByteArray accion = "\"http://DescargaMasivaTerceros.sat.gob.mx/IVerificaSolicitudDescargaService/"
                              "VerificaSolicitudDescarga\"";
    QVERIFY2(cab.toLower().contains("soapaction: " + accion.toLower()), cab.constData());
    QVERIFY(cab.contains(accion)); // valor exacto, con comillas
    QVERIFY(cab.toLower().contains("content-type: text/xml; charset=utf-8"));
    QCOMPARE(s.ultimoCuerpo, sobre);

    // Sin token (Autentica): no hay header Authorization.
    const auto sinToken = esperar(cliente.enviar(peticion(s.puerto())));
    QVERIFY(sinToken);
    QVERIFY(!s.ultimaCabecera.contains("Authorization"));
}

void TestClienteHttpSat::respuestaExplicitaConFault500()
{
    ServidorPrueba s(ServidorPrueba::Modo::Responder);
    s.estado = 500;
    s.cuerpo = fixture("fault_sintetico.xml");
    ClienteHttpSat cliente;
    const auto r = esperar(cliente.enviar(peticion(s.puerto())));
    QVERIFY(r);
    QCOMPARE(r->fase, FaseResultado::RespuestaExplicita);
    QCOMPARE(r->estadoHttp, std::optional<int>(500));
    const auto fault = parsearFault(r->cuerpo);
    QVERIFY(fault);
    QCOMPARE(fault->codigo, QStringLiteral("a:InvalidSecurity"));
}

void TestClienteHttpSat::peticionesSeriales()
{
    ServidorPrueba s(ServidorPrueba::Modo::Manual);
    s.cuerpo = fixture("solicitud_emitidos_ok.xml");
    ClienteHttpSat cliente;
    auto a = cliente.enviar(peticion(s.puerto(), "A"));
    auto b = cliente.enviar(peticion(s.puerto(), "B"));
    QCOMPARE(cliente.pendientes(), 2);
    QVERIFY(QTest::qWaitFor([&] { return s.pendientes() == 1; }, 5000));
    s.responderPendiente();
    QVERIFY(esperar(a));
    QVERIFY(QTest::qWaitFor([&] { return s.pendientes() == 1; }, 5000));
    s.responderPendiente();
    QVERIFY(esperar(b));
    // B solo llego despues de responder A: operaciones seriales.
    QCOMPARE(s.bitacora, (QStringList{QStringLiteral("recibida:A"), QStringLiteral("respondida"),
                                      QStringLiteral("recibida:B"), QStringLiteral("respondida")}));
    QCOMPARE(cliente.pendientes(), 0);
}
