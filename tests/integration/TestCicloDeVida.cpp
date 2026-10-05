#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"
#include "app_core/AppLifecycleController.h"
#include "app_core/ExtensionCicloDeVida.h"

#include "application/configuration/ConfiguracionAppService.h"
#include "application/profiles/DemoPerfilesSatService.h"
#include "application/requests/DemoSolicitudesService.h"
#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include "fakes/FakeConfiguracionAppService.h"
#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakeOSIntegration.h"
#include "fakes/FakeSecretStore.h"

#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

#include <memory>
#include <optional>

using namespace satcfdi;
using fakes::FakeConfiguracionAppService;
using fakes::FakeOSIntegration;
using LoginItemStatus = OSIntegration::LoginItemStatus;
using NotificationStatus = OSIntegration::NotificationStatus;
using NotificationSendResult = OSIntegration::NotificationSendResult;

namespace {

struct ExtensionEspia final : ExtensionCicloDeVida {
    QStringList eventos;
    void aplicarMonitoreoPausado(bool pausado) override
    {
        eventos.append(pausado ? QStringLiteral("pausado(true)") : QStringLiteral("pausado(false)"));
    }
    QFuture<void> detener() override
    {
        eventos.append(QStringLiteral("detener"));
        return QtFuture::makeReadyVoidFuture();
    }
};

// Grafo minimo del controlador: fakes de SO y configuracion, QWindow simple
// (offscreen) y AppViewModel real sobre servicios demo (solo navegacion).
struct Escenario {
    explicit Escenario(OSIntegration::LaunchContext contexto = OSIntegration::LaunchContext::Manual)
    {
        os.setLaunchContext(contexto);
        ventana.resize(320, 240);
        controlador = std::make_unique<AppLifecycleController>(os, config, *vms.app(), &ventana);
        controlador->setExtension(&extension);
        controlador->setSalida([this] {
            os.comandos.append(QStringLiteral("salida"));
            ++salidas;
        });
    }

    // Inicia y confirma la lectura inicial de configuracion.
    void iniciarConfirmado()
    {
        controlador->iniciar();
        config.confirmarSiguiente();
    }

    const QList<PerfilResumen> perfiles = DemoPerfilesSatService::perfilesDemo();
    DemoPerfilesSatService perfilesService{perfiles};
    DemoSolicitudesService solicitudesService{perfiles};
    fakes::FakeCredencialesSatService credencialesService;
    PresentacionViewModels vms{&solicitudesService, &perfilesService, &credencialesService};
    FakeOSIntegration os;
    FakeConfiguracionAppService config;
    ExtensionEspia extension;
    QWindow ventana;
    std::unique_ptr<AppLifecycleController> controlador;
    int salidas = 0;
};

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

} // namespace

class TestCicloDeVida : public QObject {
    Q_OBJECT

private slots:
    void init();

    void arranqueManualMuestraYEnfocaVentana();
    void arranqueLoginItemNoMuestraVentana();
    void reflejaConfiguracionPersistidaAlArrancar();
    void cerrarVentanaLaOcultaYElProcesoSigue();
    void mostrarYActivarVentana();
    void nuevaSolicitudMuestraYNavega();
    void pausaSeReflejaSoloTrasConfirmar();
    void pausaFallidaConservaEstadoConfirmado();
    void inicioAutomaticoPersisteYDespuesConfiguraLoginItem_data();
    void inicioAutomaticoPersisteYDespuesConfiguraLoginItem();
    void inicioAutomaticoFallidoNoLlamaAlSistema();
    void permisoYNotificacionDePrueba();
    void salirRegistraCierreYTerminaEnOrden();
    void salirConCierreFallidoTerminaIgual();
    void quitDeAplicacionUsaElFlujoDeSalida();
    void rootPersistidoConCicloDeVida();
};

void TestCicloDeVida::init()
{
    // Excepciones: avisos propios de la plataforma offscreen (fuentes y
    // operaciones de ventana que no implementa, como raise()).
    QTest::failOnWarning(QRegularExpression(QStringLiteral(
        "^(?!Populating font family aliases took|This plugin does not support ).*")));
}

void TestCicloDeVida::arranqueManualMuestraYEnfocaVentana()
{
    Escenario e(OSIntegration::LaunchContext::Manual);
    QVERIFY(!e.ventana.isVisible());
    e.controlador->iniciar();
    QVERIFY(e.os.inicializado);
    QVERIFY(e.ventana.isVisible());
    QCOMPARE(e.config.llamadas, QStringList{QStringLiteral("obtener")});
    QCOMPARE(e.salidas, 0);
}

void TestCicloDeVida::arranqueLoginItemNoMuestraVentana()
{
    Escenario e(OSIntegration::LaunchContext::LoginItem);
    e.iniciarConfirmado();
    QVERIFY(e.os.inicializado); // menu bar creado
    QVERIFY(!e.ventana.isVisible());
    QCOMPARE(e.salidas, 0);     // el proceso sigue activo
}

void TestCicloDeVida::reflejaConfiguracionPersistidaAlArrancar()
{
    Escenario e;
    e.config.confirmada.inicioAutomaticoHabilitado = true;
    e.config.confirmada.monitoreoPausado = true;
    QSignalSpy reflejada(e.controlador.get(), &AppLifecycleController::configuracionReflejada);
    e.iniciarConfirmado();
    QTRY_COMPARE(reflejada.size(), 1);
    QCOMPARE(e.os.preferenciaReflejada, std::optional<bool>(true));
    QCOMPARE(e.os.monitoreoPausadoReflejado, std::optional<bool>(true));
    QCOMPARE(e.extension.eventos, QStringList{QStringLiteral("pausado(true)")});
    // Preferencia local y estado efectivo son datos separados.
    QCOMPARE(e.controlador->loginItemStatus(), LoginItemStatus::Disabled);
}

void TestCicloDeVida::cerrarVentanaLaOcultaYElProcesoSigue()
{
    Escenario e;
    e.iniciarConfirmado();
    QVERIFY(e.ventana.isVisible());
    QVERIFY(!e.ventana.close()); // cierre consumido
    QVERIFY(!e.ventana.isVisible());
    QCOMPARE(e.salidas, 0);
    QVERIFY(!e.os.salidaPreparada);
    QVERIFY(!e.config.llamadas.contains(QStringLiteral("registrarUltimoCierre")));
}

void TestCicloDeVida::mostrarYActivarVentana()
{
    Escenario e(OSIntegration::LaunchContext::LoginItem);
    e.iniciarConfirmado();
    e.os.emitirMostrarVentana();
    QVERIFY(e.ventana.isVisible());
    e.os.emitirOcultarVentana();
    QVERIFY(!e.ventana.isVisible());
    e.os.emitirActivarVentana(); // reapertura o segunda instancia
    QVERIFY(e.ventana.isVisible());
}

void TestCicloDeVida::nuevaSolicitudMuestraYNavega()
{
    Escenario e(OSIntegration::LaunchContext::LoginItem);
    e.iniciarConfirmado();
    QCOMPARE(e.vms.app()->pagina(), AppViewModel::Pagina::Lista);
    e.os.emitirNuevaSolicitud();
    QVERIFY(e.ventana.isVisible());
    QCOMPARE(e.vms.app()->pagina(), AppViewModel::Pagina::Nueva);
}

void TestCicloDeVida::pausaSeReflejaSoloTrasConfirmar()
{
    Escenario e;
    e.iniciarConfirmado();
    QTRY_COMPARE(e.os.monitoreoPausadoReflejado, std::optional<bool>(false));

    e.os.emitirCambioMonitoreo(true);
    QCOMPARE(e.config.llamadas.last(), QStringLiteral("actualizarMonitoreoPausado(true)"));
    QCOMPARE(e.os.monitoreoPausadoReflejado, std::optional<bool>(false)); // aun sin confirmar

    e.config.confirmarSiguiente();
    QTRY_COMPARE(e.os.monitoreoPausadoReflejado, std::optional<bool>(true));
    QVERIFY(e.config.confirmada.monitoreoPausado);
    QCOMPARE(e.extension.eventos.last(), QStringLiteral("pausado(true)"));

    e.os.emitirCambioMonitoreo(false); // reanudar
    e.config.confirmarSiguiente();
    QTRY_COMPARE(e.os.monitoreoPausadoReflejado, std::optional<bool>(false));
}

void TestCicloDeVida::pausaFallidaConservaEstadoConfirmado()
{
    Escenario e;
    e.iniciarConfirmado();
    QSignalSpy fallos(e.controlador.get(), &AppLifecycleController::operacionFallida);
    e.os.emitirCambioMonitoreo(true);
    e.config.fallarSiguiente(
        ErrorPersistencia::de(ErrorPersistencia::Tipo::Ocupado, QStringLiteral("ocupado")));
    QTRY_COMPARE(fallos.size(), 1);
    QCOMPARE(fallos.at(0).at(0).toString(), QStringLiteral("monitoreo"));
    QCOMPARE(e.os.monitoreoPausadoReflejado, std::optional<bool>(false));
    QVERIFY(!e.config.confirmada.monitoreoPausado);
    QVERIFY(!e.extension.eventos.contains(QStringLiteral("pausado(true)")));
}

void TestCicloDeVida::inicioAutomaticoPersisteYDespuesConfiguraLoginItem_data()
{
    QTest::addColumn<std::optional<LoginItemStatus>>("respuestaSo");
    QTest::addColumn<LoginItemStatus>("esperado");
    QTest::newRow("Enabled") << std::optional<LoginItemStatus>() << LoginItemStatus::Enabled;
    QTest::newRow("RequiresApproval") << std::optional(LoginItemStatus::RequiresApproval)
                                      << LoginItemStatus::RequiresApproval;
    QTest::newRow("Rejected") << std::optional(LoginItemStatus::Rejected) << LoginItemStatus::Rejected;
}

void TestCicloDeVida::inicioAutomaticoPersisteYDespuesConfiguraLoginItem()
{
    QFETCH(std::optional<LoginItemStatus>, respuestaSo);
    QFETCH(LoginItemStatus, esperado);

    Escenario e;
    e.os.resultadoHabilitarLoginItem = respuestaSo;
    e.iniciarConfirmado();
    QSignalSpy estados(e.controlador.get(), &AppLifecycleController::loginItemStatusChanged);
    e.os.comandos.clear();

    e.os.emitirCambioInicioAutomatico(true);
    QCOMPARE(e.config.llamadas.last(), QStringLiteral("actualizarInicioAutomatico(true)"));
    QVERIFY(e.os.comandos.isEmpty()); // nada llega al SO antes de persistir

    e.config.confirmarSiguiente();
    QTRY_COMPARE(e.os.comandos.size(), 2);
    QCOMPARE(e.os.comandos, (QStringList{QStringLiteral("configurarLoginItem(true)"),
                                         QStringLiteral("reflejarPreferenciaLoginItem(true)")}));
    QCOMPARE(estados.size(), 1);
    QCOMPARE(e.controlador->loginItemStatus(), esperado);
    QVERIFY(e.controlador->configuracionConfirmada()->inicioAutomaticoHabilitado);
}

void TestCicloDeVida::inicioAutomaticoFallidoNoLlamaAlSistema()
{
    Escenario e;
    e.iniciarConfirmado();
    QTRY_COMPARE(e.os.preferenciaReflejada, std::optional<bool>(false));
    QSignalSpy fallos(e.controlador.get(), &AppLifecycleController::operacionFallida);
    e.os.emitirCambioInicioAutomatico(true);
    e.config.fallarSiguiente(
        ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("disco")));
    QTRY_COMPARE(fallos.size(), 1);
    QVERIFY(!e.os.comandos.contains(QStringLiteral("configurarLoginItem(true)")));
    QCOMPARE(e.os.preferenciaReflejada, std::optional<bool>(false));
    QCOMPARE(e.controlador->loginItemStatus(), LoginItemStatus::Disabled);
}

void TestCicloDeVida::permisoYNotificacionDePrueba()
{
    {
        // NotDetermined -> el usuario pide permiso -> estado real (Granted).
        Escenario e;
        e.iniciarConfirmado();
        QCOMPARE(e.controlador->notificationStatus(), NotificationStatus::NotDetermined);
        QSignalSpy estados(e.controlador.get(), &AppLifecycleController::notificationStatusChanged);
        QSignalSpy envios(e.controlador.get(), &AppLifecycleController::notificacionPruebaTerminada);
        e.os.emitirPermisoNotificaciones();
        QVERIFY(e.os.comandos.contains(QStringLiteral("solicitarPermisoNotificaciones")));
        QCOMPARE(estados.size(), 1);
        QCOMPARE(e.controlador->notificationStatus(), NotificationStatus::Granted);

        e.os.emitirNotificacionPrueba();
        QCOMPARE(envios.size(), 1);
        QCOMPARE(envios.at(0).at(0).value<NotificationSendResult>(), NotificationSendResult::Sent);
        QCOMPARE(e.os.notificacionesEnviadas.size(), 1);
        QCOMPARE(e.os.notificacionesEnviadas.at(0).first, QStringLiteral("SAT CFDI Downloader"));
    }
    {
        // Denied: la prueba informa deshabilitado y la app sigue operativa.
        Escenario e;
        e.os.resultadoPermiso = NotificationStatus::Denied;
        e.iniciarConfirmado();
        QSignalSpy envios(e.controlador.get(), &AppLifecycleController::notificacionPruebaTerminada);
        e.os.emitirPermisoNotificaciones();
        QCOMPARE(e.controlador->notificationStatus(), NotificationStatus::Denied);
        e.os.emitirNotificacionPrueba();
        QCOMPARE(envios.at(0).at(0).value<NotificationSendResult>(),
                 NotificationSendResult::PermissionDenied);
        QVERIFY(e.os.notificacionesEnviadas.isEmpty());
        QCOMPARE(e.salidas, 0);
        e.os.emitirMostrarVentana();
        QVERIFY(e.ventana.isVisible());
    }
}

void TestCicloDeVida::salirRegistraCierreYTerminaEnOrden()
{
    Escenario e;
    e.iniciarConfirmado();
    e.os.comandos.clear();
    QSignalSpy iniciada(e.controlador.get(), &AppLifecycleController::salidaIniciada);

    e.os.emitirSalir();
    QCOMPARE(iniciada.size(), 1);
    QCOMPARE(e.extension.eventos.last(), QStringLiteral("detener"));
    QCOMPARE(e.config.llamadas.last(), QStringLiteral("registrarUltimoCierre"));
    QVERIFY(!e.os.salidaPreparada); // espera la confirmacion
    QCOMPARE(e.salidas, 0);

    e.os.emitirSalir(); // repetido: ignorado
    QCOMPARE(e.config.pendientes(), 1);

    e.config.confirmarSiguiente();
    QTRY_COMPARE(e.salidas, 1);
    QCOMPARE(e.os.comandos, (QStringList{QStringLiteral("prepararSalida"), QStringLiteral("salida")}));
    QVERIFY(e.config.confirmada.ultimoCierreEn.has_value());
}

void TestCicloDeVida::salirConCierreFallidoTerminaIgual()
{
    Escenario e;
    e.iniciarConfirmado();
    QSignalSpy fallos(e.controlador.get(), &AppLifecycleController::operacionFallida);
    e.os.emitirSalir();
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("No se pudo registrar el ultimo cierre")));
    e.config.fallarSiguiente(
        ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("disco")));
    QTRY_COMPARE(e.salidas, 1);
    QVERIFY(e.os.salidaPreparada);
    QCOMPARE(fallos.size(), 1);
}

void TestCicloDeVida::quitDeAplicacionUsaElFlujoDeSalida()
{
    Escenario e;
    e.iniciarConfirmado();
    QSignalSpy iniciada(e.controlador.get(), &AppLifecycleController::salidaIniciada);
    QEvent quit(QEvent::Quit);
    QVERIFY(QCoreApplication::sendEvent(QCoreApplication::instance(), &quit));
    QCOMPARE(iniciada.size(), 1);
    QCOMPARE(e.config.llamadas.last(), QStringLiteral("registrarUltimoCierre"));
    e.config.confirmarSiguiente();
    QTRY_COMPARE(e.salidas, 1);
}

void TestCicloDeVida::rootPersistidoConCicloDeVida()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    AppBootstrapper::Opciones opciones;
    opciones.directorioDatos = tmp.path();
    {
        const auto arranque = AppBootstrapper(opciones).preparar();
        QVERIFY(arranque);
        FakeOSIntegration os;
        fakes::FakeSecretStore secretos;
        int salidas = 0;
        AppCompositionRoot root(arranque.valor().rutaBase, secretos);
        QVERIFY(root.cargar());
        AppLifecycleController& ctrl = root.iniciarCicloDeVida(os, nullptr, [&salidas] { ++salidas; });
        QVERIFY(os.inicializado);
        auto* ventana = qobject_cast<QWindow*>(root.engine()->rootObjects().constFirst());
        QVERIFY(ventana && ventana->isVisible()); // arranque Manual

        // Instalacion nueva: preferencia false (SQLite real).
        QTRY_COMPARE(os.preferenciaReflejada, std::optional<bool>(false));
        QVERIFY(!ctrl.configuracionConfirmada()->inicioAutomaticoHabilitado);

        QVERIFY(!ventana->close());
        QVERIFY(!ventana->isVisible());

        os.emitirCambioMonitoreo(true);
        QTRY_COMPARE(os.monitoreoPausadoReflejado, std::optional<bool>(true));

        os.emitirSalir();
        QTRY_COMPARE(salidas, 1);
        QVERIFY(os.salidaPreparada);
    } // destruye el grafo en el orden de T003/T004

    const auto arranque = AppBootstrapper(opciones).preparar();
    QVERIFY(arranque);
    fakes::FakeSecretStore secretos;
    AppCompositionRoot root(arranque.valor().rutaBase, secretos);
    const auto c = esperar(root.configuracion().obtener());
    QVERIFY(c && c->esExito());
    QVERIFY(c->valor().monitoreoPausado);
    QVERIFY(c->valor().ultimoCierreEn.has_value());
    QVERIFY(!c->valor().inicioAutomaticoHabilitado);
}

#include "TestCicloDeVida.moc"

int ejecutarTestCicloDeVida(int argc, char* argv[])
{
    TestCicloDeVida prueba;
    return QTest::qExec(&prueba, argc, argv);
}
