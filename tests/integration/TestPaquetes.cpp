// T008 (plataforma): raiz de paquetes del root real (D8, criterio 15).

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"

#include "application/operaciones/OperacionExecutor.h"
#include "infrastructure/storage/FilesystemPackageStorage.h"
#include "ports/storage/RutaPaquete.h"

#include "fakes/FakeSecretStore.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QTest>

#include <sys/stat.h>

using namespace satcfdi;

namespace {

int modo(const QString& ruta)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(ruta).constData(), &st) == 0 ? static_cast<int>(st.st_mode & 0777) : -1;
}

// Huella de ~/SAT-CFDI-Downloader (si existe es del usuario: no se toca).
QStringList huellaHome()
{
    const QString raiz = QDir(QDir::homePath()).filePath(QStringLiteral("SAT-CFDI-Downloader"));
    if (!QFileInfo::exists(raiz)) {
        return {QStringLiteral("<no existe>")};
    }
    QStringList r;
    QDirIterator it(raiz, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo f(it.next());
        r.append(f.filePath() + QLatin1Char('@') + QString::number(f.lastModified().toMSecsSinceEpoch()));
    }
    r.sort();
    return r;
}

} // namespace

class TestPaquetes : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(
            "^(?!Populating font family aliases took|This plugin does not support ).*")));
    }

    void raizConDataDirEsDataDirPaquetes();
    void finalSinSolicitudSeConservaConDiagnostico();
    void raizSeCreaFueraDelHiloGrafico();
};

void TestPaquetes::raizConDataDirEsDataDirPaquetes()
{
    const QStringList homeAntes = huellaHome();
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dataDir = tmp.filePath(QStringLiteral("datos"));

    // Resolucion de main (D8): con --data-dir -> <dir>/paquetes.
    const QString esperada = QDir(dataDir).filePath(QStringLiteral("paquetes"));
    QCOMPARE(FilesystemPackageStorage::resolverRaiz(dataDir, QDir::homePath()), esperada);
    QCOMPARE(FilesystemPackageStorage::resolverRaiz(std::nullopt, QStringLiteral("/Users/x")),
             QStringLiteral("/Users/x/SAT-CFDI-Downloader/paquetes"));

    AppBootstrapper::Opciones opciones;
    opciones.directorioDatos = dataDir;
    opciones.raizPaquetes = FilesystemPackageStorage::resolverRaiz(dataDir, QDir::homePath());
    const auto arranque = AppBootstrapper(opciones).preparar();
    QVERIFY(arranque);
    QCOMPARE(arranque.valor().raizPaquetes, esperada);
    fakes::FakeSecretStore secretos;
    {
        OpcionesMonitoreo m;
        m.raizPaquetes = arranque.valor().raizPaquetes;
        AppCompositionRoot root(arranque.valor().rutaBase, secretos, m);
        QCOMPARE(root.raizPaquetes(), esperada);
        // Creada al arrancar, privada y vacia.
        QVERIFY(QFileInfo(esperada).isDir());
        QCOMPARE(modo(esperada), 0700);
        QVERIFY(QDir(esperada).isEmpty());
    }
    {
        // Sin raiz explicita, el root usa <directorio de la base>/paquetes.
        AppCompositionRoot root(arranque.valor().rutaBase, secretos);
        QCOMPARE(root.raizPaquetes(), esperada);
    }
    QCOMPARE(huellaHome(), homeAntes); // nada bajo ~/SAT-CFDI-Downloader
}

void TestPaquetes::finalSinSolicitudSeConservaConDiagnostico()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    AppBootstrapper::Opciones opciones;
    opciones.directorioDatos = tmp.path();
    const auto arranque = AppBootstrapper(opciones).preparar();
    QVERIFY(arranque);
    fakes::FakeSecretStore secretos;
    AppCompositionRoot root(arranque.valor().rutaBase, secretos);

    // Un ZIP final con estructura propia de una solicitud que no existe.
    UbicacionPaquete u;
    u.rfcSolicitante = QStringLiteral("EKU9003173C9");
    u.fechaInicialSat = QStringLiteral("2026-09-01T00:00:00");
    u.solicitudId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    u.idPaqueteSat = QStringLiteral("PAQ_01");
    const auto ruta = rutapaquete::derivarRutaRelativa(u);
    QVERIFY(ruta);
    const QString absoluta = QDir(root.raizPaquetes()).filePath(ruta.valor());
    QVERIFY(QDir().mkpath(QFileInfo(absoluta).absolutePath()));
    QFile zip(absoluta);
    QVERIFY(zip.open(QIODevice::WriteOnly));
    zip.write("PK\x03\x04 contenido");
    zip.close();

    // Recuperacion integrada (criterio 13): se conserva y solo hay diagnostico.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(
        "archivo final sin solicitud asociable \\(se conserva\\): %1").arg(QRegularExpression::escape(ruta.valor()))));
    const QFuture<ResultadoOperacion> f = root.ejecutor().recuperar();
    QVERIFY(QTest::qWaitFor([&] { return f.isFinished(); }, 5000));
    QVERIFY(QFileInfo::exists(absoluta));
    QCOMPARE(QFileInfo(absoluta).size(), qint64(sizeof("PK\x03\x04 contenido") - 1));
}

void TestPaquetes::raizSeCreaFueraDelHiloGrafico()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QThread* hiloMkdir = nullptr;
    QString raizPedida;
    AppBootstrapper::Opciones opciones;
    opciones.directorioDatos = tmp.path();
    opciones.crearRaizPaquetes = [&](const QString& raiz) {
        hiloMkdir = QThread::currentThread();
        raizPedida = raiz;
        crearRaizPaquetesPrivada(raiz); // implementacion real
    };
    const auto arranque = AppBootstrapper(opciones).preparar();
    QVERIFY(arranque);
    QVERIFY(hiloMkdir != nullptr);
    QVERIFY(hiloMkdir != QCoreApplication::instance()->thread());
    QVERIFY(hiloMkdir != QThread::currentThread());
    QCOMPARE(raizPedida, QDir(tmp.path()).filePath(QStringLiteral("paquetes")));
    QCOMPARE(modo(raizPedida), 0700);

    // Solo componentes faltantes: una carpeta existente conserva sus permisos.
    QTemporaryDir otro;
    const QString existente = otro.filePath(QStringLiteral("usuario"));
    QVERIFY(QDir().mkdir(existente));
    QFile::setPermissions(existente, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                         | QFileDevice::ReadGroup | QFileDevice::ExeGroup);
    crearRaizPaquetesPrivada(QDir(existente).filePath(QStringLiteral("a/paquetes")));
    QCOMPARE(modo(existente), 0750);
    QCOMPARE(modo(QDir(existente).filePath(QStringLiteral("a"))), 0700);
    QCOMPARE(modo(QDir(existente).filePath(QStringLiteral("a/paquetes"))), 0700);
}

#include "TestPaquetes.moc"

int ejecutarTestPaquetes(int argc, char* argv[])
{
    TestPaquetes prueba;
    return QTest::qExec(&prueba, argc, argv);
}
