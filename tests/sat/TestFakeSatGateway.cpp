#include "TestFakeSatGateway.h"

#include "SatPruebasComun.h"

#include "fakes/FakeSatGateway.h"
#include "infrastructure/sat/RespuestasSat.h"
#include "infrastructure/sat/SaneadoRespuestaSat.h"

#include <QTest>
#include <QThread>

#include <atomic>
#include <memory>

using namespace satcfdi;
using fakes::FakeSatGateway;
using Op = FakeSatGateway::Op;
using Paso = FakeSatGateway::Paso;

namespace {

QByteArray fixture(const char* nombre)
{
    return satpruebas::leer(QStringLiteral(SATCFDI_SAT_FIXTURES "/") + QString::fromLatin1(nombre));
}

MaterialFirma materialVacio()
{
    return MaterialFirma(BufferSecreto(), BufferSecreto(), BufferSecreto());
}

// Receptor que guarda via FakePackageStorage (como hara OperacionesSat).
struct ReceptorAlmacen final : ReceptorPaqueteSat {
    fakes::FakePackageStorage& storage;
    UbicacionPaquete ubicacion;
    Cancelacion cancelacion;
    std::optional<Resultado<ArchivoFinal, ErrorGuardarZip>> resultado;
    ReceptorAlmacen(fakes::FakePackageStorage& s, UbicacionPaquete u, Cancelacion c = {})
        : storage(s), ubicacion(std::move(u)), cancelacion(std::move(c)) {}
    void recibir(FuenteZipPorChunks& paquete) override
    {
        resultado.emplace(storage.guardarAtomico(ubicacion, paquete, cancelacion));
    }
};

} // namespace

void TestFakeSatGateway::fixturesCoincidenConElParser()
{
    const auto solicitud = sat::parsearSolicitud(fixture("solicitud_emitidos_ok.xml"));
    QVERIFY(solicitud);
    QCOMPARE(solicitud.valor().codEstatus, FakeSatGateway::creacionAceptada().codEstatus);
    QCOMPARE(solicitud.valor().mensaje, FakeSatGateway::creacionAceptada().mensaje);
    QCOMPARE(solicitud.valor().idSolicitud, *FakeSatGateway::creacionAceptada().idSolicitud);
    const auto rechazo = sat::parsearSolicitud(fixture("solicitud_recibidos_rechazo.xml"));
    QVERIFY(rechazo);
    QCOMPARE(rechazo.valor().codEstatus, FakeSatGateway::rechazo301().codEstatus);
    QVERIFY(rechazo.valor().idSolicitud.isEmpty() && !FakeSatGateway::rechazo301().idSolicitud);
    const auto verificacion = sat::parsearVerificacion(fixture("verificacion_terminada.xml"));
    QVERIFY(verificacion);
    const RespuestaVerificacion esperado = FakeSatGateway::verificacionTerminada();
    QCOMPARE(verificacion.valor().estadoSolicitud, esperado.estadoSolicitud);
    QCOMPARE(verificacion.valor().codigoEstadoSolicitud, *esperado.codigoEstadoSolicitud);
    QCOMPARE(qint64(*verificacion.valor().numeroCfdis), *esperado.numeroCfdi);
    QCOMPARE(verificacion.valor().idsPaquetes, esperado.idsPaquete);
    const auto descarga = sat::parsearDescarga(fixture("descarga_ok.xml"));
    QVERIFY(descarga);
    QCOMPARE(descarga.valor().paquete, FakeSatGateway::paqueteOk());
    QCOMPARE(descarga.valor().paquete.size(), 64);
    const auto vencido = sat::parsearDescarga(fixture("descarga_vencido.xml"));
    QVERIFY(vencido);
    QCOMPARE(vencido.valor().codEstatus, FakeSatGateway::descargaVencida5007().codEstatus);
    const auto fault = sat::parsearFault(fixture("fault_sintetico.xml"));
    QVERIFY(fault);
    // El fake reproduce la salida SANEADA del gateway (sin prefijo QName).
    QCOMPARE(sat::saneado::faultcodePermitido(fault->codigo), *FakeSatGateway::faultSintetico().codigoSat);
}

void TestFakeSatGateway::guionTokenYConteo()
{
    FakeSatGateway sat;
    QDateTime reloj = QDateTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone::UTC);
    sat.ahora = [&] { return reloj; };
    const MaterialFirma m = materialVacio();
    auto token = sat.autenticar(m, Cancelacion());
    QVERIFY(token);
    QCOMPARE(token.valor().expira(), reloj.addSecs(300));
    QCOMPARE(sat.autenticaciones(), 1);

    SolicitudSat s;
    s.tipo = TipoDescarga::Recibidos;
    s.rfcSolicitante = QStringLiteral("AAA010101AAA");
    sat.programar(Op::Crear, Paso::conCreacion(FakeSatGateway::rechazo301()));
    QCOMPARE(sat.crearSolicitud(token.valor(), s, m, Cancelacion()).valor().codEstatus, QStringLiteral("301"));
    QCOMPARE(sat.crearSolicitud(token.valor(), s, m, Cancelacion()).valor().codEstatus, QStringLiteral("5000"));
    QCOMPARE(sat.solicitudes().size(), 2);
    QCOMPARE(sat.solicitudes().first().tipo, TipoDescarga::Recibidos);

    sat.programar(Op::Verificar, Paso::conError(FakeSatGateway::faultSintetico()));
    const auto f = sat.verificarSolicitud(token.valor(), {QStringLiteral("x"), s.rfcSolicitante}, m, Cancelacion());
    QVERIFY(!f);
    QVERIFY(f.error().tokenRechazado);

    // Token vencido: rechazo inferido (HTTP 401).
    reloj = reloj.addSecs(301);
    const auto vencido = sat.verificarSolicitud(token.valor(), {QStringLiteral("x"), s.rfcSolicitante}, m, Cancelacion());
    QVERIFY(!vencido);
    QCOMPARE(vencido.error().estadoHttp, std::optional<int>(401));
    QVERIFY(vencido.error().tokenRechazado);
    QCOMPARE(sat.maximoActivas(), 1);
}

void TestFakeSatGateway::bloqueoCancelableYLiberable()
{
    FakeSatGateway sat;
    const MaterialFirma m = materialVacio();
    auto token = sat.autenticar(m, Cancelacion()).valor().expira(); // solo para ejercitar
    Q_UNUSED(token);
    sat.programar(Op::Autenticar, Paso::bloqueado(FaseSatGateway::AntesDeEnvio));
    std::atomic<bool> cancelar{false};
    std::optional<Resultado<TokenSat, ErrorSatGateway>> r;
    std::unique_ptr<QThread> hilo(QThread::create([&] {
        r.emplace(sat.autenticar(m, Cancelacion([&] { return cancelar.load(); })));
    }));
    hilo->start();
    QVERIFY(sat.esperarBloqueo());
    cancelar = true;
    QVERIFY(hilo->wait(5000));
    QVERIFY(r && !*r);
    QVERIFY(r->error().cancelada);
    QCOMPARE(r->error().fase, FaseSatGateway::AntesDeEnvio);

    sat.programar(Op::Autenticar, Paso::bloqueado());
    std::optional<Resultado<TokenSat, ErrorSatGateway>> r2;
    std::unique_ptr<QThread> hilo2(QThread::create([&] { r2.emplace(sat.autenticar(m, Cancelacion())); }));
    hilo2->start();
    QVERIFY(sat.esperarBloqueo());
    sat.liberar();
    QVERIFY(hilo2->wait(5000));
    QVERIFY(r2 && *r2);
}

void TestFakeSatGateway::descargaEntregaAlReceptor()
{
    FakeSatGateway sat;
    fakes::FakePackageStorage storage;
    const MaterialFirma m = materialVacio();
    auto token = sat.autenticar(m, Cancelacion());
    const UbicacionPaquete u{QStringLiteral("AAA010101AAA"), QStringLiteral("2026-01-01T00:00:00"),
                             QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e"), QStringLiteral("P_01")};
    ReceptorAlmacen receptor(storage, u);
    const auto r = sat.descargarPaquete(token.valor(), {QStringLiteral("P_01"), u.rfcSolicitante}, m, receptor,
                                        Cancelacion());
    QVERIFY(r && r.valor().paqueteEntregado);
    QCOMPARE(r.valor().bytesPaquete, 64);
    QVERIFY(receptor.resultado && *receptor.resultado);
    QCOMPARE(storage.bytesFinal(receptor.resultado->valor().rutaRelativa), std::optional<QByteArray>(FakeSatGateway::paqueteOk()));

    // 5007: el receptor no se invoca.
    sat.programar(Op::Descargar, Paso::conDescarga(FakeSatGateway::descargaVencida5007()));
    ReceptorAlmacen otro(storage, u);
    const auto v = sat.descargarPaquete(token.valor(), {QStringLiteral("P_02"), u.rfcSolicitante}, m, otro,
                                        Cancelacion());
    QVERIFY(v && !v.valor().paqueteEntregado);
    QVERIFY(!otro.resultado);
}
