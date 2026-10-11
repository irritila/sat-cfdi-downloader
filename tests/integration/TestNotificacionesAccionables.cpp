// T014.4 (integracion): notificaciones accionables (D1) y estado agregado del
// icono del menu bar (D2) con FakeOSIntegration.
// - Enrutamiento en AppLifecycleController sobre AppViewModel real y servicios
//   demo: detalle, solicitud inexistente, Perfiles con seleccion, Finder,
//   destino invalido, arranque por notificacion (cola) y salida.
// - Root persistido real con FakeSatGateway: destino/acciones de las
//   notificaciones emitidas, acciones sobre ellas sin cambios en la base
//   (permiso concedido o denegado) e icono Normal/Atencion/Trabajando/Pausado
//   (en pausa sin llamadas al puerto SAT).

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"
#include "app_core/AppLifecycleController.h"
#include "app_core/ExtensionCicloDeVida.h"
#include "app_core/IntegracionWorker.h"

#include "application/estadoagregado/MonitorEstadoAgregado.h"
#include "application/operaciones/WorkerLocal.h"
#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/DemoPerfilesSatService.h"
#include "application/profiles/PerfilesSatService.h"
#include "application/requests/DemoSolicitudesService.h"
#include "application/requests/SolicitudesService.h"
#include "presentation/viewmodels/AccionesFinder.h"
#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/PerfilesSatViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include "fakes/FakeConfiguracionAppService.h"
#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakeOSIntegration.h"
#include "fakes/FakeProgramador.h"
#include "fakes/FakeSatGateway.h"
#include "fakes/FakeSecretStore.h"

#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QWindow>

#include <memory>
#include <optional>

using namespace satcfdi;
using fakes::FakeOSIntegration;
using Accion = OSIntegration::AccionNotificacion;
using Icono = OSIntegration::EstadoIcono;
using Pagina = AppViewModel::Pagina;

namespace {

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

struct ExtensionNula final : ExtensionCicloDeVida {
    void aplicarMonitoreoPausado(bool) override {}
    QFuture<void> detener() override { return QtFuture::makeReadyVoidFuture(); }
};

// AccionesFinder controlable: registra la carpeta pedida y responde `estado`.
struct FinderFalso final : AccionesFinder {
    ResultadoAccionFinder::Estado estado = ResultadoAccionFinder::Estado::Mostrado;
    QStringList carpetas;
    QFuture<ResultadoAccionFinder> mostrarPaquete(const SolicitudId&, const QString&) override
    {
        return QtFuture::makeReadyValueFuture(ResultadoAccionFinder{ResultadoAccionFinder::Estado::Fallido, {}});
    }
    QFuture<ResultadoAccionFinder> abrirCarpetaSolicitud(const SolicitudId& id) override
    {
        carpetas.append(id.texto());
        const bool ok = estado == ResultadoAccionFinder::Estado::Mostrado;
        return QtFuture::makeReadyValueFuture(
            ResultadoAccionFinder{estado, ok ? QString() : QStringLiteral("Carpeta local no encontrada")});
    }
    QFuture<ResultadoAccionFinder> abrirCarpetaPaquetes() override
    {
        return QtFuture::makeReadyValueFuture(ResultadoAccionFinder{ResultadoAccionFinder::Estado::Fallido, {}});
    }
};

// Controlador real sobre AppViewModel real y servicios demo (solo navegacion).
struct Escenario {
    explicit Escenario(OSIntegration::LaunchContext contexto = OSIntegration::LaunchContext::Manual)
    {
        os.setLaunchContext(contexto);
        ventana.resize(320, 240);
        vms.setAccionesFinder(&finder);
        controlador = std::make_unique<AppLifecycleController>(os, config, *vms.app(), &ventana);
        controlador->setExtension(&extension);
        controlador->setSalida([this] { ++salidas; });
    }

    void iniciar()
    {
        controlador->iniciar();
        config.confirmarSiguiente();
    }

    QString primeraSolicitud()
    {
        const auto lista = esperar(solicitudesService.listar());
        return lista && lista->esExito() && !lista->valor().isEmpty() ? lista->valor().first().id.texto()
                                                                       : QString();
    }

    const QList<PerfilResumen> perfiles = DemoPerfilesSatService::perfilesDemo();
    DemoPerfilesSatService perfilesService{perfiles};
    DemoSolicitudesService solicitudesService{perfiles};
    fakes::FakeCredencialesSatService credencialesService;
    PresentacionViewModels vms{&solicitudesService, &perfilesService, &credencialesService};
    FinderFalso finder;
    FakeOSIntegration os;
    fakes::FakeConfiguracionAppService config;
    ExtensionNula extension;
    QWindow ventana;
    std::unique_ptr<AppLifecycleController> controlador;
    int salidas = 0;
};

// Root persistido real (como TestFlujoSat).
struct Flujo {
    explicit Flujo(OSIntegration::NotificationStatus permiso = OSIntegration::NotificationStatus::Granted)
    {
        os.setNotificationStatus(permiso);
        gateway.ahora = reloj.funcion();
        AppBootstrapper::Opciones opciones;
        opciones.directorioDatos = tmp.path();
        const auto arranque = AppBootstrapper(opciones).preparar();
        ok = arranque.esExito();
        if (!ok) {
            return;
        }
        OpcionesMonitoreo m;
        m.satGateway = &gateway;
        m.reloj = reloj.funcion();
        m.programadorEjecutor = &programadorEjecutor;
        m.programadorWorker = &programadorWorker;
        m.programadorVencimiento = &programadorVencimiento;
        root = std::make_unique<AppCompositionRoot>(arranque.valor().rutaBase, secretos, m);
    }

    void iniciar()
    {
        QVERIFY(root->cargar());
        root->iniciarCicloDeVida(os, nullptr, [this] { ++salidas; });
    }

    std::optional<PerfilId> crearPerfil()
    {
        const auto perfil = esperar(root->perfiles().crear(secretos.rfcCertificado, QStringLiteral("Perfil")));
        return perfil && perfil->esExito() ? std::optional(perfil->valor().id) : std::nullopt;
    }

    bool importar(const PerfilId& perfil)
    {
        EntradaEFirma e;
        e.rutaCertificado = QStringLiteral("/ficticio/efirma.cer");
        e.rutaLlavePrivada = QStringLiteral("/ficticio/efirma.key");
        e.contrasena = BufferSecreto::desdeBytes(secretos.contrasenaValida.constData(),
                                                 static_cast<std::size_t>(secretos.contrasenaValida.size()));
        const auto r = esperar(root->credenciales().importar(perfil, std::move(e)));
        return r && r->esExito();
    }

    std::optional<SolicitudId> crearSolicitud(const PerfilId& perfil, int dia)
    {
        NuevaSolicitudRequest r;
        r.perfilId = perfil;
        r.tipoDescarga = TipoDescarga::Recibidos;
        r.fechaInicial = QDate(2026, 9, dia);
        r.fechaFinal = QDate(2026, 9, dia);
        const auto creada = esperar(root->solicitudes().crear(r));
        return creada && creada->esExito() ? std::optional(creada->valor()) : std::nullopt;
    }

    std::optional<SolicitudDetalle> detalle(const SolicitudId& id)
    {
        const auto d = esperar(root->solicitudes().obtener(id));
        return d && d->esExito() ? std::optional(d->valor()) : std::nullopt;
    }

    int llamadasSat() const
    {
        using Op = fakes::FakeSatGateway::Op;
        return gateway.llamadas(Op::Autenticar) + gateway.llamadas(Op::Crear) + gateway.llamadas(Op::Verificar)
               + gateway.llamadas(Op::Descargar);
    }

    QTemporaryDir tmp;
    bool ok = false;
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone::UTC)};
    fakes::FakeProgramador programadorEjecutor{reloj};
    fakes::FakeProgramador programadorWorker{reloj};
    fakes::FakeProgramador programadorVencimiento{reloj};
    fakes::FakeSecretStore secretos;
    fakes::FakeSatGateway gateway;
    FakeOSIntegration os;
    int salidas = 0;
    std::unique_ptr<AppCompositionRoot> root;
};

QByteArray volcado(const QString& rutaBase)
{
    QProcess p;
    p.start(QStringLiteral("/usr/bin/sqlite3"), {rutaBase, QStringLiteral(".dump")});
    if (!p.waitForFinished(10000) || p.exitCode() != 0) {
        return {};
    }
    return p.readAllStandardOutput();
}

bool sinDuplicadosConsecutivos(const QList<Icono>& estados)
{
    for (qsizetype i = 1; i < estados.size(); ++i) {
        if (estados.at(i) == estados.at(i - 1)) {
            return false;
        }
    }
    return true;
}

} // namespace

class TestNotificacionesAccionables : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(
            "^(?!Populating font family aliases took|This plugin does not support ).*")));
    }

    void rutaDeNotificacionPura();
    void adaptacionANotificacionLocal();
    void abrirSolicitudConAppAbiertaUOculta();
    void solicitudInexistenteAbreLaLista();
    void perfilSeleccionadoEnPerfiles();
    void mostrarEnFinderSoloTraeVentanaConAviso();
    void destinoInvalidoSoloMuestraVentana();
    void arranquePorNotificacionSeEncola();
    void durantesLaSalidaSeIgnora();
    void notificacionesDelRootSonAccionables_data();
    void notificacionesDelRootSonAccionables();
    void iconoSigueEstadoAgregado();
};

void TestNotificacionesAccionables::rutaDeNotificacionPura()
{
    using T = RutaNotificacion::Tipo;
    using D = OSIntegration::DestinoNotificacion;
    const QString sol = SolicitudId::generar().texto();
    const QString per = PerfilId::generar().texto();
    const D dSol{D::Tipo::Solicitud, sol};
    const D dPer{D::Tipo::Perfil, per};
    QCOMPARE(rutaDeNotificacion(dSol, Accion::Abrir), (RutaNotificacion{T::DetalleSolicitud, sol}));
    QCOMPARE(rutaDeNotificacion(dSol, Accion::MostrarEnFinder), (RutaNotificacion{T::FinderSolicitud, sol}));
    QCOMPARE(rutaDeNotificacion(dSol, Accion::AbrirPerfiles), (RutaNotificacion{T::DetalleSolicitud, sol}));
    QCOMPARE(rutaDeNotificacion(dPer, Accion::Abrir), (RutaNotificacion{T::PerfilSeleccionado, per}));
    QCOMPARE(rutaDeNotificacion(dPer, Accion::AbrirPerfiles), (RutaNotificacion{T::PerfilSeleccionado, per}));
    QCOMPARE(rutaDeNotificacion(dPer, Accion::MostrarEnFinder), (RutaNotificacion{T::PerfilSeleccionado, per}));
    QCOMPARE(rutaDeNotificacion(D{}, Accion::Abrir), RutaNotificacion{});
    QCOMPARE(rutaDeNotificacion(D{D::Tipo::Solicitud, QStringLiteral("EKU9003173C9")}, Accion::Abrir),
             RutaNotificacion{});
    QCOMPARE(rutaDeNotificacion(D{D::Tipo::Perfil, sol.toUpper()}, Accion::Abrir), RutaNotificacion{});
}

void TestNotificacionesAccionables::adaptacionANotificacionLocal()
{
    using D = OSIntegration::DestinoNotificacion;
    const QString sol = SolicitudId::generar().texto();
    const QString per = PerfilId::generar().texto();
    const auto local = [](const QString& tipo, DestinoNotificacion::Tipo t, const QString& id) {
        Notificacion n{QStringLiteral("x"), tipo, QStringLiteral("t"), QStringLiteral("c"), {t, id}};
        return notificacionLocalDe(n);
    };
    for (const char* tipo : {"terminada", "error_sat", "rechazada", "vencida"}) {
        const auto n = local(QString::fromLatin1(tipo), DestinoNotificacion::Tipo::Solicitud, sol);
        QCOMPARE(n.destino, (D{D::Tipo::Solicitud, sol}));
        QVERIFY(n.accionesExtra.isEmpty());
    }
    auto n = local(QStringLiteral("descarga_completa"), DestinoNotificacion::Tipo::Solicitud, sol);
    QCOMPARE(n.destino, (D{D::Tipo::Solicitud, sol}));
    QCOMPARE(n.accionesExtra, QList<Accion>{Accion::MostrarEnFinder});
    for (const char* tipo : {"credencial", "efirma_por_vencer"}) {
        n = local(QString::fromLatin1(tipo), DestinoNotificacion::Tipo::Perfil, per);
        QCOMPARE(n.destino, (D{D::Tipo::Perfil, per}));
        QCOMPARE(n.accionesExtra, QList<Accion>{Accion::AbrirPerfiles});
    }
    n = local(QStringLiteral("descarga_completa"), DestinoNotificacion::Tipo::Ninguno, QString());
    QCOMPARE(n.destino, D{});
    QVERIFY(n.accionesExtra.isEmpty());
    QCOMPARE(n.id, QStringLiteral("x"));
    QCOMPARE(n.titulo, QStringLiteral("t"));
    QCOMPARE(n.cuerpo, QStringLiteral("c"));
}

void TestNotificacionesAccionables::abrirSolicitudConAppAbiertaUOculta()
{
    Escenario e;
    e.iniciar();
    const QString id = e.primeraSolicitud();
    QVERIFY(!id.isEmpty());
    AppViewModel* app = e.vms.app();

    // Abierta.
    e.os.simularActivacionNotificacion(FakeOSIntegration::destinoSolicitud(id));
    QVERIFY(e.ventana.isVisible());
    QCOMPARE(app->pagina(), Pagina::Detalle);
    QCOMPARE(app->solicitudSeleccionadaId(), id);

    // Oculta (ventana cerrada al menu bar).
    app->mostrarLista();
    e.ventana.close();
    QVERIFY(!e.ventana.isVisible());
    e.os.simularActivacionNotificacion(FakeOSIntegration::destinoSolicitud(id));
    QVERIFY(e.ventana.isVisible());
    QCOMPARE(app->pagina(), Pagina::Detalle);
    QCOMPARE(e.salidas, 0);
    QCOMPARE(e.finder.carpetas.size(), 0);
}

void TestNotificacionesAccionables::solicitudInexistenteAbreLaLista()
{
    Escenario e;
    e.iniciar();
    const QString id = SolicitudId::generar().texto();
    e.os.simularActivacionNotificacion(FakeOSIntegration::destinoSolicitud(id));
    QVERIFY(e.ventana.isVisible());
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Lista);
    QVERIFY(e.vms.app()->mensajeFinder().isEmpty());
}

void TestNotificacionesAccionables::perfilSeleccionadoEnPerfiles()
{
    Escenario e(OSIntegration::LaunchContext::LoginItem);
    e.iniciar();
    QVERIFY(!e.ventana.isVisible());
    const QString perfil = e.perfiles.last().id.texto();
    e.os.simularActivacionNotificacion(FakeOSIntegration::destinoPerfil(perfil), Accion::AbrirPerfiles);
    QVERIFY(e.ventana.isVisible());
    QCOMPARE(e.vms.app()->pagina(), Pagina::Perfiles);
    QTRY_COMPARE(e.vms.perfiles()->perfilId(), perfil);
    QCOMPARE(e.vms.perfiles()->modo(), PerfilesSatViewModel::Modo::Edicion);

    // Pulsar el cuerpo (Abrir) de una notificacion de e.firma: igual.
    const QString otro = e.perfiles.first().id.texto();
    e.os.simularActivacionNotificacion(FakeOSIntegration::destinoPerfil(otro), Accion::Abrir);
    QTRY_COMPARE(e.vms.perfiles()->perfilId(), otro);

    // Perfil inexistente: Perfiles sin seleccion y sin error.
    e.os.simularActivacionNotificacion(FakeOSIntegration::destinoPerfil(PerfilId::generar().texto()),
                                       Accion::AbrirPerfiles);
    QCOMPARE(e.vms.app()->pagina(), Pagina::Perfiles);
    QTest::qWait(50);
    QVERIFY(e.vms.perfiles()->errorMessage().isEmpty());
}

void TestNotificacionesAccionables::mostrarEnFinderSoloTraeVentanaConAviso()
{
    Escenario e(OSIntegration::LaunchContext::LoginItem);
    e.iniciar();
    const QString id = e.primeraSolicitud();

    // Mostrado: Finder pasa al frente; la ventana no se muestra.
    e.os.simularActivacionNotificacion(FakeOSIntegration::destinoSolicitud(id), Accion::MostrarEnFinder);
    QTRY_COMPARE(e.finder.carpetas, QStringList{id});
    QTest::qWait(20);
    QVERIFY(!e.ventana.isVisible());

    // No encontrada: aviso D7 visible y ventana al frente (como T009.1).
    e.finder.estado = ResultadoAccionFinder::Estado::NoEncontrado;
    e.os.simularActivacionNotificacion(FakeOSIntegration::destinoSolicitud(id), Accion::MostrarEnFinder);
    QTRY_VERIFY(e.ventana.isVisible());
    QCOMPARE(e.finder.carpetas.size(), 2);
    QCOMPARE(e.vms.app()->pagina(), Pagina::Detalle);
}

void TestNotificacionesAccionables::destinoInvalidoSoloMuestraVentana()
{
    Escenario e(OSIntegration::LaunchContext::LoginItem);
    e.iniciar();
    QSignalSpy rutas(e.controlador.get(), &AppLifecycleController::notificacionEnrutada);
    const Pagina antes = e.vms.app()->pagina();
    e.os.simularActivacionNotificacion(OSIntegration::DestinoNotificacion{});
    QVERIFY(e.ventana.isVisible());
    QCOMPARE(e.vms.app()->pagina(), antes);
    QCOMPARE(rutas.count(), 1);
    QCOMPARE(rutas.at(0).at(0).value<RutaNotificacion>(), RutaNotificacion{});
}

void TestNotificacionesAccionables::arranquePorNotificacionSeEncola()
{
    // La app arranca por la notificacion (sin ventana, como un LoginItem): la
    // respuesta llega antes de inicializar(); el adaptador la encola y la
    // entrega en cola despues; el controlador abre el detalle.
    Escenario e(OSIntegration::LaunchContext::LoginItem);
    const QString id = e.primeraSolicitud();
    e.os.simularActivacionNotificacion(FakeOSIntegration::destinoSolicitud(id));
    QCOMPARE(e.os.activacionesEncoladas.size(), 1);
    QSignalSpy rutas(e.controlador.get(), &AppLifecycleController::notificacionEnrutada);
    e.iniciar();
    QVERIFY(!e.ventana.isVisible()); // entrega diferida
    QTRY_COMPARE(rutas.count(), 1);
    QVERIFY(e.ventana.isVisible());
    QCOMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QCOMPARE(e.vms.app()->solicitudSeleccionadaId(), id);

    // Cola propia del controlador: una activacion emitida antes de que
    // iniciar() termine (p. ej. sincronica dentro de inicializar()) se entrega
    // al final de iniciar().
    Escenario s(OSIntegration::LaunchContext::LoginItem);
    QSignalSpy rutasS(s.controlador.get(), &AppLifecycleController::notificacionEnrutada);
    s.controlador->activarNotificacion(FakeOSIntegration::destinoSolicitud(id), Accion::Abrir);
    QCOMPARE(rutasS.count(), 0);
    QVERIFY(!s.ventana.isVisible());
    s.iniciar();
    QCOMPARE(rutasS.count(), 1);
    QVERIFY(s.ventana.isVisible());
    QCOMPARE(s.vms.app()->pagina(), Pagina::Detalle);
}

void TestNotificacionesAccionables::durantesLaSalidaSeIgnora()
{
    Escenario e(OSIntegration::LaunchContext::LoginItem);
    QSignalSpy rutas(e.controlador.get(), &AppLifecycleController::notificacionEnrutada);
    e.iniciar();
    const QString id = e.primeraSolicitud();
    e.controlador->solicitarSalida();
    QVERIFY(e.controlador->saliendo());
    // Durante la salida (antes de prepararSalida): el controlador la descarta.
    e.os.simularActivacionNotificacion(FakeOSIntegration::destinoSolicitud(id), Accion::Abrir);
    e.os.simularActivacionNotificacion(FakeOSIntegration::destinoSolicitud(id), Accion::MostrarEnFinder);
    QTRY_COMPARE(e.config.pendientes(), 1);
    e.config.confirmarSiguiente(); // ultimo cierre
    QTRY_COMPARE(e.salidas, 1);
    QVERIFY(e.os.salidaPreparada);
    QCOMPARE(rutas.count(), 0);
    QVERIFY(!e.ventana.isVisible());
    QCOMPARE(e.finder.carpetas.size(), 0);
}

void TestNotificacionesAccionables::notificacionesDelRootSonAccionables_data()
{
    QTest::addColumn<OSIntegration::NotificationStatus>("permiso");
    QTest::newRow("permiso concedido") << OSIntegration::NotificationStatus::Granted;
    QTest::newRow("permiso denegado") << OSIntegration::NotificationStatus::Denied;
}

void TestNotificacionesAccionables::notificacionesDelRootSonAccionables()
{
    QFETCH(OSIntegration::NotificationStatus, permiso);
    Flujo f(permiso);
    QVERIFY(f.ok);
    const auto perfil = f.crearPerfil();
    QVERIFY(perfil);
    QVERIFY(f.importar(*perfil));
    const auto id = f.crearSolicitud(*perfil, 1);
    QVERIFY(id);
    f.iniciar();

    // Flujo real hasta Descargado (2 paquetes).
    f.root->worker().enviar(*id);
    QTRY_COMPARE(f.detalle(*id)->resumen.estadoLocal, EstadoLocal::Enviada);
    f.reloj.fijar(f.reloj.ahora().addSecs(11 * 60));
    f.root->worker().ejecutarCiclo();
    QTRY_COMPARE(f.detalle(*id)->paquetes.size(), 2);
    QTRY_VERIFY([&] {
        f.root->worker().ejecutarCiclo();
        const auto d = f.detalle(*id);
        return d && d->paquetes.size() == 2 && d->paquetes.at(0).estadoDescarga == EstadoDescarga::Descargado
               && d->paquetes.at(1).estadoDescarga == EstadoDescarga::Descargado;
    }());

    // Destino tipado (UUID interno, sin RFC) y acciones extra por tipo.
    using D = OSIntegration::DestinoNotificacion;
    QTRY_VERIFY(!f.os.notificacionesPedidas.isEmpty());
    bool vioDescarga = false;
    for (const auto& n : f.os.notificacionesPedidas) {
        QCOMPARE(n.destino, (D{D::Tipo::Solicitud, id->texto()}));
        if (n.tipo == QStringLiteral("descarga_completa")) {
            vioDescarga = true;
            QCOMPARE(n.accionesExtra, QList<Accion>{Accion::MostrarEnFinder});
        } else {
            QVERIFY(n.accionesExtra.isEmpty());
        }
    }
    QVERIFY(vioDescarga);
    if (permiso == OSIntegration::NotificationStatus::Denied) {
        QVERIFY(f.os.notificacionesEntregadas.isEmpty());
    }

    // Acciones sobre la notificacion: solo lectura (base, reintentos y SAT
    // sin cambios), con permiso concedido o denegado.
    QTRY_VERIFY(!f.root->estadoAgregado().consultando());
    const QString rutaBase = QDir(f.tmp.path()).filePath(QStringLiteral("satcfdi.sqlite3"));
    const QByteArray antes = volcado(rutaBase);
    QVERIFY(!antes.isEmpty());
    const int sat = f.llamadasSat();
    const auto destino = f.os.notificacionesPedidas.constLast().destino;

    const qsizetype peticiones = f.os.peticionesFinder.size();
    f.os.simularActivacionNotificacion(destino, Accion::MostrarEnFinder);
    QTRY_COMPARE(f.os.peticionesFinder.size(), peticiones + 1);
    QCOMPARE(f.os.peticionesFinder.last().metodo, QStringLiteral("abrirCarpetaEnFinder"));
    QVERIFY(QFileInfo(f.os.peticionesFinder.last().ruta).isDir());

    f.os.simularActivacionNotificacion(destino, Accion::Abrir);
    QCOMPARE(f.root->viewModels().app()->pagina(), Pagina::Detalle);
    QCOMPARE(f.root->viewModels().app()->solicitudSeleccionadaId(), id->texto());
    f.os.simularActivacionNotificacion(FakeOSIntegration::destinoPerfil(perfil->texto()), Accion::AbrirPerfiles);
    QCOMPARE(f.root->viewModels().app()->pagina(), Pagina::Perfiles);
    QTRY_COMPARE(f.root->viewModels().perfiles()->perfilId(), perfil->texto());

    QTest::qWait(50);
    QCOMPARE(volcado(rutaBase), antes);
    QCOMPARE(f.llamadasSat(), sat);
    QCOMPARE(f.salidas, 0);
}

void TestNotificacionesAccionables::iconoSigueEstadoAgregado()
{
    using Op = fakes::FakeSatGateway::Op;
    using Paso = fakes::FakeSatGateway::Paso;
    Flujo f;
    QVERIFY(f.ok);
    f.iniciar();
    // Estado inicial (sin perfiles ni solicitudes).
    QCOMPARE(f.os.estadosIcono.value(0), Icono::Normal);
    QTRY_VERIFY(!f.root->estadoAgregado().consultando());
    QCOMPARE(f.os.estadoIcono, std::optional(Icono::Normal));

    // Perfil activo sin e.firma -> Atencion; con e.firma Lista -> Normal.
    const auto perfil = f.crearPerfil();
    QVERIFY(perfil);
    QTRY_COMPARE(f.os.estadoIcono, std::optional(Icono::Atencion));
    QVERIFY(f.importar(*perfil));
    QTRY_COMPARE(f.os.estadoIcono, std::optional(Icono::Normal));

    // Operacion en curso -> Trabajando; al terminar -> Normal.
    const auto id = f.crearSolicitud(*perfil, 1);
    QVERIFY(id);
    f.gateway.programar(Op::Crear, Paso::bloqueado());
    f.root->worker().enviar(*id);
    QVERIFY(f.gateway.esperarBloqueo());
    QTRY_COMPARE(f.os.estadoIcono, std::optional(Icono::Trabajando));
    f.gateway.liberar();
    QTRY_COMPARE(f.detalle(*id)->resumen.estadoLocal, EstadoLocal::Enviada);
    QTRY_COMPARE(f.os.estadoIcono, std::optional(Icono::Normal));

    // Envio rechazado explicitamente (EnvioFallido) -> Atencion.
    const auto rechazada = f.crearSolicitud(*perfil, 2);
    QVERIFY(rechazada);
    f.gateway.programar(Op::Crear, Paso::conCreacion(fakes::FakeSatGateway::rechazo301()));
    f.root->worker().enviar(*rechazada);
    QTRY_COMPARE(f.detalle(*rechazada)->resumen.estadoLocal, EstadoLocal::EnvioFallido);
    QTRY_COMPARE(f.os.estadoIcono, std::optional(Icono::Atencion));
    QVERIFY(esperar(f.root->solicitudes().eliminar(*rechazada)));
    QTRY_COMPARE(f.os.estadoIcono, std::optional(Icono::Normal));

    // Pausa confirmada -> Pausado y sin llamadas al puerto SAT.
    const auto pendiente = f.crearSolicitud(*perfil, 3);
    QVERIFY(pendiente);
    f.os.emitirCambioMonitoreo(true);
    QTRY_COMPARE(f.os.estadoIcono, std::optional(Icono::Pausado));
    const int sat = f.llamadasSat();
    f.reloj.fijar(f.reloj.ahora().addSecs(3600));
    f.programadorWorker.avanzar(std::chrono::hours(1));
    f.root->worker().ejecutarCiclo();
    QTest::qWait(100);
    QCOMPARE(f.llamadasSat(), sat);
    QCOMPARE(f.detalle(*pendiente)->resumen.estadoLocal, EstadoLocal::Creada);
    QCOMPARE(f.os.estadoIcono, std::optional(Icono::Pausado));

    // Atencion manda sobre Pausado.
    QVERIFY(esperar(f.root->credenciales().eliminar(*perfil)));
    QTRY_COMPARE(f.os.estadoIcono, std::optional(Icono::Atencion));

    // El icono solo se refleja al cambiar.
    QVERIFY2(sinDuplicadosConsecutivos(f.os.estadosIcono), "reflejarEstadoIcono repetido");

    // Salida: deja de publicar.
    const qsizetype antes = f.os.estadosIcono.size();
    f.os.emitirSalir();
    QTRY_COMPARE(f.salidas, 1);
    f.root->estadoAgregado().refrescarPerfiles();
    QTest::qWait(50);
    QCOMPARE(f.os.estadosIcono.size(), antes);
}

#include "TestNotificacionesAccionables.moc"

int ejecutarTestNotificacionesAccionables(int argc, char* argv[])
{
    TestNotificacionesAccionables prueba;
    return QTest::qExec(&prueba, argc, argv);
}
