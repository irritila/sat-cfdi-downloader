// T007 (plataforma): root real con SQLite temporal, OperacionExecutor y
// WorkerLocal reales, FakeOperacionesSat, FakeReloj/FakeProgramador (sin
// tiempo real) y FakeOSIntegration. Cubre: estado del worker en el menu bar,
// salida con operacion bloqueada (9.999 s / 10 s), un log por operacion
// interrumpida, adaptador nulo (falla Preparacion visible) y responsividad del
// hilo grafico con el puerto bloqueado.

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"
#include "app_core/AppLifecycleController.h"
#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include "application/operaciones/OperacionExecutor.h"
#include "application/operaciones/OperacionesSatNulo.h"
#include "application/operaciones/WorkerLocal.h"
#include "application/profiles/PerfilesSatService.h"
#include "application/requests/SolicitudesService.h"

#include "fakes/FakeOSIntegration.h"
#include "fakes/FakeOperacionesSat.h"
#include "fakes/FakeProgramador.h"
#include "fakes/FakeSecretStore.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTimeZone>

#include <memory>
#include <optional>

using namespace satcfdi;
using Fase = OSIntegration::EstadoMonitoreo::Fase;
using Actividad = OSIntegration::EstadoMonitoreo::Actividad;
using Op = fakes::FakeOperacionesSat::Op;

namespace {

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

// Grafo completo con dependencias falsas de monitoreo.
struct Grafo {
    explicit Grafo(bool adaptadorNulo = false)
    {
        AppBootstrapper::Opciones opciones;
        opciones.directorioDatos = tmp.path();
        const auto arranque = AppBootstrapper(opciones).preparar();
        ok = arranque.esExito();
        if (!ok) {
            return;
        }
        rutaBase = arranque.valor().rutaBase;
        abrir(adaptadorNulo);
    }

    void abrir(bool adaptadorNulo = false)
    {
        OpcionesMonitoreo m;
        // Nunca el adaptador productivo (red real): fake o el nulo explicito.
        m.operacionesSat = adaptadorNulo ? static_cast<OperacionesSat*>(&nulo) : &sat;
        m.reloj = reloj.funcion();
        m.programadorEjecutor = &programadorEjecutor;
        m.programadorWorker = &programadorWorker;
        root = std::make_unique<AppCompositionRoot>(rutaBase, secretos, m);
    }

    // Perfil + solicitud Creada; devuelve su id.
    std::optional<SolicitudId> crearSolicitud()
    {
        const auto perfil = esperar(root->perfiles().crear(QStringLiteral("EKU9003173C9"), QStringLiteral("Perfil")));
        if (!perfil || !perfil->esExito()) {
            return std::nullopt;
        }
        NuevaSolicitudRequest r;
        r.perfilId = perfil->valor().id;
        r.tipoDescarga = TipoDescarga::Recibidos;
        r.fechaInicial = QDate(2026, 9, 1);
        r.fechaFinal = QDate(2026, 9, 1);
        const auto creada = esperar(root->solicitudes().crear(r));
        if (!creada || !creada->esExito()) {
            return std::nullopt;
        }
        return creada->valor();
    }

    // Ciclo de vida real con la ventana QML y el menu bar falso.
    bool iniciarApp()
    {
        if (!root->cargar()) {
            return false;
        }
        root->iniciarCicloDeVida(os, nullptr, [this] { ++salidas; });
        return true;
    }

    std::optional<OSIntegration::EstadoMonitoreo> ultimoEstado() const
    {
        return os.estadosMonitoreo.isEmpty() ? std::nullopt : std::optional(os.estadosMonitoreo.constLast());
    }

    int logsDe(const SolicitudId& id, TipoEventoLog tipo)
    {
        const auto d = esperar(root->solicitudes().obtener(id));
        if (!d || !d->esExito()) {
            return -1;
        }
        int n = 0;
        for (const LogResumen& l : d->valor().logs) {
            n += l.tipoEvento == tipo ? 1 : 0;
        }
        return n;
    }

    QTemporaryDir tmp;
    bool ok = false;
    QString rutaBase;
    fakes::FakeSecretStore secretos;
    fakes::FakeOperacionesSat sat;
    OperacionesSatNulo nulo;
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone::UTC)};
    fakes::FakeProgramador programadorEjecutor{reloj};
    fakes::FakeProgramador programadorWorker{reloj};
    fakes::FakeOSIntegration os;
    int salidas = 0;
    std::unique_ptr<AppCompositionRoot> root; // ultimo: se destruye primero
};

} // namespace

class TestMonitoreo : public QObject {
    Q_OBJECT

private slots:
    void init();

    void estadoDelWorkerLlegaAlMenuBar();
    void salidaEsperaHastaDiezSegundosYCancela();
    void cierreLimpioNoGeneraLogs();
    void adaptadorNuloFallaEnPreparacionVisible();
    void hiloGraficoRespondeConElPuertoBloqueado();
    void cambioDeCredencialDiagnosticaYRefrescaUi();
    void credencialNoListaNotificaSinEfectosConPermisoDenegado();
};

void TestMonitoreo::init()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral(
        "^(?!Populating font family aliases took|This plugin does not support ).*")));
}

void TestMonitoreo::estadoDelWorkerLlegaAlMenuBar()
{
    Grafo g;
    QVERIFY(g.ok);
    const auto id = g.crearSolicitud();
    QVERIFY(id);
    QVERIFY(g.iniciarApp());
    QVERIFY(!g.os.estadosMonitoreo.isEmpty()); // estado inicial reflejado
    QTRY_COMPARE(g.ultimoEstado()->fase, Fase::ActivoEnEspera);

    // Envio manual bloqueado en el puerto: Ejecutando(enviando).
    g.sat.bloquearSiguiente(Op::Enviar);
    g.root->worker().enviar(*id);
    QVERIFY(g.sat.esperarBloqueo());
    QTRY_COMPARE(g.ultimoEstado()->fase, Fase::Ejecutando);
    QCOMPARE(g.ultimoEstado()->actividad, Actividad::Enviando);
    g.sat.liberar();
    QTRY_COMPARE(g.ultimoEstado()->fase, Fase::ActivoEnEspera);

    // Pausa confirmada desde el menu bar: Pausado.
    g.os.emitirCambioMonitoreo(true);
    QTRY_COMPARE(g.ultimoEstado()->fase, Fase::Pausado);
    // Verificar ahora con pausa: intencion pendiente -> Pendientes: 1.
    g.root->worker().verificarAhora(*id);
    QTRY_COMPARE(g.ultimoEstado()->pendientes, 1);
    // Reanudar.
    g.os.emitirCambioMonitoreo(false);
    QTRY_VERIFY(g.ultimoEstado()->fase != Fase::Pausado);
}

void TestMonitoreo::salidaEsperaHastaDiezSegundosYCancela()
{
    Grafo g;
    QVERIFY(g.ok);
    const auto id = g.crearSolicitud();
    QVERIFY(id);
    QVERIFY(g.iniciarApp());

    g.sat.bloquearSiguiente(Op::Enviar, FaseOperacion::DespuesDeEnvio);
    g.root->worker().enviar(*id);
    QVERIFY(g.sat.esperarBloqueo());

    g.os.emitirSalir();
    g.os.emitirSalir(); // senal de cierre repetida
    QTRY_COMPARE(g.ultimoEstado()->fase, Fase::Deteniendo);
    QVERIFY(!g.root->ejecutor().aceptaOperaciones());

    // 9.999 s: sigue esperando; nada del cierre ha ocurrido.
    g.programadorEjecutor.avanzar(std::chrono::milliseconds(9999));
    QCoreApplication::processEvents();
    QCOMPARE(g.sat.activas(), 1);
    QCOMPARE(g.salidas, 0);
    QVERIFY(!g.os.salidaPreparada);

    // 10 s: cancelacion cooperativa, ejecutor termina, luego menu bar y salida.
    g.programadorEjecutor.avanzar(std::chrono::milliseconds(1));
    QTRY_COMPARE(g.salidas, 1);
    QVERIFY(g.os.salidaPreparada);
    QCOMPARE(g.sat.activas(), 0);

    // Exactamente un log de la operacion interrumpida (DespuesDeEnvio en un
    // envio -> EnvioIncierto, D12), aun con la salida repetida; tras reabrir.
    g.root.reset();
    g.abrir();
    QCOMPARE(g.logsDe(*id, TipoEventoLog::EnvioIncierto), 1);
}

void TestMonitoreo::cierreLimpioNoGeneraLogs()
{
    Grafo g;
    QVERIFY(g.ok);
    const auto id = g.crearSolicitud();
    QVERIFY(id);
    const int antes = [&] {
        const auto d = esperar(g.root->solicitudes().obtener(*id));
        return d && d->esExito() ? static_cast<int>(d->valor().logs.size()) : -1;
    }();
    QVERIFY(antes >= 1);
    QVERIFY(g.iniciarApp());
    g.os.emitirSalir();
    QTRY_COMPARE(g.salidas, 1);
    g.root.reset();
    g.abrir();
    const auto d = esperar(g.root->solicitudes().obtener(*id));
    QVERIFY(d && d->esExito());
    QCOMPARE(static_cast<int>(d->valor().logs.size()), antes);
}

void TestMonitoreo::adaptadorNuloFallaEnPreparacionVisible()
{
    Grafo g(true); // OperacionesSatNulo inyectado (sin adaptador SAT)
    QVERIFY(g.ok);
    const auto id = g.crearSolicitud();
    QVERIFY(id);
    g.root->iniciarMonitoreo();
    g.root->worker().enviar(*id);
    // D7: falla Preparacion -> vuelve a Creada con ultimo_error y log
    // envio_no_iniciado; nunca Enviada.
    QTRY_COMPARE(g.logsDe(*id, TipoEventoLog::EnvioNoIniciado), 1);
    const auto d = esperar(g.root->solicitudes().obtener(*id));
    QVERIFY(d && d->esExito());
    QCOMPARE(d->valor().resumen.estadoLocal, EstadoLocal::Creada);
    QVERIFY(d->valor().ultimoError.has_value() && !d->valor().ultimoError->isEmpty());
    QVERIFY(!d->valor().idSolicitudSat.has_value());
}

void TestMonitoreo::hiloGraficoRespondeConElPuertoBloqueado()
{
    Grafo g;
    QVERIFY(g.ok);
    const auto id = g.crearSolicitud();
    QVERIFY(id);
    g.root->iniciarMonitoreo();
    g.sat.bloquearSiguiente(Op::Enviar);
    g.root->worker().enviar(*id);
    QVERIFY(g.sat.esperarBloqueo());
    bool procesado = false;
    QTimer::singleShot(0, [&procesado] { procesado = true; });
    QTRY_VERIFY(procesado);
    QCOMPARE(g.sat.activas(), 1); // el puerto sigue bloqueado
    g.sat.liberar();
    QTRY_COMPARE(g.sat.activas(), 0);
}

void TestMonitoreo::cambioDeCredencialDiagnosticaYRefrescaUi()
{
    Grafo g;
    QVERIFY(g.ok);
    const auto id = g.crearSolicitud();
    QVERIFY(id);
    const auto perfiles = esperar(g.root->perfiles().listarNoEliminados());
    QVERIFY(perfiles && perfiles->esExito() && perfiles->valor().size() == 1);
    const PerfilId perfil = perfiles->valor().constFirst().id;

    g.root->iniciarMonitoreo();
    // Envio manual (5000 por defecto): la solicitud queda Enviada y con
    // verificacion programada; asi el perfil tiene trabajo para el gate D9.
    g.root->worker().enviar(*id);
    QTRY_VERIFY(g.sat.llamadas().contains(QStringLiteral("enviar:") + id->texto()));
    QTRY_COMPARE(g.sat.activas(), 0);

    QSignalSpy cambios(&g.root->credenciales(), &CredencialesSatService::credencialCambio);
    g.sat.fijarCredencial(perfil, EstadoCredencial::Vencida);
    // Diagnostico de app con la clave del estado, sin RFC.
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral(
        "^estado de credencial de un perfil cambio a %1$").arg(claveEstable(EstadoCredencial::Vencida))));
    g.reloj.fijar(g.reloj.ahora().addSecs(60 * 60)); // la verificacion ya es debida
    g.root->worker().ejecutarCiclo();
    QTRY_COMPARE(cambios.size(), 1);
    QCOMPARE(cambios.at(0).at(0).toString(), perfil.texto());

    // Sin cambio no se repite; un cambio nuevo vuelve a publicarse.
    g.root->worker().ejecutarCiclo();
    g.sat.fijarCredencial(perfil, EstadoCredencial::Lista);
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral(
        "^estado de credencial de un perfil cambio a %1$").arg(claveEstable(EstadoCredencial::Lista))));
    g.reloj.fijar(g.reloj.ahora().addSecs(60 * 60));
    g.root->worker().ejecutarCiclo();
    QTRY_COMPARE(cambios.size(), 2);
    QTRY_COMPARE(g.sat.activas(), 0);
}

void TestMonitoreo::credencialNoListaNotificaSinEfectosConPermisoDenegado()
{
    Grafo g;
    QVERIFY(g.ok);
    g.os.setNotificationStatus(OSIntegration::NotificationStatus::Denied);
    const auto id = g.crearSolicitud();
    QVERIFY(id);
    const auto perfiles = esperar(g.root->perfiles().listarNoEliminados());
    QVERIFY(perfiles && perfiles->esExito());
    const PerfilId perfil = perfiles->valor().constFirst().id;
    QVERIFY(g.iniciarApp());
    // La UI indica que las notificaciones estan deshabilitadas.
    QVERIFY(g.root->viewModels().app()->notificacionesDeshabilitadas());

    g.root->worker().enviar(*id);
    QTRY_VERIFY(g.sat.llamadas().contains(QStringLiteral("enviar:") + id->texto()));
    QTRY_COMPARE(g.sat.activas(), 0);
    // activas()==0 solo indica que el puerto respondio; el ejecutor persiste
    // despues (estado + log en una transaccion). Esperar el estado persistido.
    std::optional<SolicitudesService::ResultadoDetalle> antes;
    QTRY_VERIFY_WITH_TIMEOUT((antes = esperar(g.root->solicitudes().obtener(*id)), antes && antes->esExito()
                                  && antes->valor().resumen.estadoLocal == EstadoLocal::Enviada),
                             5000);

    QSignalSpy resultados(&g.os, &OSIntegration::notificacionTerminada);
    g.sat.fijarCredencial(perfil, EstadoCredencial::Vencida);
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("cambio a ")));
    g.reloj.fijar(g.reloj.ahora().addSecs(60 * 60));
    g.root->worker().ejecutarCiclo();
    QTRY_COMPARE(g.os.notificacionesPedidas.size(), 1);
    const OSIntegration::NotificacionLocal n = g.os.notificacionesPedidas.constFirst();
    QCOMPARE(n.id, QStringLiteral("credencial:%1:%2").arg(perfil.texto(), claveEstable(EstadoCredencial::Vencida)));
    QCOMPARE(n.tipo, QStringLiteral("credencial"));
    QVERIFY(!n.titulo.isEmpty() && !n.cuerpo.isEmpty()); // textos del servicio (catalogo D10)
    QVERIFY(!n.titulo.contains(QStringLiteral("EKU9003173C9")) && !n.cuerpo.contains(QStringLiteral("EKU9003173C9")));
    QCOMPARE(resultados.size(), 1);
    QCOMPARE(resultados.at(0).at(1).value<OSIntegration::NotificationSendResult>(),
             OSIntegration::NotificationSendResult::PermissionDenied);
    QVERIFY(g.os.notificacionesEntregadas.isEmpty());

    // Sin cambio de estado: ninguna notificacion nueva (dedupe).
    g.root->worker().ejecutarCiclo();
    QTRY_COMPARE(g.sat.activas(), 0);
    QCOMPARE(g.os.notificacionesPedidas.size(), 1);

    // El permiso denegado no cambia estados ni logs.
    const auto despues = esperar(g.root->solicitudes().obtener(*id));
    QVERIFY(despues && despues->esExito());
    QCOMPARE(despues->valor().resumen.estadoLocal, antes->valor().resumen.estadoLocal);
    QCOMPARE(despues->valor().logs.size(), antes->valor().logs.size());
}

#include "TestMonitoreo.moc"

int ejecutarTestMonitoreo(int argc, char* argv[])
{
    TestMonitoreo prueba;
    return QTest::qExec(&prueba, argc, argv);
}
