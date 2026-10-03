#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"

#include "application/profiles/PerfilesSatService.h"
#include "application/requests/DemoSolicitudesService.h"
#include "application/requests/SolicitudesService.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"
#include "presentation/viewmodels/PresentacionViewModels.h"
#include "presentation/viewmodels/SolicitudesListModel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <memory>
#include <optional>

using namespace satcfdi;

namespace {

// Espera el future procesando eventos (las continuaciones corren en el hilo
// grafico); nunca waitForFinished().
template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

QByteArray hashArchivo(const QString& ruta)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return h.result();
}

std::unique_ptr<AppCompositionRoot> abrir(const QString& directorio)
{
    AppBootstrapper::Opciones opciones;
    opciones.directorioDatos = directorio;
    const auto arranque = AppBootstrapper(opciones).preparar();
    if (!arranque) {
        qWarning("bootstrap fallo: %s", qUtf8Printable(arranque.error().mensaje));
        return nullptr;
    }
    return std::make_unique<AppCompositionRoot>(arranque.valor().rutaBase);
}

NuevaSolicitudRequest solicitudDe(const PerfilId& perfil)
{
    NuevaSolicitudRequest r;
    r.perfilId = perfil;
    r.tipoDescarga = TipoDescarga::Recibidos;
    r.fechaInicial = QDate(2026, 9, 1);
    r.fechaFinal = QDate(2026, 9, 30);
    r.rfcContraparte = QStringLiteral("XAXX010101000");
    r.tipoComprobante = QStringLiteral("I");
    return r;
}

} // namespace

class TestComposicionPersistida : public QObject {
    Q_OBJECT

private slots:
    void init();

    void argumentosDataDir();
    void directorioPorDefectoUsaNombresDeApp();
    void bootstrapSqlFueraDelHiloPrincipal();
    void bootstrapCreaDirectorioYMigraUnaVez();
    void reinicioConservaDatos();
    void eliminarYReiniciarDaNoEncontrado();
    void grafoPersistidoSinDemosNiCarpetaZip();
    void bootstrapRechazaVersionFutura();
};

void TestComposicionPersistida::init()
{
    // Unica excepcion: aviso de rendimiento de fuentes de la plataforma
    // offscreen, ajeno a la app (igual que satcfdi_presentation_tests).
    QTest::failOnWarning(QRegularExpression(
        QStringLiteral("^(?!Populating font family aliases took).*")));
}

void TestComposicionPersistida::argumentosDataDir()
{
    const QString app = QStringLiteral("satcfdi_app");

    auto r = AppBootstrapper::opcionesDesdeArgumentos({app});
    QVERIFY(r);
    QVERIFY(r.valor().directorioDatos.isEmpty());

    r = AppBootstrapper::opcionesDesdeArgumentos({app, QStringLiteral("--data-dir"),
                                                  QStringLiteral("/tmp/x")});
    QVERIFY(r);
    QCOMPARE(r.valor().directorioDatos, QStringLiteral("/tmp/x"));

    r = AppBootstrapper::opcionesDesdeArgumentos({app, QStringLiteral("--data-dir=/tmp/y")});
    QVERIFY(r);
    QCOMPARE(r.valor().directorioDatos, QStringLiteral("/tmp/y"));

    r = AppBootstrapper::opcionesDesdeArgumentos({app, QStringLiteral("--data-dir")});
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorArranque::Tipo::ArgumentoInvalido);

    r = AppBootstrapper::opcionesDesdeArgumentos({app, QStringLiteral("--data-dir=")});
    QVERIFY(!r);
}

void TestComposicionPersistida::directorioPorDefectoUsaNombresDeApp()
{
    // main de la prueba llama configurarIdentidadAplicacion(), igual que
    // main.cpp. Solo se calcula la ruta, no se crea.
    QCOMPARE(QCoreApplication::organizationName(), QStringLiteral("Adenium"));
    QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("SAT CFDI Downloader"));
    const QString dir = AppBootstrapper::directorioDatosPorDefecto();
    QVERIFY2(dir.endsWith(QStringLiteral("/Adenium/SAT CFDI Downloader")), qPrintable(dir));
}

void TestComposicionPersistida::bootstrapSqlFueraDelHiloPrincipal()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Envuelve la inicializacion real y registra el hilo donde corre.
    QThread* hiloInicializacion = nullptr;
    bool hiloSeguiaCorriendo = false;
    AppBootstrapper::Opciones opciones;
    opciones.directorioDatos = tmp.path();
    opciones.inicializador = [&](const QString& rutaBase) {
        hiloInicializacion = QThread::currentThread();
        hiloSeguiaCorriendo = hiloInicializacion->isRunning();
        return inicializarBaseSqlite(rutaBase);
    };

    const auto r = AppBootstrapper(opciones).preparar();
    QVERIFY(r);
    QVERIFY(hiloInicializacion != nullptr);
    QVERIFY(hiloSeguiaCorriendo);
    QVERIFY(hiloInicializacion != QCoreApplication::instance()->thread());
    QVERIFY(hiloInicializacion != QThread::currentThread());
    QCOMPARE(r.valor().informe.migracion.aplicadas, QList<int>{1});
}

void TestComposicionPersistida::bootstrapCreaDirectorioYMigraUnaVez()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    AppBootstrapper::Opciones opciones;
    opciones.directorioDatos = tmp.filePath(QStringLiteral("a/b"));

    const auto primero = AppBootstrapper(opciones).preparar();
    QVERIFY(primero);
    QVERIFY(QFile::exists(tmp.filePath(QStringLiteral("a/b/satcfdi.sqlite3"))));
    QCOMPARE(primero.valor().informe.migracion.aplicadas, QList<int>{1});
    QCOMPARE(primero.valor().informe.journalMode, QStringLiteral("wal"));

    const auto segundo = AppBootstrapper(opciones).preparar();
    QVERIFY(segundo);
    QVERIFY(segundo.valor().informe.migracion.aplicadas.isEmpty());
    QCOMPARE(segundo.valor().informe.migracion.versionFinal, 1);
}

void TestComposicionPersistida::reinicioConservaDatos()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    PerfilId perfilId;
    SolicitudId solicitudId;
    SolicitudDetalle antes;
    {
        auto root = abrir(tmp.path());
        QVERIFY(root);
        const auto perfil = esperar(root->perfiles().crearPerfilSimulado(
            {QStringLiteral("EKU9003173C9"), QStringLiteral("Perfil de prueba"), true}));
        QVERIFY(perfil && perfil->esExito());
        perfilId = perfil->valor();

        const auto creada = esperar(root->solicitudes().crear(solicitudDe(perfilId)));
        QVERIFY(creada && creada->esExito());
        solicitudId = creada->valor();

        const auto detalle = esperar(root->solicitudes().obtener(solicitudId));
        QVERIFY(detalle && detalle->esExito());
        antes = detalle->valor();
    } // destruye el grafo completo (cierra conexion y dispatcher)

    auto root = abrir(tmp.path());
    QVERIFY(root);

    const auto perfiles = esperar(root->perfiles().listarActivos());
    QVERIFY(perfiles && perfiles->esExito());
    QCOMPARE(perfiles->valor().size(), 1);
    const PerfilResumen& p = perfiles->valor().constFirst();
    QCOMPARE(p.id, perfilId);
    QCOMPARE(p.rfc, QStringLiteral("EKU9003173C9"));
    QCOMPARE(p.razonSocial, QStringLiteral("Perfil de prueba"));
    QVERIFY(p.activo);

    const auto lista = esperar(root->solicitudes().listar());
    QVERIFY(lista && lista->esExito());
    QCOMPARE(lista->valor().size(), 1);
    QCOMPARE(lista->valor().constFirst().id, solicitudId);

    const auto detalle = esperar(root->solicitudes().obtener(solicitudId));
    QVERIFY(detalle && detalle->esExito());
    const SolicitudDetalle& d = detalle->valor();
    QCOMPARE(d.resumen.id, antes.resumen.id);
    QCOMPARE(d.resumen.perfilRfc, antes.resumen.perfilRfc);
    QCOMPARE(d.resumen.rfcContraparte, antes.resumen.rfcContraparte);
    QCOMPARE(d.resumen.tipoDescarga, antes.resumen.tipoDescarga);
    QCOMPARE(d.resumen.fechaInicial, antes.resumen.fechaInicial);
    QCOMPARE(d.resumen.fechaFinal, antes.resumen.fechaFinal);
    QCOMPARE(d.resumen.estadoLocal, EstadoLocal::Creada);
    QCOMPARE(d.resumen.estadoSat, antes.resumen.estadoSat);
    QVERIFY(!d.resumen.estadoSat);
    QCOMPARE(d.resumen.creadaEn, antes.resumen.creadaEn);
    QCOMPARE(d.resumen.totalPaquetes, antes.resumen.totalPaquetes);
    QCOMPARE(d.fechaInicialSat, antes.fechaInicialSat);
    QCOMPARE(d.fechaFinalSat, antes.fechaFinalSat);
    QCOMPARE(d.rfcContrapartes, antes.rfcContrapartes);
    QCOMPARE(d.tipoComprobante, antes.tipoComprobante);
    QCOMPARE(d.complemento, antes.complemento);
    QCOMPARE(d.idSolicitudSat, antes.idSolicitudSat);
    QCOMPARE(d.paquetes.size(), antes.paquetes.size());
    QCOMPARE(d.logs.size(), antes.logs.size());
    QVERIFY(!d.logs.isEmpty());
    for (qsizetype i = 0; i < d.logs.size(); ++i) {
        QCOMPARE(d.logs.at(i).tipoEvento, antes.logs.at(i).tipoEvento);
        QCOMPARE(d.logs.at(i).creadoEn, antes.logs.at(i).creadoEn);
    }
}

void TestComposicionPersistida::eliminarYReiniciarDaNoEncontrado()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    SolicitudId id;
    {
        auto root = abrir(tmp.path());
        QVERIFY(root);
        const auto perfil = esperar(root->perfiles().crearPerfilSimulado(
            {QStringLiteral("EKU9003173C9"), QStringLiteral("Perfil"), true}));
        QVERIFY(perfil && perfil->esExito());
        const auto creada = esperar(root->solicitudes().crear(solicitudDe(perfil->valor())));
        QVERIFY(creada && creada->esExito());
        id = creada->valor();

        const auto eliminada = esperar(root->solicitudes().eliminar(id));
        QVERIFY(eliminada && eliminada->esExito());
        QVERIFY(eliminada->valor().cambio);
    }

    auto root = abrir(tmp.path());
    QVERIFY(root);
    const auto detalle = esperar(root->solicitudes().obtener(id));
    QVERIFY(detalle);
    QVERIFY(!detalle->esExito());
    QCOMPARE(detalle->error().tipo, ErrorObtener::Tipo::NoEncontrada);

    const auto lista = esperar(root->solicitudes().listar());
    QVERIFY(lista && lista->esExito());
    QVERIFY(lista->valor().isEmpty());

    const auto otraVez = esperar(root->solicitudes().eliminar(id));
    QVERIFY(otraVez && otraVez->esExito());
    QVERIFY(!otraVez->valor().cambio);
}

void TestComposicionPersistida::grafoPersistidoSinDemosNiCarpetaZip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    {
        auto root = abrir(tmp.path());
        QVERIFY(root);

        QVERIFY(!qobject_cast<DemoSolicitudesService*>(&root->solicitudes()));
        QCOMPARE(root->solicitudes().metaObject()->className(),
                 "satcfdi::SolicitudesServicePersistido");
        QCOMPARE(root->perfiles().metaObject()->className(),
                 "satcfdi::PerfilesSatServicePersistido");

        // Carga el QML real sobre servicios persistidos.
        QVERIFY(root->cargar());
        QCOMPARE(root->engine()->rootObjects().size(), 1);
        // La carga inicial del modelo de lista termina contra la base vacia.
        SolicitudesListModel* modelo = root->viewModels().solicitudes();
        QVERIFY(QTest::qWaitFor(
            [modelo] { return modelo->estado() != SolicitudesListModel::Estado::Cargando; }, 5000));
        QCOMPARE(modelo->estado(), SolicitudesListModel::Estado::Vacia);
        QCOMPARE(modelo->rowCount(), 0);
    }

    // Solo la base (y sus -wal/-shm); ningun directorio (p. ej. paquetes ZIP).
    const QDir dir(tmp.path());
    QVERIFY(dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    const QStringList archivos = dir.entryList(QDir::Files | QDir::Hidden);
    QVERIFY(archivos.contains(QStringLiteral("satcfdi.sqlite3")));
    for (const QString& archivo : archivos) {
        QVERIFY2(archivo.startsWith(QStringLiteral("satcfdi.sqlite3")), qPrintable(archivo));
    }
}

void TestComposicionPersistida::bootstrapRechazaVersionFutura()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Base creada por una "version futura": 001 embebida + 002 desconocida.
    auto embebidas = migracionesSqliteEmbebidas();
    QVERIFY(embebidas);
    QList<MigracionSql> futuras = embebidas.valor();
    futuras.append(MigracionSql{2, QStringLiteral("002_futura"),
                                QStringLiteral("CREATE TABLE tabla_futura (id INTEGER)\n;\n")});
    AppBootstrapper::Opciones conFutura;
    conFutura.directorioDatos = tmp.path();
    conFutura.migraciones = futuras;
    QVERIFY(AppBootstrapper(conFutura).preparar());

    const QString ruta = tmp.filePath(QStringLiteral("satcfdi.sqlite3"));
    const QByteArray hashAntes = hashArchivo(ruta);
    QVERIFY(!hashAntes.isEmpty());

    // Esta version solo conoce las embebidas: debe rechazar sin modificar.
    AppBootstrapper::Opciones actual;
    actual.directorioDatos = tmp.path();
    const auto r = AppBootstrapper(actual).preparar();
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorArranque::Tipo::BaseDatos);
    QVERIFY(r.error().causa);
    QCOMPARE(r.error().causa->tipo, ErrorPersistencia::Tipo::Migracion);
    QVERIFY(!r.error().mensaje.contains(tmp.path()));
    QCOMPARE(hashArchivo(ruta), hashAntes);
}

#include "TestComposicionPersistida.moc"

int ejecutarTestComposicionPersistida(int argc, char* argv[])
{
    TestComposicionPersistida prueba;
    return QTest::qExec(&prueba, argc, argv);
}
