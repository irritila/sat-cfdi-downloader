#include "TestOSIntegration.h"

#include "fakes/FakeOSIntegration.h"

#include <QCoreApplication>
#include <QMetaEnum>
#include <QSignalSpy>
#include <QTest>

#include <type_traits>

using namespace satcfdi;
using fakes::FakeOSIntegration;
using LS = OSIntegration::LoginItemStatus;
using NS = OSIntegration::NotificationStatus;
using SR = OSIntegration::NotificationSendResult;

static_assert(std::is_abstract_v<OSIntegration>);
static_assert(std::is_base_of_v<QObject, OSIntegration>);
static_assert(std::has_virtual_destructor_v<OSIntegration>);

namespace {

QStringList claves(const QMetaEnum& e)
{
    QStringList r;
    for (int i = 0; i < e.keyCount(); ++i) {
        r.append(QString::fromLatin1(e.key(i)));
    }
    return r;
}

} // namespace

void TestOSIntegration::enumsDelContrato()
{
    QCOMPARE(claves(QMetaEnum::fromType<OSIntegration::LaunchContext>()),
             (QStringList{"Manual", "LoginItem"}));
    QCOMPARE(claves(QMetaEnum::fromType<LS>()),
             (QStringList{"Disabled", "Enabled", "RequiresApproval", "Rejected", "Unavailable"}));
    QCOMPARE(claves(QMetaEnum::fromType<NS>()),
             (QStringList{"NotDetermined", "Granted", "Denied", "Unavailable"}));
    QCOMPARE(claves(QMetaEnum::fromType<SR>()),
             (QStringList{"Sent", "PermissionDenied", "PermissionNotDetermined", "Unavailable", "Failed"}));
}

void TestOSIntegration::intencionesSeEmitenConArgumentos()
{
    FakeOSIntegration os;
    QSignalSpy mostrar(&os, &OSIntegration::mostrarVentanaSolicitada);
    QSignalSpy ocultar(&os, &OSIntegration::ocultarVentanaSolicitada);
    QSignalSpy enfocar(&os, &OSIntegration::enfocarVentanaSolicitada);
    QSignalSpy nueva(&os, &OSIntegration::nuevaSolicitudSolicitada);
    QSignalSpy monitoreo(&os, &OSIntegration::cambioMonitoreoSolicitado);
    QSignalSpy inicio(&os, &OSIntegration::cambioInicioAutomaticoSolicitado);
    QSignalSpy permiso(&os, &OSIntegration::permisoNotificacionesSolicitado);
    QSignalSpy prueba(&os, &OSIntegration::notificacionPruebaSolicitada);
    QSignalSpy salir(&os, &OSIntegration::salirSolicitado);
    QSignalSpy activar(&os, &OSIntegration::activarVentanaSolicitada);

    os.emitirMostrarVentana();
    os.emitirOcultarVentana();
    os.emitirEnfocarVentana();
    os.emitirNuevaSolicitud();
    os.emitirCambioMonitoreo(true);
    os.emitirCambioMonitoreo(false);
    os.emitirCambioInicioAutomatico(true);
    os.emitirPermisoNotificaciones();
    os.emitirNotificacionPrueba();
    os.emitirSalir();
    os.emitirActivarVentana();

    QCOMPARE(mostrar.count(), 1);
    QCOMPARE(ocultar.count(), 1);
    QCOMPARE(enfocar.count(), 1);
    QCOMPARE(nueva.count(), 1);
    QCOMPARE(monitoreo.count(), 2);
    QCOMPARE(monitoreo.at(0).at(0).toBool(), true);
    QCOMPARE(monitoreo.at(1).at(0).toBool(), false);
    QCOMPARE(inicio.count(), 1);
    QCOMPARE(inicio.at(0).at(0).toBool(), true);
    QCOMPARE(permiso.count(), 1);
    QCOMPARE(prueba.count(), 1);
    QCOMPARE(salir.count(), 1);
    QCOMPARE(activar.count(), 1);
    // Emitir intenciones no ejecuta comandos de SO.
    QVERIFY(os.comandos.isEmpty());
}

void TestOSIntegration::contextoDeArranque()
{
    FakeOSIntegration os;
    QCOMPARE(os.launchContext(), OSIntegration::LaunchContext::Manual);
    os.setLaunchContext(OSIntegration::LaunchContext::LoginItem);
    QCOMPARE(os.launchContext(), OSIntegration::LaunchContext::LoginItem);
    os.inicializar();
    QVERIFY(os.inicializado);
    QCOMPARE(os.comandos, QStringList{"inicializar"});
}

void TestOSIntegration::loginItemHabilitarYDeshabilitar()
{
    FakeOSIntegration os;
    QCOMPARE(os.loginItemStatus(), LS::Disabled);
    QSignalSpy cambio(&os, &OSIntegration::loginItemStatusChanged);
    QSignalSpy configurado(&os, &OSIntegration::loginItemConfigurado);

    os.configurarLoginItem(true);
    QCOMPARE(os.loginItemStatus(), LS::Enabled);
    QCOMPARE(cambio.count(), 1);
    QCOMPARE(cambio.at(0).at(0).value<LS>(), LS::Enabled);
    QCOMPARE(configurado.count(), 1);
    QCOMPARE(configurado.at(0).at(0).toBool(), true);
    QCOMPARE(configurado.at(0).at(1).value<LS>(), LS::Enabled);

    // Repetir sin cambio: loginItemConfigurado siempre, Changed no.
    os.configurarLoginItem(true);
    QCOMPARE(cambio.count(), 1);
    QCOMPARE(configurado.count(), 2);

    os.configurarLoginItem(false);
    QCOMPARE(os.loginItemStatus(), LS::Disabled);
    QCOMPARE(cambio.count(), 2);
    QCOMPARE(os.comandos, (QStringList{"configurarLoginItem(true)", "configurarLoginItem(true)",
                                       "configurarLoginItem(false)"}));
}

void TestOSIntegration::loginItemEstadosEfectivosDistintosDeLaPreferencia_data()
{
    QTest::addColumn<LS>("efectivo");
    QTest::newRow("RequiresApproval") << LS::RequiresApproval;
    QTest::newRow("Rejected") << LS::Rejected;
    QTest::newRow("Disabled") << LS::Disabled;
}

void TestOSIntegration::loginItemEstadosEfectivosDistintosDeLaPreferencia()
{
    QFETCH(LS, efectivo);
    FakeOSIntegration os;
    os.setLoginItemStatus(LS::Enabled);
    os.resultadoHabilitarLoginItem = efectivo;
    QSignalSpy configurado(&os, &OSIntegration::loginItemConfigurado);

    os.configurarLoginItem(true);
    QCOMPARE(configurado.count(), 1);
    // La peticion (habilitar) y el estado efectivo se reportan por separado.
    QCOMPARE(configurado.at(0).at(0).toBool(), true);
    QCOMPARE(configurado.at(0).at(1).value<LS>(), efectivo);
    QCOMPARE(os.loginItemStatus(), efectivo);
    QVERIFY(!os.preferenciaReflejada.has_value());
}

void TestOSIntegration::loginItemNoDisponiblePermaneceNoDisponible()
{
    FakeOSIntegration os;
    os.setLoginItemStatus(LS::Unavailable);
    QSignalSpy cambio(&os, &OSIntegration::loginItemStatusChanged);
    QSignalSpy configurado(&os, &OSIntegration::loginItemConfigurado);
    os.configurarLoginItem(true);
    QCOMPARE(cambio.count(), 0);
    QCOMPARE(configurado.count(), 1);
    QCOMPARE(configurado.at(0).at(1).value<LS>(), LS::Unavailable);
}

void TestOSIntegration::permisoNotificaciones_data()
{
    QTest::addColumn<NS>("inicial");
    QTest::addColumn<std::optional<NS>>("respuestaSo");
    QTest::addColumn<NS>("final");
    QTest::addColumn<int>("cambios");
    QTest::newRow("concedido") << NS::NotDetermined << std::optional<NS>() << NS::Granted << 1;
    QTest::newRow("denegado") << NS::NotDetermined << std::optional<NS>(NS::Denied) << NS::Denied << 1;
    QTest::newRow("ya-denegado") << NS::Denied << std::optional<NS>() << NS::Denied << 0;
    QTest::newRow("no-disponible") << NS::Unavailable << std::optional<NS>() << NS::Unavailable << 0;
}

void TestOSIntegration::permisoNotificaciones()
{
    QFETCH(NS, inicial);
    QFETCH(std::optional<NS>, respuestaSo);
    QFETCH(NS, final);
    QFETCH(int, cambios);
    FakeOSIntegration os;
    os.setNotificationStatus(inicial);
    os.resultadoPermiso = respuestaSo;
    QSignalSpy cambio(&os, &OSIntegration::notificationStatusChanged);
    QSignalSpy resuelto(&os, &OSIntegration::permisoNotificacionesResuelto);

    os.solicitarPermisoNotificaciones();
    QCOMPARE(os.notificationStatus(), final);
    QCOMPARE(cambio.count(), cambios);
    QCOMPARE(resuelto.count(), 1);
    QCOMPARE(resuelto.at(0).at(0).value<NS>(), final);
    QCOMPARE(os.comandos, QStringList{"solicitarPermisoNotificaciones"});
}

void TestOSIntegration::envioNotificacionSegunPermiso_data()
{
    QTest::addColumn<NS>("permiso");
    QTest::addColumn<SR>("esperado");
    QTest::newRow("granted") << NS::Granted << SR::Sent;
    QTest::newRow("denied") << NS::Denied << SR::PermissionDenied;
    QTest::newRow("not-determined") << NS::NotDetermined << SR::PermissionNotDetermined;
    QTest::newRow("unavailable") << NS::Unavailable << SR::Unavailable;
}

void TestOSIntegration::envioNotificacionSegunPermiso()
{
    QFETCH(NS, permiso);
    QFETCH(SR, esperado);
    FakeOSIntegration os;
    os.setNotificationStatus(permiso);
    QSignalSpy terminada(&os, &OSIntegration::notificacionPruebaTerminada);
    QSignalSpy cambioPermiso(&os, &OSIntegration::notificationStatusChanged);

    os.enviarNotificacionPrueba(QStringLiteral("Titulo"), QStringLiteral("Cuerpo"));
    QCOMPARE(terminada.count(), 1);
    QCOMPARE(terminada.at(0).at(0).value<SR>(), esperado);
    // Enviar nunca solicita permiso por si mismo.
    QCOMPARE(cambioPermiso.count(), 0);
    QVERIFY(!os.comandos.contains(QStringLiteral("solicitarPermisoNotificaciones")));
    if (esperado == SR::Sent) {
        QCOMPARE(os.notificacionesEnviadas.size(), 1);
        QCOMPARE(os.notificacionesEnviadas.at(0).first, QStringLiteral("Titulo"));
        QCOMPARE(os.notificacionesEnviadas.at(0).second, QStringLiteral("Cuerpo"));
    } else {
        QVERIFY(os.notificacionesEnviadas.isEmpty());
    }
}

void TestOSIntegration::comandosPendientesSeCompletanBajoControl()
{
    FakeOSIntegration os;
    os.responderInmediato = false;
    os.setNotificationStatus(NS::Granted);
    QSignalSpy configurado(&os, &OSIntegration::loginItemConfigurado);
    QSignalSpy resuelto(&os, &OSIntegration::permisoNotificacionesResuelto);
    QSignalSpy terminada(&os, &OSIntegration::notificacionPruebaTerminada);

    os.configurarLoginItem(true);
    os.solicitarPermisoNotificaciones();
    os.enviarNotificacionPrueba(QStringLiteral("T"), QStringLiteral("C"));
    QCOMPARE(configurado.count(), 0);
    QCOMPARE(resuelto.count(), 0);
    QCOMPARE(terminada.count(), 0);
    QCOMPARE(os.loginItemStatus(), LS::Disabled);
    QCOMPARE(os.pendientesLoginItem(), 1);
    QCOMPARE(os.pendientesPermiso(), 1);
    QCOMPARE(os.pendientesEnvio(), 1);

    os.completarLoginItem();
    os.completarPermiso();
    os.completarEnvio();
    QCOMPARE(configurado.count(), 1);
    QCOMPARE(resuelto.count(), 1);
    QCOMPARE(terminada.count(), 1);
    QCOMPARE(os.loginItemStatus(), LS::Enabled);
    QCOMPARE(os.pendientesLoginItem(), 0);
}

void TestOSIntegration::reflejarYPrepararSalidaSoloRegistran()
{
    FakeOSIntegration os;
    QSignalSpy cambioLogin(&os, &OSIntegration::loginItemStatusChanged);
    QSignalSpy monitoreo(&os, &OSIntegration::cambioMonitoreoSolicitado);
    QSignalSpy inicio(&os, &OSIntegration::cambioInicioAutomaticoSolicitado);

    os.reflejarPreferenciaLoginItem(true);
    os.reflejarMonitoreoPausado(true);
    os.prepararSalida();
    os.prepararSalida();

    QCOMPARE(os.preferenciaReflejada, std::optional<bool>(true));
    QCOMPARE(os.monitoreoPausadoReflejado, std::optional<bool>(true));
    QVERIFY(os.salidaPreparada);
    // Reflejar no cambia el estado efectivo ni emite intenciones.
    QCOMPARE(os.loginItemStatus(), LS::Disabled);
    QCOMPARE(cambioLogin.count(), 0);
    QCOMPARE(monitoreo.count(), 0);
    QCOMPARE(inicio.count(), 0);
    QCOMPARE(os.comandos, (QStringList{"reflejarPreferenciaLoginItem(true)", "reflejarMonitoreoPausado(true)",
                                       "prepararSalida", "prepararSalida"}));
}

void TestOSIntegration::senalesDeEstadoCruzanConexionEncolada()
{
    // Los enums estan registrados (Q_ENUM): las senales funcionan con
    // conexiones encoladas, como las usaria un consumidor en otro contexto.
    FakeOSIntegration os;
    LS recibido = LS::Disabled;
    NS permiso = NS::NotDetermined;
    QObject receptor;
    connect(&os, &OSIntegration::loginItemStatusChanged, &receptor,
            [&](LS s) { recibido = s; }, Qt::QueuedConnection);
    connect(&os, &OSIntegration::notificationStatusChanged, &receptor,
            [&](NS s) { permiso = s; }, Qt::QueuedConnection);
    os.cambiarLoginItemStatus(LS::RequiresApproval);
    os.cambiarNotificationStatus(NS::Denied);
    QCOMPARE(recibido, LS::Disabled);
    QCOMPARE(permiso, NS::NotDetermined);
    QCoreApplication::processEvents(); // entrega los eventos ya encolados
    QCOMPARE(recibido, LS::RequiresApproval);
    QCOMPARE(permiso, NS::Denied);
}
