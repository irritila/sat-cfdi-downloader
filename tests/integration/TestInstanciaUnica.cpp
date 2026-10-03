#include "app_core/AppBootstrapper.h"
#include "app_core/ArranqueProceso.h"
#include "app_core/SingleInstanceCoordinator.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>

using namespace satcfdi;

namespace {

// Deja un socket Unix "obsoleto": archivo de socket sin proceso escuchando,
// como el que queda tras un cierre abrupto de la instancia primaria.
bool crearSocketObsoleto(const QString& ruta)
{
    const QByteArray bytes = QFile::encodeName(ruta);
    sockaddr_un dir{};
    if (static_cast<size_t>(bytes.size()) >= sizeof(dir.sun_path)) {
        return false;
    }
    dir.sun_family = AF_UNIX;
    std::memcpy(dir.sun_path, bytes.constData(), static_cast<size_t>(bytes.size()));
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    const bool ok = ::bind(fd, reinterpret_cast<sockaddr*>(&dir), sizeof(dir)) == 0;
    ::close(fd); // sin listen(): nadie acepta conexiones
    return ok && QFile::exists(ruta);
}

} // namespace

class TestInstanciaUnica : public QObject {
    Q_OBJECT

private slots:
    void init();

    void nombrePorDefecto();
    void segundaAperturaActivaALaPrimaria();
    void activacionesSeEncolanHastaHabilitar();
    void secundariaNoLlegaAlBootstrap();
    void canalObsoletoSeRetiraYSeAsumePrimaria();
    void arranquesSimultaneosDejanUnaSolaPrimaria_data();
    void arranquesSimultaneosDejanUnaSolaPrimaria();

private:
    // Rutas cortas: sun_path admite ~104 bytes en macOS.
    QTemporaryDir m_tmp{QDir::tempPath() + QStringLiteral("/sci-XXXXXX")};
};

void TestInstanciaUnica::init()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    QVERIFY(m_tmp.isValid());
}

void TestInstanciaUnica::nombrePorDefecto()
{
    QCOMPARE(SingleInstanceCoordinator::nombreCanalPorDefecto(),
             QStringLiteral("mx.adenium.satcfdi-downloader.%1.instance-v1").arg(::getuid()));
}

void TestInstanciaUnica::segundaAperturaActivaALaPrimaria()
{
    const QString nombre = m_tmp.filePath(QStringLiteral("a"));
    SingleInstanceCoordinator primaria(nombre);
    QCOMPARE(primaria.adquirir(), SingleInstanceCoordinator::Rol::Primary);
    primaria.habilitarEntrega();
    QSignalSpy activaciones(&primaria, &SingleInstanceCoordinator::activacionSolicitada);

    SingleInstanceCoordinator segunda(nombre);
    QCOMPARE(segunda.adquirir(), SingleInstanceCoordinator::Rol::SecondaryActivated);
    QVERIFY(activaciones.wait(5000));
    QCOMPARE(activaciones.size(), 1);

    SingleInstanceCoordinator tercera(nombre);
    QCOMPARE(tercera.adquirir(), SingleInstanceCoordinator::Rol::SecondaryActivated);
    QVERIFY(activaciones.wait(5000));
    QCOMPARE(activaciones.size(), 2);
    QCOMPARE(primaria.rol(), SingleInstanceCoordinator::Rol::Primary);
}

void TestInstanciaUnica::activacionesSeEncolanHastaHabilitar()
{
    const QString nombre = m_tmp.filePath(QStringLiteral("b"));
    SingleInstanceCoordinator primaria(nombre);
    QCOMPARE(primaria.adquirir(), SingleInstanceCoordinator::Rol::Primary);
    QSignalSpy activaciones(&primaria, &SingleInstanceCoordinator::activacionSolicitada);

    SingleInstanceCoordinator s1(nombre);
    SingleInstanceCoordinator s2(nombre);
    QCOMPARE(s1.adquirir(), SingleInstanceCoordinator::Rol::SecondaryActivated);
    QCOMPARE(s2.adquirir(), SingleInstanceCoordinator::Rol::SecondaryActivated);
    QTRY_COMPARE(primaria.activacionesPendientes(), 2);
    QCOMPARE(activaciones.size(), 0); // aun no existe el controlador

    primaria.habilitarEntrega();
    QCOMPARE(activaciones.size(), 1); // coalescidas en una sola activacion
    QCOMPARE(primaria.activacionesPendientes(), 0);
}

void TestInstanciaUnica::secundariaNoLlegaAlBootstrap()
{
    const QString nombre = m_tmp.filePath(QStringLiteral("c"));
    SingleInstanceCoordinator primaria(nombre);
    QCOMPARE(primaria.adquirir(), SingleInstanceCoordinator::Rol::Primary);

    int inicializaciones = 0;
    AppBootstrapper::Opciones opciones;
    opciones.directorioDatos = m_tmp.filePath(QStringLiteral("datos"));
    opciones.inicializador = [&inicializaciones](const QString&) {
        ++inicializaciones;
        return Resultado<InformeInicializacionSqlite, ErrorPersistencia>::fallo(
            ErrorPersistencia::de(ErrorPersistencia::Tipo::Interno, QStringLiteral("no debe correr")));
    };

    SingleInstanceCoordinator segunda(nombre);
    const PreparacionProceso p = prepararProceso(segunda, AppBootstrapper(opciones));
    QCOMPARE(p.tipo, PreparacionProceso::Tipo::Secundaria);
    QCOMPARE(p.codigoSalida(), 0);
    QCOMPARE(inicializaciones, 0);
    QVERIFY(!QDir(opciones.directorioDatos).exists());
}

void TestInstanciaUnica::canalObsoletoSeRetiraYSeAsumePrimaria()
{
    const QString nombre = m_tmp.filePath(QStringLiteral("d"));
    QVERIFY(crearSocketObsoleto(nombre));

    SingleInstanceCoordinator nueva(nombre);
    QCOMPARE(nueva.adquirir(), SingleInstanceCoordinator::Rol::Primary);
    QVERIFY(nueva.canalObsoletoRetirado());

    // El canal recuperado funciona para una segunda apertura.
    nueva.habilitarEntrega();
    QSignalSpy activaciones(&nueva, &SingleInstanceCoordinator::activacionSolicitada);
    SingleInstanceCoordinator segunda(nombre);
    QCOMPARE(segunda.adquirir(), SingleInstanceCoordinator::Rol::SecondaryActivated);
    QVERIFY(activaciones.wait(5000));
}

void TestInstanciaUnica::arranquesSimultaneosDejanUnaSolaPrimaria_data()
{
    QTest::addColumn<bool>("conCanalObsoleto");
    QTest::newRow("canal limpio") << false;
    QTest::newRow("canal obsoleto") << true;
}

void TestInstanciaUnica::arranquesSimultaneosDejanUnaSolaPrimaria()
{
    QFETCH(bool, conCanalObsoleto);
    constexpr int kProcesos = 8;
    const QString nombre =
        m_tmp.filePath(conCanalObsoleto ? QStringLiteral("mo") : QStringLiteral("ml"));
    if (conCanalObsoleto) {
        QVERIFY(crearSocketObsoleto(nombre));
    }

    // Arranques concurrentes reales: N procesos con el mismo canal.
    QList<QProcess*> procesos;
    for (int i = 0; i < kProcesos; ++i) {
        auto* p = new QProcess(this);
        p->setProgram(QStringLiteral(SATCFDI_INSTANCE_HELPER));
        p->setArguments({nombre, QString::number(kProcesos - 1)});
        procesos.append(p);
    }
    for (QProcess* p : procesos) {
        p->start();
    }

    int primarias = 0;
    int secundarias = 0;
    QString salidaPrimaria;
    for (QProcess* p : procesos) {
        QVERIFY2(p->waitForFinished(30000), "proceso auxiliar sin terminar");
        QCOMPARE(p->exitStatus(), QProcess::NormalExit);
        const QString salida = QString::fromUtf8(p->readAllStandardOutput());
        const QString rol = salida.section(QLatin1Char('\n'), 0, 0);
        if (rol == QStringLiteral("Primary")) {
            ++primarias;
            salidaPrimaria = salida;
            QCOMPARE(p->exitCode(), 0);
        } else if (rol == QStringLiteral("SecondaryActivated")) {
            ++secundarias;
            QCOMPARE(p->exitCode(), 0);
        } else {
            QFAIL(qPrintable(QStringLiteral("rol inesperado: ") + salida +
                             QString::fromUtf8(p->readAllStandardError())));
        }
        delete p;
    }
    QCOMPARE(primarias, 1);
    QCOMPARE(secundarias, kProcesos - 1);
    // La primaria recibio una activacion de cada secundaria.
    QVERIFY2(salidaPrimaria.contains(QStringLiteral("activaciones=%1").arg(kProcesos - 1)),
             qPrintable(salidaPrimaria));
}

#include "TestInstanciaUnica.moc"

int ejecutarTestInstanciaUnica(int argc, char* argv[])
{
    TestInstanciaUnica prueba;
    return QTest::qExec(&prueba, argc, argv);
}
