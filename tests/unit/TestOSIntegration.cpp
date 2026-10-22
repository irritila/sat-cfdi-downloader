#include "TestOSIntegration.h"

#include "fakes/FakeAlmacenKekRespaldo.h"
#include "fakes/FakeCarpetaRespaldoSistema.h"
#include "fakes/FakeOSIntegration.h"

#include <thread>

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
    QSignalSpy verFacturas(&os, &OSIntegration::verFacturasSolicitada);
    QSignalSpy monitoreo(&os, &OSIntegration::cambioMonitoreoSolicitado);
    QSignalSpy inicio(&os, &OSIntegration::cambioInicioAutomaticoSolicitado);
    QSignalSpy permiso(&os, &OSIntegration::permisoNotificacionesSolicitado);
    QSignalSpy prueba(&os, &OSIntegration::notificacionPruebaSolicitada);
    QSignalSpy salir(&os, &OSIntegration::salirSolicitado);
    QSignalSpy activar(&os, &OSIntegration::activarVentanaSolicitada);

    os.emitirMostrarVentana();
    os.emitirOcultarVentana();
    os.emitirEnfocarVentana();
    os.emitirVerFacturas();
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
    QCOMPARE(verFacturas.count(), 1);
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

void TestOSIntegration::despertarSoloEntreInicializarYSalida()
{
    FakeOSIntegration os;
    QSignalSpy despertar(&os, &OSIntegration::sistemaDesperto);
    QVERIFY(!os.emitirDespertar());
    os.inicializar();
    QVERIFY(os.emitirDespertar());
    QVERIFY(os.emitirDespertar());
    os.prepararSalida();
    QVERIFY(!os.emitirDespertar());
    QCOMPARE(despertar.count(), 2);
}

void TestOSIntegration::actividadDeSistemaViveConElToken()
{
    using M = OSIntegration::MotivoActividad;
    std::unique_ptr<OSIntegration::ActividadSistema> sobreviviente;
    {
        FakeOSIntegration os;
        QCOMPARE(os.actividadesActivas(), 0);
        {
            auto a = os.iniciarActividadSistema(M::Respaldo);
            QVERIFY(a);
            QCOMPARE(os.actividadesActivas(), 1);
            // Desde otro hilo (el respaldo no corre en el grafico).
            std::thread hilo([&os] {
                auto b = os.iniciarActividadSistema(M::Respaldo);
                Q_UNUSED(b);
            });
            hilo.join();
            QCOMPARE(os.actividadesActivas(), 1);
        }
        QCOMPARE(os.actividadesActivas(), 0);
        QCOMPARE(os.actividadesIniciadas(), 2);
        QCOMPARE(os.motivosActividad(), (QList<M>{M::Respaldo, M::Respaldo}));
        // El token puede vivir mas que el adaptador.
        sobreviviente = os.iniciarActividadSistema(M::Respaldo);
        QCOMPARE(os.actividadesActivas(), 1);
    }
    sobreviviente.reset();

    // Implementacion por omision del puerto: token no-op, nunca nulo.
    class Minima final : public OSIntegration {
    public:
        void inicializar() override {}
        LaunchContext launchContext() const override { return LaunchContext::Manual; }
        LoginItemStatus loginItemStatus() const override { return LoginItemStatus::Unavailable; }
        NotificationStatus notificationStatus() const override { return NotificationStatus::Unavailable; }
        void configurarLoginItem(bool) override {}
        void solicitarPermisoNotificaciones() override {}
        void enviarNotificacionPrueba(const QString&, const QString&) override {}
        void notificar(const NotificacionLocal&) override {}
        void mostrarEnFinder(const QString&, const QString&) override {}
        void abrirCarpetaEnFinder(const QString&, const QString&) override {}
        void abrirEnVistaPrevia(const QString&, const QString&) override {}
        void reflejarPreferenciaLoginItem(bool) override {}
        void reflejarMonitoreoPausado(bool) override {}
        void reflejarEstadoMonitoreo(const EstadoMonitoreo&) override {}
        void reflejarEstadoIcono(EstadoIcono) override {}
        void prepararSalida() override {}
    } minima;
    QVERIFY(minima.iniciarActividadSistema(M::Respaldo));
}

void TestOSIntegration::fakesDePlataformaDelRespaldo()
{
    using namespace satcfdi::respaldo;
    fakes::FakeAlmacenKekRespaldo almacen;
    BufferSecreto clave(kTamanoKek);
    const auto ref = almacen.guardar(KekRespaldo::desdeBytes(
        KekRespaldo::Tipo::Contrasena, ParametrosKdf::predeterminados(QByteArray(int(kTamanoSal), 'x')),
        std::move(clave)));
    QVERIFY(ref.esExito());
    QVERIFY(almacen.cargar(ref.valor()).esExito());
    almacen.fallarEn(fakes::FakeAlmacenKekRespaldo::Operacion::Cargar, ErrorAlmacenKek::Categoria::AlmacenBloqueado);
    QCOMPARE(almacen.cargar(ref.valor()).error().categoria, ErrorAlmacenKek::Categoria::AlmacenBloqueado);
    QCOMPARE(almacen.guardar(KekRespaldo{}).error().categoria, ErrorAlmacenKek::Categoria::KekInvalida);
    almacen.quitarFallos();

    // Ambitos: otro fake sobre el mismo llavero no ve ni borra lo ajeno.
    fakes::FakeAlmacenKekRespaldo otro(QStringLiteral("7e6d5c4b-3a29-4180-9f8e-7d6c5b4a3928"), almacen.llavero());
    QCOMPARE(otro.cargar(ref.valor()).error().categoria, ErrorAlmacenKek::Categoria::NoEncontrada);
    QVERIFY(otro.listar().valor().isEmpty());
    QVERIFY(otro.borrar(ref.valor()).esExito());
    QVERIFY(almacen.cargar(ref.valor()).esExito());
    QCOMPARE(almacen.llavero()->cantidadTotal(), 1);
    fakes::FakeAlmacenKekRespaldo invalido(QString(), almacen.llavero());
    QCOMPARE(invalido.listar().error(), satcfdi::respaldo::errorAmbitoInvalido());

    fakes::FakeCarpetaRespaldoSistema carpeta;
    carpeta.fijarCanonica(QStringLiteral("/Users/demo/Dropbox"),
                          QStringLiteral("/Users/demo/Library/CloudStorage/Dropbox"));
    const auto r = carpeta.crearReferencia(QStringLiteral("/Users/demo/Dropbox"));
    QVERIFY(r.esExito());
    QCOMPARE(r.valor().rutaCanonica, QStringLiteral("/Users/demo/Library/CloudStorage/Dropbox"));
    QCOMPARE(carpeta.resolver(r.valor().bookmark).valor().rutaCanonica, r.valor().rutaCanonica);
    carpeta.fijarResolucion(r.valor().bookmark, {QStringLiteral("/Users/demo/Otra"), r.valor().uuidVolumen, true});
    QVERIFY(carpeta.resolver(r.valor().bookmark).valor().obsoleto);
    carpeta.fallarResolver(r.valor().bookmark, ErrorCarpetaRespaldo::Categoria::NoResuelve);
    QCOMPARE(carpeta.resolver(r.valor().bookmark).error().categoria, ErrorCarpetaRespaldo::Categoria::NoResuelve);
    QCOMPARE(carpeta.crearReferencia(QStringLiteral("relativa")).error().categoria,
             ErrorCarpetaRespaldo::Categoria::RutaInvalida);
}

void TestOSIntegration::relanzamientoContratoYFake()
{
    using RR = OSIntegration::ResultadoRelanzamiento;
    QCOMPARE(claves(QMetaEnum::fromType<RR>()), (QStringList{"Programado", "NoSoportado", "Fallido"}));

    FakeOSIntegration os;
    QCOMPARE(os.relanzamientosSolicitados(), 0);
    QCOMPARE(os.solicitarRelanzamiento(), RR::Programado);
    QCOMPARE(os.relanzamientosSolicitados(), 1);
    os.resultadoRelanzamiento = RR::Fallido;
    QCOMPARE(os.solicitarRelanzamiento(), RR::Fallido);
    QCOMPARE(os.relanzamientosSolicitados(), 2);
    QVERIFY(!os.comandos.contains(QStringLiteral("solicitarRelanzamiento")));

    // Implementacion por omision del puerto (plataformas sin soporte).
    class SinRelanzamiento final : public OSIntegration {
    public:
        void inicializar() override {}
        LaunchContext launchContext() const override { return LaunchContext::Manual; }
        LoginItemStatus loginItemStatus() const override { return LoginItemStatus::Unavailable; }
        NotificationStatus notificationStatus() const override { return NotificationStatus::Unavailable; }
        void configurarLoginItem(bool) override {}
        void solicitarPermisoNotificaciones() override {}
        void enviarNotificacionPrueba(const QString&, const QString&) override {}
        void notificar(const NotificacionLocal&) override {}
        void mostrarEnFinder(const QString&, const QString&) override {}
        void abrirCarpetaEnFinder(const QString&, const QString&) override {}
        void abrirEnVistaPrevia(const QString&, const QString&) override {}
        void reflejarPreferenciaLoginItem(bool) override {}
        void reflejarMonitoreoPausado(bool) override {}
        void reflejarEstadoMonitoreo(const EstadoMonitoreo&) override {}
        void reflejarEstadoIcono(EstadoIcono) override {}
        void prepararSalida() override {}
    } sinRelanzamiento;
    QCOMPARE(sinRelanzamiento.solicitarRelanzamiento(), RR::NoSoportado);
}
