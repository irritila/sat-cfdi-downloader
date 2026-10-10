// T009 (plataforma): flujo completo con el root real, OperacionesSatProductivo
// real, FakeSatGateway (sin red), FilesystemPackageStorage real en un
// directorio temporal, FakeSecretStore (credencial Lista) y FakeOSIntegration:
// crear, enviar, verificar (Terminada, N paquetes), descargar (Descargado) y
// notificaciones (una Terminada y una Descarga completa; permiso denegado sin
// efectos).

#include "app_core/AppBootstrapper.h"
#include "app_core/AccesoFinder.h"
#include "app_core/AppCompositionRoot.h"

#include "application/operaciones/OperacionExecutor.h"
#include "application/operaciones/WorkerLocal.h"
#include "application/profiles/ConsultaPreparacionPerfiles.h"
#include "application/profiles/CredencialesSatService.h"
#include "application/vencimiento/AvisoVencimientoEFirma.h"
#include "application/profiles/PerfilesSatService.h"
#include "application/requests/SolicitudesService.h"
#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"
#include "presentation/viewmodels/SolicitudDetailViewModel.h"

#include "fakes/FakeOSIntegration.h"
#include "fakes/FakeProgramador.h"
#include "fakes/FakeSatGateway.h"
#include "fakes/FakeSecretStore.h"

#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>
#include <optional>

using namespace satcfdi;

namespace {

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

struct Flujo {
    explicit Flujo(OSIntegration::NotificationStatus permiso)
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
        m.satGateway = &gateway; // OperacionesSatProductivo real sobre el fake
        m.reloj = reloj.funcion();
        m.programadorEjecutor = &programadorEjecutor;
        m.programadorWorker = &programadorWorker;
        m.programadorVencimiento = &programadorVencimiento; // T014.3
        root = std::make_unique<AppCompositionRoot>(arranque.valor().rutaBase, secretos, m);
    }

    // Perfil con e.firma Lista (FakeSecretStore) y solicitud de recibidos.
    std::optional<SolicitudId> prepararSolicitud()
    {
        const auto perfil = esperar(root->perfiles().crear(secretos.rfcCertificado, QStringLiteral("Perfil")));
        if (!perfil || !perfil->esExito()) {
            return std::nullopt;
        }
        EntradaEFirma e;
        e.rutaCertificado = QStringLiteral("/ficticio/efirma.cer");
        e.rutaLlavePrivada = QStringLiteral("/ficticio/efirma.key");
        e.contrasena = BufferSecreto::desdeBytes(secretos.contrasenaValida.constData(),
                                                 static_cast<std::size_t>(secretos.contrasenaValida.size()));
        const auto importada = esperar(root->credenciales().importar(perfil->valor().id, std::move(e)));
        if (!importada || !importada->esExito()) {
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

    std::optional<SolicitudDetalle> detalle(const SolicitudId& id)
    {
        const auto d = esperar(root->solicitudes().obtener(id));
        return d && d->esExito() ? std::optional(d->valor()) : std::nullopt;
    }

    int notificaciones(const QString& tipo) const
    {
        int n = 0;
        for (const auto& x : os.notificacionesPedidas) {
            n += x.tipo == tipo ? 1 : 0;
        }
        return n;
    }

    QTemporaryDir tmp;
    bool ok = false;
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone::UTC)};
    fakes::FakeProgramador programadorEjecutor{reloj};
    fakes::FakeProgramador programadorWorker{reloj};
    fakes::FakeProgramador programadorVencimiento{reloj};
    fakes::FakeSecretStore secretos;
    fakes::FakeSatGateway gateway;
    fakes::FakeOSIntegration os;
    int salidas = 0;
    std::unique_ptr<AppCompositionRoot> root;
};

// Volcado SQL de la base (sqlite3 CLI del sistema): para comprobar que una
// accion de solo lectura no cambia nada.
QByteArray volcado(const QString& rutaBase)
{
    QProcess p;
    p.start(QStringLiteral("/usr/bin/sqlite3"), {rutaBase, QStringLiteral(".dump")});
    if (!p.waitForFinished(10000) || p.exitCode() != 0) {
        return {};
    }
    return p.readAllStandardOutput();
}

// Lleva la solicitud hasta Descargado (2 paquetes) por el flujo real.
bool hastaDescargado(Flujo& f, const SolicitudId& id)
{
    f.root->worker().enviar(id);
    if (!QTest::qWaitFor([&] { return f.detalle(id)->resumen.estadoLocal == EstadoLocal::Enviada; }, 5000)) {
        return false;
    }
    f.reloj.fijar(f.reloj.ahora().addSecs(11 * 60));
    f.root->worker().ejecutarCiclo();
    if (!QTest::qWaitFor([&] { return f.detalle(id)->paquetes.size() == 2; }, 5000)) {
        return false;
    }
    return QTest::qWaitFor(
        [&] {
            f.root->worker().ejecutarCiclo();
            const auto d = f.detalle(id);
            if (!d) {
                return false;
            }
            for (const PaqueteResumen& p : d->paquetes) {
                if (p.estadoDescarga != EstadoDescarga::Descargado) {
                    return false;
                }
            }
            return true;
        },
        5000);
}

template <typename T>
std::optional<T> resultadoDe(QFuture<T> f)
{
    return esperar(std::move(f));
}

} // namespace

class TestFlujoSat : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(
            "^(?!Populating font family aliases took|This plugin does not support ).*")));
    }

    void flujoCompleto_data();
    void flujoCompleto();
    void finderDestinosExactosYSinCambios();
    void vencimientoConPermisoDenegado();
};

void TestFlujoSat::flujoCompleto_data()
{
    QTest::addColumn<OSIntegration::NotificationStatus>("permiso");
    QTest::newRow("permiso concedido") << OSIntegration::NotificationStatus::Granted;
    QTest::newRow("permiso denegado") << OSIntegration::NotificationStatus::Denied;
}

void TestFlujoSat::flujoCompleto()
{
    QFETCH(OSIntegration::NotificationStatus, permiso);
    Flujo f(permiso);
    QVERIFY(f.ok);
    const auto id = f.prepararSolicitud();
    QVERIFY(id);
    // Creada y persistida antes de cualquier llamada SAT.
    QCOMPARE(f.detalle(*id)->resumen.estadoLocal, EstadoLocal::Creada);
    QCOMPARE(f.gateway.llamadas(fakes::FakeSatGateway::Op::Crear), 0);

    QVERIFY(f.root->cargar());
    f.root->iniciarCicloDeVida(f.os, nullptr, [&f] { ++f.salidas; });

    // Enviar (via ejecutor): Enviada con IdSolicitud SAT.
    f.root->worker().enviar(*id);
    QTRY_COMPARE(f.detalle(*id)->resumen.estadoLocal, EstadoLocal::Enviada);
    QCOMPARE(f.gateway.llamadas(fakes::FakeSatGateway::Op::Crear), 1);
    QVERIFY(f.detalle(*id)->idSolicitudSat.has_value());

    // Verificacion debida: Terminada con 2 paquetes; descargas automaticas.
    f.reloj.fijar(f.reloj.ahora().addSecs(11 * 60));
    f.root->worker().ejecutarCiclo();
    QTRY_COMPARE(f.detalle(*id)->resumen.estadoSat, std::optional(EstadoSolicitudSat::Terminada));
    QTRY_COMPARE(f.detalle(*id)->paquetes.size(), 2);
    for (int i = 0; i < 4; ++i) {
        f.root->worker().ejecutarCiclo();
        QTest::qWait(0);
    }
    QTRY_VERIFY([&] {
        const auto d = f.detalle(*id);
        if (!d) {
            return false;
        }
        for (const PaqueteResumen& p : d->paquetes) {
            if (p.estadoDescarga != EstadoDescarga::Descargado) {
                return false;
            }
        }
        return true;
    }());

    // ZIP finales en la raiz real (<data-dir>/paquetes), fuera de SQLite.
    int zips = 0;
    QDirIterator it(f.root->raizPaquetes(), {QStringLiteral("*.zip")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++zips;
    }
    QCOMPARE(zips, 2);

    // Verificar de nuevo una solicitud ya Terminada no repite la notificacion.
    f.root->worker().verificarAhora(*id);
    f.root->worker().ejecutarCiclo();
    QTRY_COMPARE(f.notificaciones(QStringLiteral("descarga_completa")), 1);
    QCOMPARE(f.notificaciones(QStringLiteral("terminada")), 1);
    for (const auto& n : f.os.notificacionesPedidas) {
        QVERIFY(!n.titulo.contains(f.secretos.rfcCertificado) && !n.cuerpo.contains(f.secretos.rfcCertificado));
    }

    if (permiso == OSIntegration::NotificationStatus::Granted) {
        QCOMPARE(f.os.notificacionesEntregadas.size(), f.os.notificacionesPedidas.size());
    } else {
        // Permiso denegado: nada se entrega y el flujo es identico.
        QVERIFY(f.os.notificacionesEntregadas.isEmpty());
    }
    QCOMPARE(f.detalle(*id)->resumen.estadoLocal, EstadoLocal::Enviada);
}

void TestFlujoSat::finderDestinosExactosYSinCambios()
{
    Flujo f(OSIntegration::NotificationStatus::Granted);
    QVERIFY(f.ok);
    const auto id = f.prepararSolicitud();
    QVERIFY(id);
    QVERIFY(f.root->cargar());
    f.root->iniciarCicloDeVida(f.os, nullptr, [&f] { ++f.salidas; });
    QVERIFY(hastaDescargado(f, *id));

    const auto d = f.detalle(*id);
    const QString idSat = d->paquetes.first().idPaqueteSat;
    QString zip;
    QDirIterator it(f.root->raizPaquetes(), {QStringLiteral("*.zip")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString ruta = it.next();
        if (QFileInfo(ruta).fileName().startsWith(idSat)) {
            zip = ruta;
        }
    }
    QVERIFY2(!zip.isEmpty(), qPrintable(idSat));
    const QString rutaBase = QDir(f.tmp.path()).filePath(QStringLiteral("satcfdi.sqlite3"));
    const QByteArray antes = volcado(rutaBase);
    QVERIFY(!antes.isEmpty());

    // Destinos exactos pedidos al SO: el ZIP, su carpeta y la raiz.
    AccesoFinder& finder = f.root->accesoFinder();
    auto r = resultadoDe(finder.mostrarPaquete(*id, idSat));
    QVERIFY(r && r->estado == ResultadoAccionFinder::Estado::Mostrado);
    QCOMPARE(f.os.peticionesFinder.last().metodo, QStringLiteral("mostrarEnFinder"));
    QCOMPARE(QFileInfo(f.os.peticionesFinder.last().ruta).canonicalFilePath(), QFileInfo(zip).canonicalFilePath());
    r = resultadoDe(finder.abrirCarpetaSolicitud(*id));
    QVERIFY(r && r->estado == ResultadoAccionFinder::Estado::Mostrado);
    QCOMPARE(f.os.peticionesFinder.last().metodo, QStringLiteral("abrirCarpetaEnFinder"));
    QCOMPARE(QFileInfo(f.os.peticionesFinder.last().ruta).canonicalFilePath(),
             QFileInfo(QFileInfo(zip).absolutePath()).canonicalFilePath());
    r = resultadoDe(finder.abrirCarpetaPaquetes());
    QVERIFY(r && r->estado == ResultadoAccionFinder::Estado::Mostrado);
    QCOMPARE(QFileInfo(f.os.peticionesFinder.last().ruta).canonicalFilePath(),
             QFileInfo(f.root->raizPaquetes()).canonicalFilePath());

    // Finder fallido -> "No se pudo abrir Finder".
    f.os.resultadoFinder = OSIntegration::ResultadoFinder::Fallido;
    r = resultadoDe(finder.abrirCarpetaPaquetes());
    QVERIFY(r && r->estado == ResultadoAccionFinder::Estado::Fallido);
    QCOMPARE(r->mensaje, QStringLiteral("No se pudo abrir Finder"));
    f.os.resultadoFinder = OSIntegration::ResultadoFinder::Mostrado;

    // ZIP borrado antes del clic (detalle real): no se llama al SO, mensaje D7
    // y la existencia se refresca a NoEncontrado.
    SolicitudDetailViewModel* vm = f.root->viewModels().detalle();
    vm->cargar(id->texto());
    QTRY_VERIFY(vm->cargada());
    QTRY_COMPARE(vm->paquetes().first().toMap().value(QStringLiteral("existencia")).toString(),
                 QStringLiteral("Presente"));
    QVERIFY(QFile::remove(zip));
    const qsizetype peticiones = f.os.peticionesFinder.size();
    vm->mostrarEnFinder(idSat);
    QTRY_COMPARE(vm->mensajeFinder(), QStringLiteral("Archivo local no encontrado"));
    QCOMPARE(f.os.peticionesFinder.size(), peticiones);
    QTRY_COMPARE(vm->paquetes().first().toMap().value(QStringLiteral("existencia")).toString(),
                 QStringLiteral("NoEncontrado"));
    QCOMPARE(vm->paquetes().first().toMap().value(QStringLiteral("estadoDescarga")).toString(),
             QStringLiteral("Descargado"));

    // Menu bar: la intencion abre la raiz; si Finder falla, el aviso D7 va a
    // la lista de la ventana (sin crear nada).
    const qsizetype antesMenu = f.os.peticionesFinder.size();
    f.os.emitirAbrirCarpetaPaquetes();
    QTRY_COMPARE(f.os.peticionesFinder.size(), antesMenu + 1);
    QCOMPARE(QFileInfo(f.os.peticionesFinder.last().ruta).canonicalFilePath(),
             QFileInfo(f.root->raizPaquetes()).canonicalFilePath());
    f.os.resultadoFinder = OSIntegration::ResultadoFinder::Fallido;
    f.os.emitirAbrirCarpetaPaquetes();
    QTRY_COMPARE(f.root->viewModels().app()->mensajeFinder(), QStringLiteral("No se pudo abrir Finder"));
    QCOMPARE(f.root->viewModels().app()->pagina(), AppViewModel::Pagina::Lista);

    // Solo lectura: la base no cambio.
    QCOMPARE(volcado(rutaBase), antes);
}

// T014.3 D2/D3 con el root real y FakeOSIntegration con permiso DENEGADO: a 30
// dias de vencer se intenta notificar (resultado PermissionDenied), el perfil
// sigue trayendo diasParaVencer y nada mas cambia (ni reintentos, ni estados,
// ni logs; solo el dedupe, igual que con permiso).
void TestFlujoSat::vencimientoConPermisoDenegado()
{
    Flujo f(OSIntegration::NotificationStatus::Denied);
    QVERIFY(f.ok);
    // El servicio de credenciales usa el reloj del sistema: el reloj
    // controlado parte de "ahora" y la e.firma vence en 30 dias (+1 h).
    const QDateTime ahora = QDateTime::currentDateTimeUtc();
    f.reloj.fijar(ahora);
    f.secretos.vigenteHasta = ahora.addDays(30).addSecs(3600);
    const auto id = f.prepararSolicitud();
    QVERIFY(id);

    QSignalSpy resultados(&f.os, &OSIntegration::notificacionTerminada);
    QSignalSpy evaluaciones(&f.root->avisoVencimiento(), &AvisoVencimientoEFirma::evaluacionTerminada);
    const QString rutaBase = QDir(f.tmp.path()).filePath(QStringLiteral("satcfdi.sqlite3"));
    QVERIFY(f.root->cargar());
    f.root->iniciarCicloDeVida(f.os, nullptr, [&f] { ++f.salidas; });
    QTRY_COMPARE(evaluaciones.count(), 1);
    QTRY_COMPARE(resultados.count(), 1);

    // notificar() se intento con el tipo e id del aviso y el SO respondio
    // PermissionDenied: nada se entrega.
    QCOMPARE(f.notificaciones(QStringLiteral("efirma_por_vencer")), 1);
    QVERIFY(f.os.notificacionesPedidas.constLast().id.startsWith(QStringLiteral("vencimiento:")));
    QVERIFY(f.os.notificacionesPedidas.constLast().id.endsWith(QStringLiteral(":30")));
    QCOMPARE(resultados.at(0).at(1).value<OSIntegration::NotificationSendResult>(),
             OSIntegration::NotificationSendResult::PermissionDenied);
    QVERIFY(f.os.notificacionesEntregadas.isEmpty());

    // El perfil sigue trayendo diasParaVencer (badge) con el mismo reloj.
    ConsultaPreparacionPerfiles consulta(f.root->perfiles(), f.root->credenciales(), nullptr, f.reloj.funcion());
    const auto perfiles = esperar(consulta.listarVerificados());
    QVERIFY(perfiles && perfiles->esExito());
    QCOMPARE(perfiles->valor().size(), 1);
    QCOMPARE(perfiles->valor().constFirst().preparacion, PreparacionPerfil::Lista);
    QCOMPARE(perfiles->valor().constFirst().diasParaVencer, std::optional<int>(30));

    // Nada mas cambia: la revision diaria y un cambio de credencial vuelven a
    // evaluar sin reintentar la notificacion ni tocar la base.
    const QByteArray antes = volcado(rutaBase);
    QVERIFY(!antes.isEmpty());
    QVERIFY(antes.contains("INSERT INTO aviso_vencimiento_efirma"));
    f.programadorVencimiento.avanzar(std::chrono::hours(24));
    QTRY_COMPARE(evaluaciones.count(), 2);
    f.root->avisoVencimiento().alCambiarCredencial();
    QTRY_COMPARE(evaluaciones.count(), 3);
    QCOMPARE(f.notificaciones(QStringLiteral("efirma_por_vencer")), 1);
    QCOMPARE(resultados.count(), 1);
    QCOMPARE(volcado(rutaBase), antes);
    QCOMPARE(f.detalle(*id)->resumen.estadoLocal, EstadoLocal::Creada);
    QCOMPARE(f.gateway.llamadas(fakes::FakeSatGateway::Op::Crear), 0);
}

#include "TestFlujoSat.moc"

int ejecutarTestFlujoSat(int argc, char* argv[])
{
    TestFlujoSat prueba;
    return QTest::qExec(&prueba, argc, argv);
}
