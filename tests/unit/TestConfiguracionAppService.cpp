#include "TestConfiguracionAppService.h"

#include "FakesPersistencia.h"
#include "fakes/FakeConfiguracionAppService.h"

#include "application/configuration/ConfiguracionAppServicePersistido.h"
#include "application/persistence/PersistenceDispatcher.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace satcfdi;
using namespace Qt::StringLiterals;
using fakes::Almacen;
using R = ConfiguracionAppService::ResultadoConfiguracion;

namespace {

const QDateTime kAhora = QDateTime(QDate(2026, 10, 3), QTime(12, 0, 0, 250), QTimeZone(QTimeZone::UTC));
const QDateTime kAntes = QDateTime(QDate(2026, 1, 1), QTime(0, 0, 0), QTimeZone(QTimeZone::UTC));

// Orden de miembros = construccion: fakes, dispatcher, servicio.
struct Entorno {
    Almacen almacen;
    fakes::FakeConfiguracion configuracion{almacen};
    fakes::FakeUnitOfWork uow{almacen};
    PersistenceDispatcher dispatcher;
    ConfiguracionAppServicePersistido servicio{dispatcher, configuracion, uow, [] { return kAhora; }};

    Entorno() { almacen.configuracion->actualizadaEn = kAntes; }
    ~Entorno() { almacen.liberar(); } // nunca deja el dispatcher en una barrera
};

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 30000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

enum class Op { Inicio, Pausa, Cierre };

QFuture<R> invocar(ConfiguracionAppService& s, Op op)
{
    switch (op) {
    case Op::Inicio:
        return s.actualizarInicioAutomatico(true);
    case Op::Pausa:
        return s.actualizarMonitoreoPausado(true);
    case Op::Cierre:
        return s.registrarUltimoCierre();
    }
    return {};
}

const char* nombreOp(Op op)
{
    switch (op) {
    case Op::Inicio:
        return "actualizarInicioAutomatico";
    case Op::Pausa:
        return "actualizarMonitoreoPausado";
    case Op::Cierre:
        return "registrarUltimoCierre";
    }
    return "";
}

} // namespace

Q_DECLARE_METATYPE(Op)

void TestConfiguracionAppService::obtenerNoAbreTransaccionNiEmite()
{
    Entorno e;
    QSignalSpy cambio(&e.servicio, &ConfiguracionAppService::configuracionCambiada);
    const auto r = esperar(e.servicio.obtener());
    QVERIFY(r && r->esExito());
    QVERIFY(!r->valor().inicioAutomaticoHabilitado);
    QVERIFY(!r->valor().monitoreoPausado);
    QVERIFY(!r->valor().ultimoCierreEn);
    QCOMPARE(e.almacen.eventos, QStringList{u"obtenerConfiguracion"_s});
    QCOMPARE(cambio.count(), 0);

    e.almacen.configuracion.reset();
    const auto sinFila = esperar(e.servicio.obtener());
    QVERIFY(sinFila && !sinFila->esExito());
    QCOMPARE(sinFila->error().tipo, ErrorPersistencia::Tipo::NoEncontrado);
}

void TestConfiguracionAppService::escriturasConfirmanYEmitenAntesDeCompletar_data()
{
    QTest::addColumn<Op>("op");
    QTest::newRow("inicio-automatico") << Op::Inicio;
    QTest::newRow("monitoreo-pausado") << Op::Pausa;
    QTest::newRow("ultimo-cierre") << Op::Cierre;
}

void TestConfiguracionAppService::escriturasConfirmanYEmitenAntesDeCompletar()
{
    QFETCH(Op, op);
    Entorno e;
    // Barrera: la tarea no termina antes de que el servicio encadene su
    // .then(); si no, la senal podria emitirse antes del connect de abajo.
    e.almacen.bloquearEn(u"commit"_s);
    QFuture<R> f = invocar(e.servicio, op);

    QThread* grafico = QThread::currentThread();
    int senales = 0;
    bool futureTerminadoEnSenal = true;
    bool enHiloGrafico = false;
    bool commitAntesDeSenal = false;
    ConfiguracionApp emitida;
    connect(&e.servicio, &ConfiguracionAppService::configuracionCambiada, this,
            [&](const ConfiguracionApp& c) {
                ++senales;
                emitida = c;
                futureTerminadoEnSenal = f.isFinished();
                enHiloGrafico = QThread::currentThread() == grafico;
                commitAntesDeSenal = e.almacen.eventos.contains(u"commit"_s);
            });
    QVERIFY(!f.isFinished());
    e.almacen.soltar();

    const auto r = esperar(f);
    QVERIFY(r && r->esExito());
    QCOMPARE(senales, 1);
    QVERIFY(!futureTerminadoEnSenal);
    QVERIFY(enHiloGrafico);
    QVERIFY(commitAntesDeSenal);
    QCOMPARE(e.almacen.eventos,
             (QStringList{u"begin"_s, QString::fromLatin1(nombreOp(op)), u"commit"_s}));

    const ConfiguracionApp& c = r->valor();
    QCOMPARE(c.inicioAutomaticoHabilitado, op == Op::Inicio);
    QCOMPARE(c.monitoreoPausado, op == Op::Pausa);
    QCOMPARE(c.ultimoCierreEn.has_value(), op == Op::Cierre);
    if (op == Op::Cierre) {
        QCOMPARE(*c.ultimoCierreEn, kAhora);
    }
    QCOMPARE(c.actualizadaEn, kAhora); // reloj inyectado
    QCOMPARE(emitida.actualizadaEn, kAhora);
    QCOMPARE(emitida.inicioAutomaticoHabilitado, c.inicioAutomaticoHabilitado);
    QCOMPARE(emitida.monitoreoPausado, c.monitoreoPausado);
}

void TestConfiguracionAppService::falloEnEscrituraHaceRollbackSinSenal_data()
{
    QTest::addColumn<Op>("op");
    QTest::addColumn<QString>("fallo");
    QTest::addColumn<ErrorPersistencia::Tipo>("tipo");
    QTest::addColumn<bool>("rollback");
    QTest::newRow("begin-ocupado") << Op::Pausa << u"begin"_s << ErrorPersistencia::Tipo::Ocupado << false;
    QTest::newRow("update-almacenamiento")
        << Op::Inicio << u"actualizarInicioAutomatico"_s << ErrorPersistencia::Tipo::Almacenamiento << true;
    QTest::newRow("cierre-sin-fila")
        << Op::Cierre << u"registrarUltimoCierre"_s << ErrorPersistencia::Tipo::NoEncontrado << true;
    QTest::newRow("commit-ocupado") << Op::Pausa << u"commit"_s << ErrorPersistencia::Tipo::Ocupado << true;
}

void TestConfiguracionAppService::falloEnEscrituraHaceRollbackSinSenal()
{
    QFETCH(Op, op);
    QFETCH(QString, fallo);
    QFETCH(ErrorPersistencia::Tipo, tipo);
    QFETCH(bool, rollback);
    Entorno e;
    e.almacen.fallos.insert(fallo, fakes::error(tipo));
    QSignalSpy cambio(&e.servicio, &ConfiguracionAppService::configuracionCambiada);

    const auto r = esperar(invocar(e.servicio, op));
    QVERIFY(r && !r->esExito());
    QCOMPARE(r->error().tipo, tipo);
    QCOMPARE(cambio.count(), 0);
    QCOMPARE(e.almacen.eventos.contains(u"rollback"_s), rollback);
    // Sin cambios confirmados.
    QVERIFY(!e.almacen.configuracion->inicioAutomaticoHabilitado);
    QVERIFY(!e.almacen.configuracion->monitoreoPausado);
    QVERIFY(!e.almacen.configuracion->ultimoCierreEn);
    QCOMPARE(e.almacen.configuracion->actualizadaEn, kAntes);
}

void TestConfiguracionAppService::escriturasSucesivasNoSeSobrescriben()
{
    Entorno e;
    QSignalSpy cambio(&e.servicio, &ConfiguracionAppService::configuracionCambiada);
    // Despachadas sin esperar: el dispatcher las serializa en orden.
    auto f1 = e.servicio.actualizarInicioAutomatico(true);
    auto f2 = e.servicio.actualizarMonitoreoPausado(true);
    auto f3 = e.servicio.actualizarMonitoreoPausado(false);
    const auto r3 = esperar(f3);
    QVERIFY(r3 && r3->esExito());
    QVERIFY(f1.isFinished() && f2.isFinished());
    QVERIFY(r3->valor().inicioAutomaticoHabilitado);
    QVERIFY(!r3->valor().monitoreoPausado);
    QCOMPARE(cambio.count(), 3);
    QVERIFY(cambio.at(1).at(0).value<ConfiguracionApp>().monitoreoPausado);
    QVERIFY(cambio.at(1).at(0).value<ConfiguracionApp>().inicioAutomaticoHabilitado);
}

void TestConfiguracionAppService::fakeServicioControlaFutures()
{
    fakes::FakeConfiguracionAppService s;
    QSignalSpy cambio(&s, &ConfiguracionAppService::configuracionCambiada);

    auto pausa = s.actualizarMonitoreoPausado(true);
    auto lectura = s.obtener();
    auto inicio = s.actualizarInicioAutomatico(true);
    QCOMPARE(s.llamadas, (QStringList{u"actualizarMonitoreoPausado(true)"_s, u"obtener"_s,
                                      u"actualizarInicioAutomatico(true)"_s}));
    QCOMPARE(s.pendientes(), 3);
    QVERIFY(!pausa.isFinished());

    bool terminadoEnSenal = true;
    connect(&s, &ConfiguracionAppService::configuracionCambiada, this,
            [&] { terminadoEnSenal = pausa.isFinished(); }, Qt::SingleShotConnection);
    QVERIFY(s.confirmarSiguiente());
    QVERIFY(!terminadoEnSenal);
    QVERIFY(pausa.isFinished());
    QVERIFY(pausa.result().valor().monitoreoPausado);
    QCOMPARE(cambio.count(), 1);

    QVERIFY(s.confirmarSiguiente()); // lectura: sin senal
    QCOMPARE(cambio.count(), 1);
    QVERIFY(lectura.result().valor().monitoreoPausado);

    QVERIFY(s.fallarSiguiente(fakes::error(ErrorPersistencia::Tipo::Ocupado)));
    QCOMPARE(cambio.count(), 1);
    QVERIFY(!inicio.result().esExito());
    QVERIFY(!s.confirmada.inicioAutomaticoHabilitado);
    QVERIFY(!s.confirmarSiguiente());

    s.autoConfirmar = true;
    auto cierre = s.registrarUltimoCierre();
    QVERIFY(cierre.isFinished());
    QCOMPARE(cierre.result().valor().ultimoCierreEn, std::optional<QDateTime>(s.reloj()));
    QCOMPARE(cambio.count(), 2);
}
