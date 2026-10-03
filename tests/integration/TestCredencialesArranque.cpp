#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"

#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/PerfilesSatService.h"
#include "application/requests/SolicitudesService.h"

#include "fakes/FakeSecretStore.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <optional>

using namespace satcfdi;
using fakes::FakeSecretStore;

namespace {

const QString kRfc = QStringLiteral("AAA010101AAA");

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

std::unique_ptr<AppCompositionRoot> abrir(const QString& directorio, SecretStore& store)
{
    AppBootstrapper::Opciones opciones;
    opciones.directorioDatos = directorio;
    const auto arranque = AppBootstrapper(opciones).preparar();
    if (!arranque) {
        return nullptr;
    }
    return std::make_unique<AppCompositionRoot>(arranque.valor().rutaBase, store);
}

EntradaEFirma entradaCon(const QByteArray& contrasena)
{
    EntradaEFirma e;
    // El fake no lee archivos; solo exige rutas no vacias.
    e.rutaCertificado = QStringLiteral("/ficticio/efirma.cer");
    e.rutaLlavePrivada = QStringLiteral("/ficticio/efirma.key");
    e.contrasena = BufferSecreto::desdeBytes(contrasena.constData(),
                                             static_cast<std::size_t>(contrasena.size()));
    return e;
}

// Crea perfil + credencial en un grafo nuevo y lo destruye. Devuelve el perfil.
std::optional<PerfilId> perfilConCredencial(const QString& dir, FakeSecretStore& store)
{
    auto root = abrir(dir, store);
    if (!root) {
        return std::nullopt;
    }
    const auto perfil = esperar(root->perfiles().crearPerfilSimulado({kRfc, QStringLiteral("Perfil"), true}));
    if (!perfil || !perfil->esExito()) {
        return std::nullopt;
    }
    const auto importada =
        esperar(root->credenciales().importar(perfil->valor(), entradaCon(store.contrasenaValida)));
    if (!importada || !importada->esExito()) {
        return std::nullopt;
    }
    return perfil->valor();
}

} // namespace

class TestCredencialesArranque : public QObject {
    Q_OBJECT

private slots:
    void init();

    void arranqueReconciliaResiduosSinTocarLaVigente();
    void errorDeReconciliacionNoBorraYLaAppArranca();
    void importarReiniciarYObtenerEstadoLista();
    void contrasenaNoLlegaASqlite();
};

void TestCredencialesArranque::init()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral(
        "^(?!Populating font family aliases took|This plugin does not support ).*")));
}

void TestCredencialesArranque::arranqueReconciliaResiduosSinTocarLaVigente()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    FakeSecretStore store;
    QVERIFY(perfilConCredencial(tmp.path(), store));
    QCOMPARE(store.generacionesVivas(), 1);
    const QStringList vigente = store.uuidsVivos();

    // Residuo de un fallo previo (generacion sin fila en SQLite).
    const CredencialRef residuo = store.sembrar();
    QCOMPARE(store.generacionesVivas(), 2);

    auto root = abrir(tmp.path(), store);
    QVERIFY(root);
    const auto r = esperar(root->reconciliacionInicial());
    QVERIFY(r && r->esExito());
    QCOMPARE(r->valor().generacionesEliminadas, 1);
    QCOMPARE(r->valor().generacionesConservadas, 1);
    QVERIFY(!store.existe(residuo));
    QCOMPARE(store.uuidsVivos(), vigente);
}

void TestCredencialesArranque::errorDeReconciliacionNoBorraYLaAppArranca()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    FakeSecretStore store;
    QVERIFY(perfilConCredencial(tmp.path(), store));
    const CredencialRef residuo = store.sembrar();
    store.fallos.insert(FakeSecretStore::Operacion::Reconciliar,
                        ErrorSecretStore::Categoria::AlmacenNoDisponible);

    // El servicio registra solo operacion + categoria.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^credencial\\.reconciliar: ")));
    auto root = abrir(tmp.path(), store);
    QVERIFY(root);
    const auto r = esperar(root->reconciliacionInicial());
    QVERIFY(r && !r->esExito());
    QCOMPARE(store.generacionesVivas(), 2); // nada borrado
    QVERIFY(store.existe(residuo));

    // La app arranca y sigue operando.
    QVERIFY(root->cargar());
    const auto lista = esperar(root->solicitudes().listar());
    QVERIFY(lista && lista->esExito());
}

void TestCredencialesArranque::importarReiniciarYObtenerEstadoLista()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    FakeSecretStore store;
    const auto perfil = perfilConCredencial(tmp.path(), store);
    QVERIFY(perfil);

    auto root = abrir(tmp.path(), store);
    QVERIFY(root);
    const auto estado = esperar(root->credenciales().obtenerEstado(*perfil));
    QVERIFY(estado && estado->esExito());
    QCOMPARE(estado->valor(), EstadoCredencial::Lista);
    const auto r = esperar(root->reconciliacionInicial());
    QVERIFY(r && r->esExito());
    QCOMPARE(r->valor().generacionesEliminadas, 0);
}

void TestCredencialesArranque::contrasenaNoLlegaASqlite()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    FakeSecretStore store;
    store.contrasenaValida = QByteArrayLiteral("Centinela-T005-c0ntr4s3n4-9f3a");

    auto root = abrir(tmp.path(), store);
    QVERIFY(root);
    const auto perfil = esperar(root->perfiles().crearPerfilSimulado({kRfc, QStringLiteral("Perfil"), true}));
    QVERIFY(perfil && perfil->esExito());
    const auto importada =
        esperar(root->credenciales().importar(perfil->valor(), entradaCon(store.contrasenaValida)));
    QVERIFY(importada && importada->esExito());

    // Con la base abierta (WAL vivo) y despues de cerrar.
    const QStringList archivos{QStringLiteral("satcfdi.sqlite3"), QStringLiteral("satcfdi.sqlite3-wal"),
                               QStringLiteral("satcfdi.sqlite3-shm")};
    auto revisar = [&](const char* momento) {
        for (const QString& nombre : archivos) {
            QFile f(QDir(tmp.path()).filePath(nombre));
            if (!f.exists()) {
                continue; // tras cerrar, -wal/-shm pueden no existir
            }
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray bytes = f.readAll();
            QVERIFY2(!bytes.contains(store.contrasenaValida),
                     qPrintable(nombre + QStringLiteral(" (") + QLatin1String(momento) + QLatin1Char(')')));
            QVERIFY2(!bytes.contains(store.contrasenaValida.toBase64()), qPrintable(nombre));
        }
    };
    QVERIFY(QFile::exists(QDir(tmp.path()).filePath(archivos.at(0))));
    revisar("abierta");
    if (QTest::currentTestFailed()) {
        return;
    }
    root.reset();
    revisar("cerrada");
}

#include "TestCredencialesArranque.moc"

int ejecutarTestCredencialesArranque(int argc, char* argv[])
{
    TestCredencialesArranque prueba;
    return QTest::qExec(&prueba, argc, argv);
}
