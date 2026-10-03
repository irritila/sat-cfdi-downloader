// T005.1: preparacion de perfiles con el root real (SQLite real en
// QTemporaryDir, CredencialesSatServicePersistido y FakeSecretStore).
// ConsultaPreparacionPerfiles::listarListosParaSolicitudes solo devuelve
// perfiles activos con credencial Lista; centinela de la contrasena.

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"

#include "application/profiles/ConsultaPreparacionPerfiles.h"
#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/PerfilesSatService.h"

#include "fakes/FakeSecretStore.h"

#include <QDir>
#include <QFile>
#include <QMutex>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <optional>

using namespace satcfdi;

namespace {

QMutex gMutex;
QStringList gMensajes;
QtMessageHandler gPrevio = nullptr;

void capturar(QtMsgType tipo, const QMessageLogContext& contexto, const QString& mensaje)
{
    {
        QMutexLocker l(&gMutex);
        gMensajes.append(mensaje);
    }
    if (gPrevio != nullptr) {
        gPrevio(tipo, contexto, mensaje);
    }
}

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

// PerfilesSatService que delega en el real y reporta como INACTIVOS los ids
// indicados. El servicio de T005.1 no expone desactivar un perfil; este
// decorador permite probar la regla activo && Lista con credenciales reales.
class PerfilesConInactivos final : public PerfilesSatService {
public:
    PerfilesConInactivos(PerfilesSatService& real, QSet<QString> inactivos)
        : m_real(real)
        , m_inactivos(std::move(inactivos))
    {
    }

    QFuture<ResultadoLista> listarNoEliminados() override
    {
        return m_real.listarNoEliminados().then(this, [this](const ResultadoLista& r) {
            if (!r) {
                return r;
            }
            QList<PerfilResumen> lista = r.valor();
            for (PerfilResumen& p : lista) {
                if (m_inactivos.contains(p.id.texto())) {
                    p.activo = false;
                }
            }
            return ResultadoLista::exito(std::move(lista));
        });
    }
    QFuture<ResultadoPerfil> obtener(const PerfilId& id) override { return m_real.obtener(id); }
    QFuture<ResultadoCrear> crear(const QString& rfc, const QString& nombre) override
    {
        return m_real.crear(rfc, nombre);
    }
    QFuture<ResultadoActualizar> actualizarNombre(const PerfilId& id, const QString& nombre) override
    {
        return m_real.actualizarNombre(id, nombre);
    }

private:
    PerfilesSatService& m_real;
    QSet<QString> m_inactivos;
};

QStringList rfcs(const QList<PerfilConPreparacion>& lista)
{
    QStringList r;
    for (const PerfilConPreparacion& p : lista) {
        r.append(p.perfil.rfc);
    }
    return r;
}

} // namespace

class TestPreparacionPerfiles : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { gPrevio = qInstallMessageHandler(capturar); }
    void cleanupTestCase() { qInstallMessageHandler(gPrevio); }
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(
            "^(?!Populating font family aliases took|This plugin does not support ).*")));
    }

    void soloPerfilesActivosConCredencialListaSonSeleccionables();
};

void TestPreparacionPerfiles::soloPerfilesActivosConCredencialListaSonSeleccionables()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    fakes::FakeSecretStore store;
    store.contrasenaValida = QByteArrayLiteral("Centinela-T005.1-pr3p4r4c10n-7c2e");
    AppBootstrapper::Opciones opciones;
    opciones.directorioDatos = tmp.path();
    const auto arranque = AppBootstrapper(opciones).preparar();
    QVERIFY(arranque);
    auto root = std::make_unique<AppCompositionRoot>(arranque.valor().rutaBase, store);
    QVERIFY(esperar(root->reconciliacionInicial()));

    // Perfil con e.firma (RFC del certificado del fake) y perfil sin credencial.
    const auto conCredencial = esperar(root->perfiles().crear(store.rfcCertificado, QStringLiteral("Con e.firma")));
    QVERIFY(conCredencial && conCredencial->esExito());
    const auto sinCredencial = esperar(root->perfiles().crear(QStringLiteral("EKU9003173C9"), QStringLiteral("Sin e.firma")));
    QVERIFY(sinCredencial && sinCredencial->esExito());

    EntradaEFirma entrada;
    entrada.rutaCertificado = QStringLiteral("/ficticio/efirma.cer");
    entrada.rutaLlavePrivada = QStringLiteral("/ficticio/efirma.key");
    entrada.contrasena = BufferSecreto::desdeBytes(store.contrasenaValida.constData(),
                                                   static_cast<std::size_t>(store.contrasenaValida.size()));
    const auto importada = esperar(root->credenciales().importar(conCredencial->valor().id, std::move(entrada)));
    QVERIFY(importada && importada->esExito());

    // Activo + Lista: es el unico seleccionable.
    {
        ConsultaPreparacionPerfiles consulta(root->perfiles(), root->credenciales());
        const auto listos = esperar(consulta.listarListosParaSolicitudes());
        QVERIFY(listos && listos->esExito());
        QCOMPARE(rfcs(listos->valor()), QStringList{store.rfcCertificado});
        QCOMPARE(listos->valor().constFirst().preparacion, PreparacionPerfil::Lista);

        const auto todos = esperar(consulta.listarVerificados());
        QVERIFY(todos && todos->esExito());
        QCOMPARE(todos->valor().size(), 2);
        for (const PerfilConPreparacion& p : todos->valor()) {
            if (p.perfil.rfc == QStringLiteral("EKU9003173C9")) {
                QCOMPARE(p.preparacion, PreparacionPerfil::SinCredencial);
                QVERIFY(!p.listoParaSolicitudes);
            }
        }
    }
    // El mismo perfil con credencial Lista pero inactivo: no se devuelve.
    {
        PerfilesConInactivos perfiles(root->perfiles(), {conCredencial->valor().id.texto()});
        ConsultaPreparacionPerfiles consulta(perfiles, root->credenciales());
        const auto listos = esperar(consulta.listarListosParaSolicitudes());
        QVERIFY(listos && listos->esExito());
        QVERIFY(listos->valor().isEmpty());
    }

    // Centinela: la contrasena no llega a SQLite ni a los mensajes.
    auto revisar = [&](const char* momento) {
        for (const QString& nombre : {QStringLiteral("satcfdi.sqlite3"), QStringLiteral("satcfdi.sqlite3-wal"),
                                      QStringLiteral("satcfdi.sqlite3-shm")}) {
            QFile f(QDir(tmp.path()).filePath(nombre));
            if (!f.exists()) {
                continue;
            }
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray bytes = f.readAll();
            for (const QByteArray& c : {store.contrasenaValida, store.contrasenaValida.toBase64(),
                                        store.contrasenaValida.toHex()}) {
                QVERIFY2(!bytes.contains(c), qPrintable(nombre + QLatin1Char(' ') + QLatin1String(momento)));
            }
        }
        QMutexLocker l(&gMutex);
        for (const QString& m : std::as_const(gMensajes)) {
            QVERIFY2(!m.toUtf8().contains(store.contrasenaValida), "contrasena en un mensaje");
        }
    };
    QVERIFY(QFile::exists(QDir(tmp.path()).filePath(QStringLiteral("satcfdi.sqlite3"))));
    revisar("base abierta");
    if (QTest::currentTestFailed()) {
        return;
    }
    root.reset();
    revisar("base cerrada");
}

#include "TestPreparacionPerfiles.moc"

int ejecutarTestPreparacionPerfiles(int argc, char* argv[])
{
    TestPreparacionPerfiles prueba;
    return QTest::qExec(&prueba, argc, argv);
}
