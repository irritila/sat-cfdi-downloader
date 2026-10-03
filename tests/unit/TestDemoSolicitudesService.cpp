#include "TestDemoSolicitudesService.h"

#include "application/profiles/DemoPerfilesSatService.h"
#include "application/requests/DemoSolicitudesService.h"
#include "domain/common/UuidCanonico.h"
#include "domain/solicitudes/EstadoResumen.h"

#include <QSet>
#include <QSignalSpy>
#include <QTest>

using namespace satcfdi;

Q_DECLARE_METATYPE(satcfdi::NuevaSolicitudRequest)

namespace {

QList<PerfilResumen> perfiles()
{
    return DemoPerfilesSatService::perfilesDemo();
}

PerfilResumen primerActivo()
{
    for (const PerfilResumen& p : perfiles()) {
        if (p.activo) {
            return p;
        }
    }
    return {};
}

PerfilResumen primerInactivo()
{
    for (const PerfilResumen& p : perfiles()) {
        if (!p.activo) {
            return p;
        }
    }
    return {};
}

QList<SolicitudResumen> listar(SolicitudesService& s)
{
    auto r = s.listar().result();
    return r.esExito() ? r.valor() : QList<SolicitudResumen>{};
}

NuevaSolicitudRequest requestValido()
{
    NuevaSolicitudRequest r;
    r.perfilId = primerActivo().id;
    r.tipoDescarga = TipoDescarga::Recibidos;
    r.fechaInicial = QDate(2026, 9, 1);
    r.fechaFinal = QDate(2026, 9, 30);
    r.rfcContraparte = QStringLiteral("  XAXX010101000 ");
    return r;
}

} // namespace

void TestDemoSolicitudesService::listarCubreTodasLasClavesDeEstadoResumen()
{
    DemoSolicitudesService servicio(perfiles());
    QSet<QString> claves;
    for (const SolicitudResumen& s : listar(servicio)) {
        claves.insert(claveEstable(derivarEstadoResumen(s.estadoLocal, s.estadoSat)));
    }
    const QSet<QString> esperadas = {
        "Creada", "Enviando", "Enviada", "EnvioFallido", "EnvioIncierto", "Aceptada",
        "EnProceso", "Terminada", "ErrorSat", "Rechazada", "Vencida",
    };
    QCOMPARE(claves, esperadas);
}

void TestDemoSolicitudesService::listarCubreConYSinEstadoSatYPaquetes()
{
    DemoSolicitudesService servicio(perfiles());
    bool conSat = false, sinSat = false, conPaquetes = false, sinPaquetes = false;
    bool conContraparte = false, sinContraparte = false;
    QSet<EstadoDescarga> estadosPaquete;
    for (const SolicitudResumen& s : listar(servicio)) {
        (s.estadoSat ? conSat : sinSat) = true;
        (s.totalPaquetes > 0 ? conPaquetes : sinPaquetes) = true;
        (s.rfcContraparte ? conContraparte : sinContraparte) = true;
        if (s.estadoSat) {
            QCOMPARE(s.estadoLocal, EstadoLocal::Enviada);
        }
        const auto detalle = servicio.obtener(s.id).result();
        QVERIFY(detalle.esExito());
        QCOMPARE(detalle.valor().paquetes.size(), qsizetype(s.totalPaquetes));
        for (const PaqueteResumen& p : detalle.valor().paquetes) {
            estadosPaquete.insert(p.estadoDescarga);
            QCOMPARE(p.descargadoEn.has_value(), p.estadoDescarga == EstadoDescarga::Descargado);
        }
    }
    QVERIFY(conSat && sinSat && conPaquetes && sinPaquetes);
    QVERIFY(conContraparte && sinContraparte);
    QCOMPARE(estadosPaquete.size(), 5);
}

void TestDemoSolicitudesService::idsDemoSonCanonicosYUnicos()
{
    DemoSolicitudesService servicio(perfiles());
    const auto lista = listar(servicio);
    QSet<QString> ids;
    for (const SolicitudResumen& s : lista) {
        QVERIFY(uuid::esCanonico(s.id.texto()));
        ids.insert(s.id.texto());
    }
    QCOMPARE(ids.size(), lista.size());
    // Orden: mas recientes primero.
    for (qsizetype i = 1; i < lista.size(); ++i) {
        QVERIFY(lista.at(i - 1).creadaEn >= lista.at(i).creadaEn);
    }
}

void TestDemoSolicitudesService::futuresYaCompletados()
{
    DemoSolicitudesService servicio(perfiles());
    QVERIFY(servicio.listar().isFinished());
    QVERIFY(servicio.obtener(SolicitudId()).isFinished());
    QVERIFY(servicio.crear(NuevaSolicitudRequest{}).isFinished());
}

void TestDemoSolicitudesService::obtenerPorIdDevuelveDetalle()
{
    DemoSolicitudesService servicio(perfiles());
    const SolicitudResumen elegido = listar(servicio).at(3);
    const auto r = servicio.obtener(elegido.id).result();
    QVERIFY(r.esExito());
    QCOMPARE(r.valor().resumen.id, elegido.id);
    QCOMPARE(r.valor().resumen.perfilRfc, elegido.perfilRfc);
}

void TestDemoSolicitudesService::obtenerIdInexistenteDevuelveNoEncontrada()
{
    DemoSolicitudesService servicio(perfiles());
    for (const SolicitudId& id : {SolicitudId(), SolicitudId::generar()}) {
        const auto r = servicio.obtener(id).result();
        QVERIFY(!r.esExito());
        QCOMPARE(r.error().tipo, ErrorObtener::Tipo::NoEncontrada);
    }
}

void TestDemoSolicitudesService::construidoVacioNoTieneSolicitudes()
{
    DemoSolicitudesService servicio(perfiles(), DemoSolicitudesService::Datos::Vacio);
    const auto r = servicio.listar().result();
    QVERIFY(r.esExito());
    QVERIFY(r.valor().isEmpty());

    // Aun vacio permite crear con un perfil activo.
    QSignalSpy spy(&servicio, &SolicitudesService::solicitudActualizada);
    QVERIFY(servicio.crear(requestValido()).result().esExito());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(listar(servicio).size(), 1);
}

void TestDemoSolicitudesService::crearValidaAgregaAlInicioYEmiteSenal()
{
    DemoSolicitudesService servicio(perfiles());
    const qsizetype antes = listar(servicio).size();

    SolicitudId notificado;
    bool visibleAlNotificar = false;
    QSignalSpy spy(&servicio, &SolicitudesService::solicitudActualizada);
    connect(&servicio, &SolicitudesService::solicitudActualizada, this,
            [&](const SolicitudId& id) {
                notificado = id;
                visibleAlNotificar = servicio.obtener(id).result().esExito();
            });

    const auto r = servicio.crear(requestValido()).result();
    QVERIFY(r.esExito());
    const SolicitudId id = r.valor();
    QVERIFY(uuid::esCanonico(id.texto()));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(notificado, id);
    QVERIFY(visibleAlNotificar);

    const auto lista = listar(servicio);
    QCOMPARE(lista.size(), antes + 1);
    const SolicitudResumen& nueva = lista.constFirst();
    QCOMPARE(nueva.id, id);
    QCOMPARE(nueva.perfilRfc, primerActivo().rfc);
    QCOMPARE(nueva.rfcContraparte, std::optional<QString>(QStringLiteral("XAXX010101000")));
    QCOMPARE(nueva.tipoDescarga, TipoDescarga::Recibidos);
    QCOMPARE(nueva.fechaInicial, QDate(2026, 9, 1));
    QCOMPARE(nueva.fechaFinal, QDate(2026, 9, 30));
    QCOMPARE(nueva.estadoLocal, EstadoLocal::Creada);
    QVERIFY(!nueva.estadoSat.has_value());
    QCOMPARE(nueva.totalPaquetes, 0);
    QVERIFY(nueva.creadaEn.isValid());

    const auto detalle = servicio.obtener(id).result();
    QVERIFY(detalle.esExito());
    QVERIFY(detalle.valor().paquetes.isEmpty());
}

void TestDemoSolicitudesService::crearRechazaValidacion_data()
{
    QTest::addColumn<NuevaSolicitudRequest>("request");
    QTest::addColumn<int>("codigo");

    using C = ErrorValidacion::Codigo;
    NuevaSolicitudRequest r = requestValido();
    r.perfilId = PerfilId();
    QTest::newRow("sin perfil") << r << int(C::PerfilRequerido);

    r = requestValido();
    r.perfilId = PerfilId::generar();
    QTest::newRow("perfil desconocido") << r << int(C::PerfilInexistente);

    r = requestValido();
    r.perfilId = primerInactivo().id;
    QTest::newRow("perfil inactivo") << r << int(C::PerfilInexistente);

    r = requestValido();
    r.fechaInicial = QDate();
    QTest::newRow("sin fecha inicial") << r << int(C::FechaInicialRequerida);

    r = requestValido();
    r.fechaFinal = QDate();
    QTest::newRow("sin fecha final") << r << int(C::FechaFinalRequerida);

    r = requestValido();
    r.fechaInicial = QDate(2026, 9, 2);
    r.fechaFinal = QDate(2026, 9, 1);
    QTest::newRow("rango invertido") << r << int(C::RangoFechasInvalido);
}

void TestDemoSolicitudesService::crearRechazaValidacion()
{
    QFETCH(NuevaSolicitudRequest, request);
    QFETCH(int, codigo);

    DemoSolicitudesService servicio(perfiles());
    const qsizetype antes = listar(servicio).size();
    QSignalSpy spy(&servicio, &SolicitudesService::solicitudActualizada);

    const auto r = servicio.crear(request).result();
    QVERIFY(!r.esExito());
    QCOMPARE(r.error().tipo, ErrorCrear::Tipo::Validacion);
    QCOMPARE(r.error().validaciones.size(), 1);
    QCOMPARE(int(r.error().validaciones.constFirst().codigo), codigo);
    QVERIFY(!r.error().mensaje.isEmpty());
    QCOMPARE(spy.count(), 0);
    QCOMPARE(listar(servicio).size(), antes);
}

void TestDemoSolicitudesService::crearAceptaFechasIguales()
{
    DemoSolicitudesService servicio(perfiles());
    NuevaSolicitudRequest r = requestValido();
    r.fechaFinal = r.fechaInicial;
    r.rfcContraparte = QStringLiteral("   ");
    const auto resultado = servicio.crear(r).result();
    QVERIFY(resultado.esExito());
    const auto detalle = servicio.obtener(resultado.valor()).result();
    QVERIFY(!detalle.valor().resumen.rfcContraparte.has_value());
}
