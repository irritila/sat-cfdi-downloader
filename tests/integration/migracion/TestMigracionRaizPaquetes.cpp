// T019 (D2, D3, D4, D8): migracion de la raiz de paquetes y identidad interna.
// - Politica con FileOpsSimulado y home inyectado ("/h"): todos los casos de
//   la tarea, sin tocar el disco.
// - PosixFileOps real sobre un home temporal: inode conservado del ZIP y del
//   .part, enlaces, RENAME_EXCL, recuperacion sobre la raiz efectiva.
// - AppBootstrapper: orden (base -> migracion, hilo de E/S), instancia
//   secundaria y --data-dir sin migracion.
// - Identidad: AppDataLocation termina en Adenium/SAT CFDI Downloader y el
//   display name es "Archivo Fiscal".
// Nunca usa las carpetas reales del usuario.

#include "FileOpsSimulado.h"

#include "app_core/AppBootstrapper.h"
#include "app_core/ArranqueProceso.h"
#include "app_core/MigracionRaizPaquetes.h"
#include "app_core/SingleInstanceCoordinator.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"
#include "infrastructure/storage/FileOps.h"
#include "infrastructure/storage/FilesystemPackageStorage.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMutex>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

using namespace satcfdi;
using fakes::FileOpsSimulado;
using Op = FileOpsSimulado::Op;
using Tipo = FileOps::TipoEntrada;
using E = EstadoRaizPaquetes;
using Motivo = AvisoRaizPaquetes::Motivo;

Q_DECLARE_METATYPE(satcfdi::EstadoRaizPaquetes)

namespace {

const QByteArray H = QByteArrayLiteral("/h");
const QByteArray L = QByteArrayLiteral("/h/SAT-CFDI-Downloader");
const QByteArray LP = QByteArrayLiteral("/h/SAT-CFDI-Downloader/paquetes");
const QByteArray N = QByteArrayLiteral("/h/Archivo-Fiscal");
const QByteArray NP = QByteArrayLiteral("/h/Archivo-Fiscal/paquetes");
const QString kUuid = QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e");
const QString kRfc = QStringLiteral("AAA010101AAA");
const QByteArray kCentinela = QByteArrayLiteral("CENTINELA-ZIP-T019");

// Captura de logs (todas las categorias) para comprobar el saneamiento.
QStringList g_logs;
QMutex g_mutex;
QtMessageHandler g_anterior = nullptr;
void capturar(QtMsgType tipo, const QMessageLogContext& c, const QString& m)
{
    {
        QMutexLocker b(&g_mutex);
        g_logs.append(m);
    }
    if (g_anterior) {
        g_anterior(tipo, c, m);
    }
}

// Home simulado con un paquete legado (RFC/periodo/solicitud/zip).
FileOpsSimulado conLegado(bool conOtros = false)
{
    FileOpsSimulado fs;
    fs.dir(H);
    fs.dir(L, 0755);
    fs.dir(LP, 0750);
    fs.dir(LP + "/AAA010101AAA", 0700);
    fs.archivo(LP + "/AAA010101AAA/p1.zip");
    if (conOtros) {
        fs.archivo(L + "/notas.txt");
    }
    return fs;
}

ResolucionRaizPaquetes resolver(FileOpsSimulado& fs)
{
    return MigracionRaizPaquetes(QString::fromLatin1(H), fs).resolver();
}

QString s(const QByteArray& b)
{
    return QString::fromLatin1(b);
}

void verificarAvisoLegado(const ResolucionRaizPaquetes& r, Motivo motivo)
{
    QVERIFY(r.aviso);
    QCOMPARE(r.aviso->motivo, motivo);
    QCOMPARE(r.aviso->rutaLegada, QStringLiteral("~/SAT-CFDI-Downloader/paquetes"));
    QCOMPARE(r.aviso->rutaNueva, QStringLiteral("~/Archivo-Fiscal/paquetes"));
    QCOMPARE(r.aviso->rutaEfectiva, QStringLiteral("~/SAT-CFDI-Downloader/paquetes"));
    QVERIFY(r.aviso->mensaje.contains(QStringLiteral("~/SAT-CFDI-Downloader/paquetes")));
    QVERIFY(!r.aviso->mensaje.contains(QStringLiteral("/h/")));
    QCOMPARE(r.raizEfectiva, s(LP));
}

// --- Filesystem real ---------------------------------------------------------

quint64 inode(const QString& ruta)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(ruta).constData(), &st) == 0 ? quint64(st.st_ino) : 0;
}

unsigned modo(const QString& ruta)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(ruta).constData(), &st) == 0 ? unsigned(st.st_mode & 07777) : 0u;
}

bool escribir(const QString& ruta, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QByteArray leer(const QString& ruta)
{
    QFile f(ruta);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Huella de un arbol sin seguir enlaces: ruta relativa, tipo, inode y objetivo.
QStringList huella(const QString& raiz)
{
    QStringList r;
    QDirIterator it(raiz, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString ruta = it.next();
        const QFileInfo f(ruta);
        r.append(QDir(raiz).relativeFilePath(ruta) + QLatin1Char('|')
                 + (f.isSymLink() ? QStringLiteral("L>") + f.symLinkTarget() : f.isDir() ? QStringLiteral("D") : QStringLiteral("F"))
                 + QLatin1Char('|') + QString::number(inode(ruta)));
    }
    r.sort();
    return r;
}

// Arbol legado real con un ZIP final y un .part propios y un archivo ajeno.
struct HomeReal {
    QTemporaryDir tmp;
    QString home;
    QString legado;
    QString nuevo;
    QString carpeta; // relativa a la raiz: RFC/periodo/solicitud
    HomeReal()
    {
        home = tmp.filePath(QStringLiteral("home"));
        legado = home + QStringLiteral("/SAT-CFDI-Downloader");
        nuevo = home + QStringLiteral("/Archivo-Fiscal");
        carpeta = kRfc + QStringLiteral("/2026-03/") + kUuid;
        QDir().mkpath(home);
    }
    QString legadaRaiz() const { return legado + QStringLiteral("/paquetes"); }
    QString nuevaRaiz() const { return nuevo + QStringLiteral("/paquetes"); }
    bool poblarLegado()
    {
        return escribir(legadaRaiz() + QLatin1Char('/') + carpeta + QStringLiteral("/p1.zip"), kCentinela)
               && escribir(legadaRaiz() + QLatin1Char('/') + carpeta + QStringLiteral("/.p2.zip.0123456789abcdef.part"),
                           QByteArrayLiteral("parcial"))
               && escribir(legadaRaiz() + QLatin1Char('/') + carpeta + QStringLiteral("/LEEME.txt"),
                           QByteArrayLiteral("ajeno"));
    }
    ResolucionRaizPaquetes migrar() const { return migradorRaizPaquetesPosix(home)(); }
};

} // namespace

class TestMigracionRaizPaquetes : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    // D2
    void identidadCongelada();

    // D4 con FileOps simulado
    void origenAusenteCreaRaizNueva();
    void origenAusenteMkdirFallaArrancaYAvisa();
    void origenAusenteMkdirRaizFallaRetiraPadre();
    void soloPaquetesYDsStoreMigraPadre();
    void otrosArchivosMigraSoloPaquetes();
    void destinoVacioPrevio_data();
    void destinoVacioPrevio();
    void destinoConflicto_data();
    void destinoConflicto();
    void origenEnlace_data();
    void origenEnlace();
    void origenNoDirectorio_data();
    void origenNoDirectorio();
    void origenNoDirectorioSinRaizNueva();
    void padreEnlaceConOtrasCosasRenombraElEnlace();
    void padreCambiaAntesDelRenameMigraSoloPaquetes();
    void padreEnlaceSinPaquetesNoSeToca_data();
    void padreEnlaceSinPaquetesNoSeToca();
    void mkdirCreaYFallaSeRetira_data();
    void mkdirCreaYFallaSeRetira();
    void padreViejoSinPaquetesCreaNueva_data();
    void padreViejoSinPaquetesCreaNueva();
    void fallosSinCambios_data();
    void fallosSinCambios();
    void renameFallaRetiraPadreNuevo();
    void yaMigradoNoHaceNada();
    void idempotenteEnSegundoArranque_data();
    void idempotenteEnSegundoArranque();

    // D4 con PosixFileOps real
    void realMigraPadreConservaInodesYRecupera();
    void realMigraSoloPaquetesConservaPermisos();
    void realEnlaceSeRenombraObjetivoIntacto();
    void realEnlaceConOtrasCosasObjetivoIntacto();
    void realEnlaceSinPaquetesNoSeToca();
    void realRenameExclNoReemplazaDirectorioVacio();
    void realConflictoNoTocaNada();
    void realLogSinRutasRfcNiZip();

    // D8 en el bootstrap
    void bootstrapMigraEnHiloDeEsDespuesDeLaBase();
    void bootstrapSinBaseNoMigra();
    void secundariaNoMigra();
    void dataDirNoTocaHome();
    void sinDataDirConfiguraMigrador();

private:
    bool m_realLegadoExistia = false;
    bool m_realNuevoExistia = false;
    QTemporaryDir m_canal{QDir::tempPath() + QStringLiteral("/mrp-XXXXXX")};
};

void TestMigracionRaizPaquetes::initTestCase()
{
    m_realLegadoExistia = QDir::home().exists(QStringLiteral("SAT-CFDI-Downloader"));
    m_realNuevoExistia = QDir::home().exists(QStringLiteral("Archivo-Fiscal"));
}

void TestMigracionRaizPaquetes::cleanupTestCase()
{
    // Ninguna prueba crea ni mueve las carpetas reales del usuario.
    QCOMPARE(QDir::home().exists(QStringLiteral("SAT-CFDI-Downloader")), m_realLegadoExistia);
    QCOMPARE(QDir::home().exists(QStringLiteral("Archivo-Fiscal")), m_realNuevoExistia);
}

void TestMigracionRaizPaquetes::init()
{
    // Las advertencias de satcfdi.arranque se esperan explicitamente.
    QTest::failOnWarning(QRegularExpression(QStringLiteral("^(?!raiz de paquetes: ).*")));
}

void TestMigracionRaizPaquetes::identidadCongelada()
{
    configurarIdentidadAplicacion();
    QCOMPARE(QCoreApplication::organizationName(), QStringLiteral("Adenium"));
    QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("SAT CFDI Downloader"));
    QCOMPARE(QGuiApplication::applicationDisplayName(), QStringLiteral("Archivo Fiscal"));
    // writableLocation no crea nada; sin modo de prueba es la ruta productiva.
    const bool modoPrueba = QStandardPaths::isTestModeEnabled();
    QStandardPaths::setTestModeEnabled(false);
    const QString datos = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QStandardPaths::setTestModeEnabled(modoPrueba);
    QVERIFY2(datos.endsWith(QStringLiteral("/Adenium/SAT CFDI Downloader")), qPrintable(datos));
    QCOMPARE(AppBootstrapper::directorioDatosPorDefecto().section(QLatin1Char('/'), -2),
             QStringLiteral("Adenium/SAT CFDI Downloader"));
}

void TestMigracionRaizPaquetes::origenAusenteCreaRaizNueva()
{
    FileOpsSimulado fs;
    fs.dir(H);
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::Nueva);
    QCOMPARE(r.raizEfectiva, s(NP));
    QVERIFY(!r.aviso);
    QCOMPARE(fs.cambios, (QStringList{QStringLiteral("mkdir /h/Archivo-Fiscal 700"),
                                      QStringLiteral("mkdir /h/Archivo-Fiscal/paquetes 700")}));
    QCOMPARE(fs.nodos.value(N).modo, 0700u);
    QCOMPARE(fs.nodos.value(NP).modo, 0700u);
    QCOMPARE(fs.prohibidas, 0);
}

void TestMigracionRaizPaquetes::origenAusenteMkdirFallaArrancaYAvisa()
{
    FileOpsSimulado fs;
    fs.dir(H);
    fs.fallar(Op::CrearDirectorio, EACCES);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^raiz de paquetes: nueva sin crear")));
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::Nueva);
    QCOMPARE(r.raizEfectiva, s(NP)); // comportamiento actual: arranca con la raiz nueva
    QVERIFY(r.aviso);
    QCOMPARE(r.aviso->motivo, Motivo::RaizNoDisponible);
    QCOMPARE(r.aviso->rutaEfectiva, QStringLiteral("~/Archivo-Fiscal/paquetes"));
    QVERIFY(fs.cambios.isEmpty());
}

void TestMigracionRaizPaquetes::origenAusenteMkdirRaizFallaRetiraPadre()
{
    FileOpsSimulado fs;
    fs.dir(H);
    fs.fallar(Op::CrearDirectorio, ENOSPC, "/paquetes");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^raiz de paquetes: nueva sin crear")));
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::Nueva);
    QVERIFY(r.aviso);
    QCOMPARE(fs.cambios, (QStringList{QStringLiteral("mkdir /h/Archivo-Fiscal 700"),
                                      QStringLiteral("rmdir /h/Archivo-Fiscal")}));
    QCOMPARE(fs.nodos.keys(), QList<QByteArray>{H});
}

void TestMigracionRaizPaquetes::soloPaquetesYDsStoreMigraPadre()
{
    FileOpsSimulado fs = conLegado();
    fs.archivo(L + "/.DS_Store");
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::MigradaPadre);
    QCOMPARE(r.raizEfectiva, s(NP));
    QVERIFY(!r.aviso);
    QCOMPARE(fs.cambios, QStringList{QStringLiteral("rename /h/SAT-CFDI-Downloader /h/Archivo-Fiscal")});
    QVERIFY(!fs.nodos.contains(L));
    QVERIFY(fs.nodos.contains(NP + "/AAA010101AAA/p1.zip"));
    QVERIFY(fs.nodos.contains(N + "/.DS_Store"));
    // Permisos de lo existente intactos (0700 solo en directorios nuevos).
    QCOMPARE(fs.nodos.value(N).modo, 0755u);
    QCOMPARE(fs.nodos.value(NP).modo, 0750u);
    QCOMPARE(fs.prohibidas, 0);
}

void TestMigracionRaizPaquetes::otrosArchivosMigraSoloPaquetes()
{
    FileOpsSimulado fs = conLegado(true);
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::MigradaPaquetes);
    QCOMPARE(r.raizEfectiva, s(NP));
    QVERIFY(!r.aviso);
    QCOMPARE(fs.cambios, (QStringList{QStringLiteral("mkdir /h/Archivo-Fiscal 700"),
                                      QStringLiteral("rename /h/SAT-CFDI-Downloader/paquetes /h/Archivo-Fiscal/paquetes")}));
    // El padre viejo queda con lo demas y sin paquetes.
    QVERIFY(fs.nodos.contains(L + "/notas.txt"));
    QVERIFY(!fs.nodos.contains(LP));
    QCOMPARE(fs.nodos.value(N).modo, 0700u);  // nuevo
    QCOMPARE(fs.nodos.value(NP).modo, 0750u); // renombrado: conserva el suyo
    QVERIFY(fs.nodos.contains(NP + "/AAA010101AAA/p1.zip"));
    QCOMPARE(fs.prohibidas, 0);
}

void TestMigracionRaizPaquetes::destinoVacioPrevio_data()
{
    QTest::addColumn<bool>("conPaquetes");
    QTest::addColumn<QStringList>("rmdirs");
    QTest::newRow("padre vacio") << false << QStringList{QStringLiteral("rmdir /h/Archivo-Fiscal")};
    QTest::newRow("padre con paquetes vacio")
        << true
        << QStringList{QStringLiteral("rmdir /h/Archivo-Fiscal/paquetes"), QStringLiteral("rmdir /h/Archivo-Fiscal")};
}

void TestMigracionRaizPaquetes::destinoVacioPrevio()
{
    QFETCH(bool, conPaquetes);
    QFETCH(QStringList, rmdirs);
    FileOpsSimulado fs = conLegado();
    fs.dir(N, 0700);
    if (conPaquetes) {
        fs.dir(NP, 0700);
    }
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::MigradaPadre);
    QVERIFY(!r.aviso);
    QCOMPARE(fs.cambios, rmdirs + QStringList{QStringLiteral("rename /h/SAT-CFDI-Downloader /h/Archivo-Fiscal")});
    QVERIFY(fs.nodos.contains(NP + "/AAA010101AAA/p1.zip"));
}

void TestMigracionRaizPaquetes::destinoConflicto_data()
{
    QTest::addColumn<int>("caso");
    QTest::newRow("paquetes destino con contenido") << 0;
    QTest::newRow("destino es archivo") << 1;
    QTest::newRow("destino es enlace") << 2;
    QTest::newRow("paquetes destino es enlace") << 3;
    QTest::newRow("paquetes destino es archivo") << 4;
    QTest::newRow("destino con otras cosas") << 5;
    QTest::newRow("destino solo con .DS_Store") << 6;
}

void TestMigracionRaizPaquetes::destinoConflicto()
{
    QFETCH(int, caso);
    FileOpsSimulado fs = conLegado();
    switch (caso) {
    case 0:
        fs.dir(N);
        fs.dir(NP);
        fs.archivo(NP + "/otro.zip");
        break;
    case 1:
        fs.archivo(N);
        break;
    case 2:
        fs.enlace(N, "/Volumes/X");
        break;
    case 3:
        fs.dir(N);
        fs.enlace(NP, "/Volumes/X");
        break;
    case 4:
        fs.dir(N);
        fs.archivo(NP);
        break;
    case 5:
        fs.dir(N);
        fs.archivo(N + "/guion.json");
        break;
    case 6:
        fs.dir(N);
        fs.archivo(N + "/.DS_Store");
        break;
    }
    const auto antes = fs.nodos;
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("^raiz de paquetes: usa_legada_por_conflicto")));
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::UsaLegadaPorConflicto);
    verificarAvisoLegado(r, Motivo::Conflicto);
    QVERIFY(fs.cambios.isEmpty());
    QVERIFY(fs.nodos == antes);
    QCOMPARE(fs.prohibidas, 0);
}

void TestMigracionRaizPaquetes::origenEnlace_data()
{
    QTest::addColumn<bool>("padreEsEnlace");
    QTest::newRow("padre enlace") << true;
    QTest::newRow("paquetes enlace") << false;
}

void TestMigracionRaizPaquetes::origenEnlace()
{
    QFETCH(bool, padreEsEnlace);
    FileOpsSimulado fs;
    fs.dir(H);
    fs.dir("/Volumes");
    fs.dir("/Volumes/Externo");
    fs.archivo("/Volumes/Externo/real.zip");
    if (padreEsEnlace) {
        fs.enlace(L, "/Volumes/Externo/SAT");
        fs.dir(LP); // hijo a traves del enlace
    } else {
        fs.dir(L);
        fs.archivo(L + "/notas.txt");
        fs.enlace(LP, "/Volumes/Externo/paquetes");
    }
    const auto r = resolver(fs);
    QVERIFY(!r.aviso);
    QCOMPARE(r.raizEfectiva, s(NP));
    if (padreEsEnlace) {
        QCOMPARE(r.estado, E::MigradaPadre);
        QCOMPARE(fs.nodos.value(N).tipo, Tipo::Enlace);
        QCOMPARE(fs.nodos.value(N).objetivo, QByteArrayLiteral("/Volumes/Externo/SAT"));
    } else {
        QCOMPARE(r.estado, E::MigradaPaquetes);
        QCOMPARE(fs.nodos.value(NP).tipo, Tipo::Enlace);
        QCOMPARE(fs.nodos.value(NP).objetivo, QByteArrayLiteral("/Volumes/Externo/paquetes"));
    }
    // El objetivo no se toco.
    QVERIFY(fs.nodos.contains("/Volumes/Externo/real.zip"));
    for (const QString& c : std::as_const(fs.cambios)) {
        QVERIFY2(!c.contains(QStringLiteral("/Volumes")), qPrintable(c));
    }
}

void TestMigracionRaizPaquetes::origenNoDirectorio_data()
{
    QTest::addColumn<bool>("padre");
    QTest::newRow("padre es archivo") << true;
    QTest::newRow("paquetes es archivo") << false;
}

void TestMigracionRaizPaquetes::origenNoDirectorio()
{
    // Ahi no puede haber ZIP: se usa (y crea) la raiz nueva sin tocar el
    // archivo viejo, con aviso Error.
    QFETCH(bool, padre);
    FileOpsSimulado fs;
    fs.dir(H);
    if (padre) {
        fs.archivo(L);
    } else {
        fs.dir(L);
        fs.archivo(LP);
    }
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("^raiz de paquetes: nueva \\(.* no es directorio; no se modifico")));
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::Nueva);
    QCOMPARE(r.raizEfectiva, s(NP));
    QVERIFY(r.aviso);
    QCOMPARE(r.aviso->motivo, Motivo::Error);
    QCOMPARE(r.aviso->rutaEfectiva, QStringLiteral("~/Archivo-Fiscal/paquetes"));
    QVERIFY(!r.aviso->mensaje.contains(QStringLiteral("/h/")));
    QCOMPARE(fs.cambios, (QStringList{QStringLiteral("mkdir /h/Archivo-Fiscal 700"),
                                      QStringLiteral("mkdir /h/Archivo-Fiscal/paquetes 700")}));
    QCOMPARE(fs.nodos.value(padre ? L : LP).tipo, Tipo::Archivo); // intacto
    QCOMPARE(fs.prohibidas, 0);
}

void TestMigracionRaizPaquetes::origenNoDirectorioSinRaizNueva()
{
    FileOpsSimulado fs;
    fs.dir(H);
    fs.archivo(L);
    fs.fallar(Op::CrearDirectorio, EACCES);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^raiz de paquetes: nueva sin crear")));
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::Nueva);
    QCOMPARE(r.raizEfectiva, s(NP));
    QVERIFY(r.aviso);
    QCOMPARE(r.aviso->motivo, Motivo::RaizNoDisponible);
    QVERIFY(fs.cambios.isEmpty());
}

void TestMigracionRaizPaquetes::padreEnlaceConOtrasCosasRenombraElEnlace()
{
    FileOpsSimulado fs;
    fs.dir(H);
    fs.enlace(L, "/Volumes/Externo/SAT");
    fs.dir(LP);                 // hijos a traves del enlace (objetivo)
    fs.archivo(L + "/notas.txt");
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::MigradaPadre);
    QVERIFY(!r.aviso);
    QCOMPARE(fs.cambios, QStringList{QStringLiteral("rename /h/SAT-CFDI-Downloader /h/Archivo-Fiscal")});
    QCOMPARE(fs.nodos.value(N).tipo, Tipo::Enlace);
    QCOMPARE(fs.nodos.value(N).objetivo, QByteArrayLiteral("/Volumes/Externo/SAT"));
    QCOMPARE(fs.listados.value(L), 0); // el objetivo nunca se lista
}

void TestMigracionRaizPaquetes::padreEnlaceSinPaquetesNoSeToca_data()
{
    QTest::addColumn<bool>("raizNuevaExiste");
    QTest::newRow("sin raiz nueva") << false;
    QTest::newRow("con raiz nueva") << true;
}

void TestMigracionRaizPaquetes::padreEnlaceSinPaquetesNoSeToca()
{
    // Sin `paquetes` en el objetivo no hay origen: el enlace no se renombra.
    QFETCH(bool, raizNuevaExiste);
    FileOpsSimulado fs;
    fs.dir(H);
    fs.enlace(L, "/Volumes/Externo/SAT");
    fs.archivo(L + "/notas.txt"); // contenido del objetivo
    if (raizNuevaExiste) {
        fs.dir(N);
        fs.dir(NP);
    }
    const auto r = resolver(fs);
    QCOMPARE(r.estado, raizNuevaExiste ? E::YaNueva : E::Nueva);
    QCOMPARE(r.raizEfectiva, s(NP));
    QVERIFY(!r.aviso);
    QCOMPARE(fs.nodos.value(L).tipo, Tipo::Enlace);
    QCOMPARE(fs.nodos.value(L).objetivo, QByteArrayLiteral("/Volumes/Externo/SAT"));
    QVERIFY(fs.nodos.contains(L + "/notas.txt"));
    QCOMPARE(fs.listados.value(L), 0);
    for (const QString& c : std::as_const(fs.cambios)) {
        QVERIFY2(c.startsWith(QStringLiteral("mkdir /h/Archivo-Fiscal")), qPrintable(c));
    }
    QCOMPARE(fs.cambios.size(), raizNuevaExiste ? 0 : 2);
}

void TestMigracionRaizPaquetes::padreCambiaAntesDelRenameMigraSoloPaquetes()
{
    FileOpsSimulado fs = conLegado();
    // Entre la primera y la segunda lectura aparece un archivo en el padre.
    fs.alListar = [&fs](const QByteArray& ruta, int n) {
        if (ruta == L && n == 2) {
            fs.archivo(L + "/nuevo.txt");
        }
    };
    const auto r = resolver(fs);
    QCOMPARE(fs.listados.value(L), 2);
    QCOMPARE(r.estado, E::MigradaPaquetes);
    QCOMPARE(fs.cambios, (QStringList{QStringLiteral("mkdir /h/Archivo-Fiscal 700"),
                                      QStringLiteral("rename /h/SAT-CFDI-Downloader/paquetes /h/Archivo-Fiscal/paquetes")}));
    QVERIFY(fs.nodos.contains(L + "/nuevo.txt")); // se queda en el padre viejo
}

void TestMigracionRaizPaquetes::mkdirCreaYFallaSeRetira_data()
{
    QTest::addColumn<bool>("conLegadoOtros");
    QTest::newRow("migrar solo paquetes") << true;
    QTest::newRow("origen ausente") << false;
}

void TestMigracionRaizPaquetes::mkdirCreaYFallaSeRetira()
{
    QFETCH(bool, conLegadoOtros);
    FileOpsSimulado fs;
    if (conLegadoOtros) {
        fs = conLegado(true);
    } else {
        fs.dir(H);
    }
    fs.crearYFallar(N, EPERM); // mkdir bien, chmod mal
    const auto antes = fs.nodos;
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(conLegadoOtros ? QStringLiteral("^raiz de paquetes: usa_legada_por_error")
                                                           : QStringLiteral("^raiz de paquetes: nueva sin crear")));
    const auto r = resolver(fs);
    QCOMPARE(r.estado, conLegadoOtros ? E::UsaLegadaPorError : E::Nueva);
    QVERIFY(r.aviso);
    QVERIFY(fs.nodos == antes); // el directorio creado se retiro
    QCOMPARE(fs.cambios, (QStringList{QStringLiteral("mkdir /h/Archivo-Fiscal 700"),
                                      QStringLiteral("rmdir /h/Archivo-Fiscal")}));
}

void TestMigracionRaizPaquetes::padreViejoSinPaquetesCreaNueva_data()
{
    QTest::addColumn<bool>("padreNuevoExiste");
    QTest::newRow("sin padre nuevo") << false;
    QTest::newRow("padre nuevo sin paquetes") << true;
}

void TestMigracionRaizPaquetes::padreViejoSinPaquetesCreaNueva()
{
    QFETCH(bool, padreNuevoExiste);
    FileOpsSimulado fs;
    fs.dir(H);
    fs.dir(L);
    fs.archivo(L + "/notas.txt");
    if (padreNuevoExiste) {
        fs.dir(N, 0755);
    }
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::Nueva);
    QCOMPARE(r.raizEfectiva, s(NP));
    QVERIFY(!r.aviso);
    QCOMPARE(fs.nodos.value(NP).modo, 0700u);
    QVERIFY(fs.nodos.contains(L + "/notas.txt"));
    if (padreNuevoExiste) {
        QCOMPARE(fs.nodos.value(N).modo, 0755u); // existente: intacto
    }
}

void TestMigracionRaizPaquetes::fallosSinCambios_data()
{
    QTest::addColumn<int>("op");
    QTest::addColumn<int>("error");
    QTest::addColumn<QByteArray>("sufijo");
    QTest::addColumn<bool>("conOtros");
    QTest::addColumn<bool>("destinoVacio");
    QTest::newRow("lstat padre previo") << int(Op::Tipo) << EACCES << L << false << false;
    QTest::newRow("lstat raiz previa") << int(Op::Tipo) << EIO << LP << false << false;
    QTest::newRow("lstat destino") << int(Op::Tipo) << EACCES << N << false << false;
    QTest::newRow("listar padre previo") << int(Op::Listar) << EACCES << L << false << false;
    QTest::newRow("listar destino") << int(Op::Listar) << EACCES << N << false << true;
    QTest::newRow("mkdir padre nuevo") << int(Op::CrearDirectorio) << EACCES << N << true << false;
    QTest::newRow("rmdir destino vacio") << int(Op::EliminarDirectorioVacio) << EBUSY << N << false << true;
    QTest::newRow("rename EXDEV") << int(Op::Renombrar) << EXDEV << L << false << false;
    QTest::newRow("rename permisos") << int(Op::Renombrar) << EACCES << L << false << false;
    QTest::newRow("rename paquetes EXDEV") << int(Op::Renombrar) << EXDEV << LP << true << false;
}

void TestMigracionRaizPaquetes::fallosSinCambios()
{
    QFETCH(int, op);
    QFETCH(int, error);
    QFETCH(QByteArray, sufijo);
    QFETCH(bool, conOtros);
    QFETCH(bool, destinoVacio);
    FileOpsSimulado fs = conLegado(conOtros);
    if (destinoVacio) {
        fs.dir(N);
    }
    fs.fallar(Op(op), error, sufijo);
    const auto antes = fs.nodos;
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("^raiz de paquetes: usa_legada_por_error \\(paso [a-z ]+, errno %1;")
                                                .arg(error)));
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::UsaLegadaPorError);
    verificarAvisoLegado(r, Motivo::Error);
    // Sin cambios netos (un padre nuevo creado se retira) y sin operaciones de archivo.
    QVERIFY(fs.nodos == antes);
    QCOMPARE(fs.prohibidas, 0);
}

void TestMigracionRaizPaquetes::renameFallaRetiraPadreNuevo()
{
    FileOpsSimulado fs = conLegado(true);
    fs.fallar(Op::Renombrar, EXDEV);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^raiz de paquetes: usa_legada_por_error")));
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::UsaLegadaPorError);
    QCOMPARE(fs.cambios, (QStringList{QStringLiteral("mkdir /h/Archivo-Fiscal 700"),
                                      QStringLiteral("rmdir /h/Archivo-Fiscal")}));
}

void TestMigracionRaizPaquetes::yaMigradoNoHaceNada()
{
    FileOpsSimulado fs;
    fs.dir(H);
    fs.dir(N, 0755);
    fs.dir(NP, 0750);
    fs.archivo(NP + "/x.zip");
    const auto antes = fs.nodos;
    const auto r = resolver(fs);
    QCOMPARE(r.estado, E::YaNueva);
    QCOMPARE(r.raizEfectiva, s(NP));
    QVERIFY(!r.aviso);
    QVERIFY(fs.cambios.isEmpty());
    QVERIFY(fs.nodos == antes); // permisos existentes intactos
}

void TestMigracionRaizPaquetes::idempotenteEnSegundoArranque_data()
{
    QTest::addColumn<bool>("conOtros");
    QTest::addColumn<EstadoRaizPaquetes>("primero");
    QTest::newRow("padre") << false << E::MigradaPadre;
    QTest::newRow("solo paquetes") << true << E::MigradaPaquetes;
}

void TestMigracionRaizPaquetes::idempotenteEnSegundoArranque()
{
    QFETCH(bool, conOtros);
    QFETCH(EstadoRaizPaquetes, primero);
    FileOpsSimulado fs = conLegado(conOtros);
    QCOMPARE(resolver(fs).estado, primero);
    const auto tras = fs.nodos;
    fs.cambios.clear();
    const auto segundo = resolver(fs);
    QCOMPARE(segundo.estado, E::YaNueva);
    QCOMPARE(segundo.raizEfectiva, s(NP));
    QVERIFY(!segundo.aviso);
    QVERIFY(fs.cambios.isEmpty());
    QVERIFY(fs.nodos == tras);
}

// --- Filesystem real -----------------------------------------------------------

void TestMigracionRaizPaquetes::realMigraPadreConservaInodesYRecupera()
{
    HomeReal h;
    QVERIFY(h.tmp.isValid());
    QVERIFY(h.poblarLegado());
    QVERIFY(QFile::setPermissions(h.legado, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                                | QFileDevice::ReadGroup | QFileDevice::ExeGroup));
    const QString zipViejo = h.legadaRaiz() + QLatin1Char('/') + h.carpeta + QStringLiteral("/p1.zip");
    const QString partViejo = h.legadaRaiz() + QLatin1Char('/') + h.carpeta + QStringLiteral("/.p2.zip.0123456789abcdef.part");
    const quint64 inodeZip = inode(zipViejo);
    const quint64 inodePart = inode(partViejo);
    const QStringList arbolAntes = huella(h.legado);
    QVERIFY(inodeZip != 0 && inodePart != 0);

    const auto r = h.migrar();
    QCOMPARE(r.estado, E::MigradaPadre);
    QCOMPARE(r.raizEfectiva, h.nuevaRaiz());
    QVERIFY(!QFileInfo::exists(h.legado));
    // Mismo arbol (rutas relativas, tipos e inodes): nada copiado ni borrado.
    QCOMPARE(huella(h.nuevo), arbolAntes);
    const QString zip = h.nuevaRaiz() + QLatin1Char('/') + h.carpeta + QStringLiteral("/p1.zip");
    QCOMPARE(inode(zip), inodeZip);
    QCOMPARE(inode(h.nuevaRaiz() + QLatin1Char('/') + h.carpeta + QStringLiteral("/.p2.zip.0123456789abcdef.part")),
             inodePart);
    QCOMPARE(leer(zip), kCentinela);
    QCOMPARE(modo(h.nuevo), 0750u); // renombrado: conserva sus permisos

    // La recuperacion escanea la raiz efectiva y limpia solo temporales propios.
    FilesystemPackageStorage storage(r.raizEfectiva);
    const auto hallazgos = storage.escanearRecuperacion();
    QVERIFY(hallazgos);
    QCOMPARE(hallazgos.valor().size(), 2);
    const HallazgoFilesystem temporal = hallazgos.valor().at(0);
    QCOMPARE(temporal.tipo, TipoHallazgo::Temporal);
    QCOMPARE(hallazgos.valor().at(1).tipo, TipoHallazgo::Final);
    QVERIFY(storage.eliminarTemporal(temporal));
    QCOMPARE(QDir(h.nuevaRaiz() + QLatin1Char('/') + h.carpeta).entryList(QDir::Files | QDir::Hidden),
             (QStringList{QStringLiteral("LEEME.txt"), QStringLiteral("p1.zip")}));
    QCOMPARE(inode(zip), inodeZip);
    QCOMPARE(storage.existeArchivoFinal(h.carpeta + QStringLiteral("/p1.zip")).valor(), true);

    // Segundo arranque: idempotente.
    const QStringList trasMigrar = huella(h.home);
    QCOMPARE(h.migrar().estado, E::YaNueva);
    QCOMPARE(huella(h.home), trasMigrar);
}

void TestMigracionRaizPaquetes::realMigraSoloPaquetesConservaPermisos()
{
    HomeReal h;
    QVERIFY(h.poblarLegado());
    QVERIFY(escribir(h.legado + QStringLiteral("/notas.txt"), QByteArrayLiteral("del usuario")));
    QVERIFY(QFile::setPermissions(h.legadaRaiz(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                      | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                                      | QFileDevice::ExeGroup));
    const quint64 inodeNotas = inode(h.legado + QStringLiteral("/notas.txt"));
    const QStringList paquetesAntes = huella(h.legadaRaiz());

    const auto r = h.migrar();
    QCOMPARE(r.estado, E::MigradaPaquetes);
    QCOMPARE(huella(h.nuevaRaiz()), paquetesAntes);
    QCOMPARE(modo(h.nuevo), 0700u);       // nuevo
    QCOMPARE(modo(h.nuevaRaiz()), 0750u); // renombrado
    QCOMPARE(QDir(h.legado).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
             QStringList{QStringLiteral("notas.txt")});
    QCOMPARE(inode(h.legado + QStringLiteral("/notas.txt")), inodeNotas);
    QCOMPARE(h.migrar().estado, E::YaNueva);
}

void TestMigracionRaizPaquetes::realEnlaceSeRenombraObjetivoIntacto()
{
    HomeReal h;
    const QString externo = h.tmp.filePath(QStringLiteral("Externo/SAT"));
    QVERIFY(escribir(externo + QStringLiteral("/paquetes/") + h.carpeta + QStringLiteral("/p1.zip"), kCentinela));
    QVERIFY(QFile::link(externo, h.legado));
    const QStringList objetivoAntes = huella(externo);

    const auto r = h.migrar();
    QCOMPARE(r.estado, E::MigradaPadre);
    QVERIFY(!QFileInfo(h.legado).isSymLink() && !QFileInfo::exists(h.legado));
    QVERIFY(QFileInfo(h.nuevo).isSymLink());
    QCOMPARE(QFileInfo(h.nuevo).symLinkTarget(), QFileInfo(externo).absoluteFilePath());
    QCOMPARE(huella(externo), objetivoAntes);
    QCOMPARE(leer(h.nuevaRaiz() + QLatin1Char('/') + h.carpeta + QStringLiteral("/p1.zip")), kCentinela);
}

void TestMigracionRaizPaquetes::realEnlaceConOtrasCosasObjetivoIntacto()
{
    HomeReal h;
    const QString externo = h.tmp.filePath(QStringLiteral("Externo/SAT"));
    QVERIFY(escribir(externo + QStringLiteral("/paquetes/") + h.carpeta + QStringLiteral("/p1.zip"), kCentinela));
    QVERIFY(escribir(externo + QStringLiteral("/notas.txt"), QByteArrayLiteral("del usuario")));
    QVERIFY(QFile::link(externo, h.legado));
    const QStringList objetivoAntes = huella(externo);

    const auto r = h.migrar();
    QCOMPARE(r.estado, E::MigradaPadre);
    QVERIFY(!QFileInfo::exists(h.legado) && !QFileInfo(h.legado).isSymLink());
    QVERIFY(QFileInfo(h.nuevo).isSymLink());
    QCOMPARE(QFileInfo(h.nuevo).symLinkTarget(), QFileInfo(externo).absoluteFilePath());
    QCOMPARE(huella(externo), objetivoAntes); // paquetes sigue dentro del objetivo
    QCOMPARE(leer(h.nuevaRaiz() + QLatin1Char('/') + h.carpeta + QStringLiteral("/p1.zip")), kCentinela);
}

void TestMigracionRaizPaquetes::realEnlaceSinPaquetesNoSeToca()
{
    HomeReal h;
    const QString externo = h.tmp.filePath(QStringLiteral("Externo/SAT"));
    QVERIFY(escribir(externo + QStringLiteral("/notas.txt"), QByteArrayLiteral("del usuario")));
    QVERIFY(QFile::link(externo, h.legado));
    const QStringList objetivoAntes = huella(externo);

    const auto r = h.migrar();
    QCOMPARE(r.estado, E::Nueva);
    QCOMPARE(r.raizEfectiva, h.nuevaRaiz());
    QVERIFY(!r.aviso);
    QVERIFY(QFileInfo(h.legado).isSymLink());
    QCOMPARE(QFileInfo(h.legado).symLinkTarget(), QFileInfo(externo).absoluteFilePath());
    QCOMPARE(huella(externo), objetivoAntes);
    QVERIFY(QFileInfo(h.nuevaRaiz()).isDir() && !QFileInfo(h.nuevo).isSymLink());
    QCOMPARE(modo(h.nuevaRaiz()), 0700u);
    QCOMPARE(h.migrar().estado, E::YaNueva);
    QVERIFY(QFileInfo(h.legado).isSymLink());
}

void TestMigracionRaizPaquetes::realRenameExclNoReemplazaDirectorioVacio()
{
    QTemporaryDir tmp;
    const QString origen = tmp.filePath(QStringLiteral("origen"));
    const QString destino = tmp.filePath(QStringLiteral("destino"));
    QVERIFY(escribir(origen + QStringLiteral("/a.zip"), kCentinela));
    QVERIFY(QDir().mkdir(destino));
    PosixFileOps ops;
    QCOMPARE(ops.renombrarSinReemplazo(QFile::encodeName(origen), QFile::encodeName(destino)), EEXIST);
    QVERIFY(QFileInfo::exists(origen + QStringLiteral("/a.zip")));
    QCOMPARE(ops.eliminarDirectorioVacio(QFile::encodeName(origen)), ENOTEMPTY);
    QCOMPARE(ops.eliminarDirectorioVacio(QFile::encodeName(destino)), 0);
    QCOMPARE(ops.renombrarSinReemplazo(QFile::encodeName(origen), QFile::encodeName(destino)), 0);
    QCOMPARE(leer(destino + QStringLiteral("/a.zip")), kCentinela);
}

void TestMigracionRaizPaquetes::realConflictoNoTocaNada()
{
    HomeReal h;
    QVERIFY(h.poblarLegado());
    QVERIFY(escribir(h.nuevaRaiz() + QStringLiteral("/otro.zip"), QByteArrayLiteral("otro")));
    const QStringList antes = huella(h.home);
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("^raiz de paquetes: usa_legada_por_conflicto")));
    const auto r = h.migrar();
    QCOMPARE(r.estado, E::UsaLegadaPorConflicto);
    QCOMPARE(r.raizEfectiva, h.legadaRaiz());
    QVERIFY(r.aviso);
    QCOMPARE(huella(h.home), antes);
}

void TestMigracionRaizPaquetes::realLogSinRutasRfcNiZip()
{
    {
        QMutexLocker b(&g_mutex);
        g_logs.clear();
    }
    g_anterior = qInstallMessageHandler(capturar);
    QLoggingCategory::setFilterRules(QStringLiteral("satcfdi.arranque.info=true"));
    {
        HomeReal ok;
        QVERIFY(ok.poblarLegado());
        QCOMPARE(ok.migrar().estado, E::MigradaPadre);
        HomeReal conflicto;
        QVERIFY(conflicto.poblarLegado());
        QVERIFY(escribir(conflicto.nuevaRaiz() + QStringLiteral("/otro.zip"), QByteArrayLiteral("otro")));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("^raiz de paquetes: usa_legada_por_conflicto")));
        QCOMPARE(conflicto.migrar().estado, E::UsaLegadaPorConflicto);
        HomeReal sinHome; // home inexistente: mkdir falla
        sinHome.home = sinHome.tmp.filePath(QStringLiteral("no-existe"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^raiz de paquetes: nueva sin crear")));
        QCOMPARE(sinHome.migrar().estado, E::Nueva);

        qInstallMessageHandler(g_anterior);
        QLoggingCategory::setFilterRules(QString());
        QMutexLocker b(&g_mutex);
        QVERIFY(g_logs.size() >= 3);
        for (const QString& linea : std::as_const(g_logs)) {
            for (const QString& prohibido : {ok.tmp.path(), conflicto.tmp.path(), sinHome.tmp.path(),
                                             QDir::homePath(), kRfc, kUuid, QStringLiteral("p1.zip"),
                                             QStringLiteral(".part"), QStringLiteral("otro.zip")}) {
                QVERIFY2(!linea.contains(prohibido), qPrintable(linea));
            }
        }
    }
}

// --- Bootstrap -------------------------------------------------------------------

void TestMigracionRaizPaquetes::bootstrapMigraEnHiloDeEsDespuesDeLaBase()
{
    QTemporaryDir tmp;
    QStringList orden;
    QThread* hiloBase = nullptr;
    QThread* hiloMigrador = nullptr;
    int creaciones = 0;
    AppBootstrapper::Opciones o;
    o.directorioDatos = tmp.path();
    o.inicializador = [&](const QString& ruta) {
        orden.append(QStringLiteral("base"));
        hiloBase = QThread::currentThread();
        return inicializarBaseSqlite(ruta);
    };
    o.crearRaizPaquetes = [&](const QString&) { ++creaciones; };
    FileOpsSimulado fs = conLegado();
    o.migradorRaizPaquetes = [&] {
        orden.append(QStringLiteral("migrador"));
        hiloMigrador = QThread::currentThread();
        return MigracionRaizPaquetes(QString::fromLatin1(H), fs).resolver();
    };
    const auto a = AppBootstrapper(o).preparar();
    QVERIFY(a);
    QCOMPARE(orden, (QStringList{QStringLiteral("base"), QStringLiteral("migrador")}));
    QVERIFY(hiloMigrador == hiloBase);
    QVERIFY(hiloMigrador != QThread::currentThread());
    QCOMPARE(creaciones, 0); // con migrador no se usa la creacion directa
    QCOMPARE(a.valor().resolucionRaizPaquetes.estado, E::MigradaPadre);
    QCOMPARE(a.valor().raizPaquetes, s(NP));
    QCOMPARE(a.valor().resolucionRaizPaquetes.raizEfectiva, s(NP));
}

void TestMigracionRaizPaquetes::bootstrapSinBaseNoMigra()
{
    QTemporaryDir tmp;
    int migraciones = 0;
    AppBootstrapper::Opciones o;
    o.directorioDatos = tmp.path();
    o.inicializador = [](const QString&) {
        return Resultado<InformeInicializacionSqlite, ErrorPersistencia>::fallo(
            ErrorPersistencia::de(ErrorPersistencia::Tipo::Interno, QStringLiteral("falla")));
    };
    o.migradorRaizPaquetes = [&] {
        ++migraciones;
        return ResolucionRaizPaquetes{};
    };
    QVERIFY(!AppBootstrapper(o).preparar());
    QCOMPARE(migraciones, 0);
}

void TestMigracionRaizPaquetes::secundariaNoMigra()
{
    QVERIFY(m_canal.isValid());
    const QString nombre = m_canal.filePath(QStringLiteral("c"));
    SingleInstanceCoordinator primaria(nombre);
    QCOMPARE(primaria.adquirir(), SingleInstanceCoordinator::Rol::Primary);

    QTemporaryDir tmp;
    int migraciones = 0;
    AppBootstrapper::Opciones o;
    o.directorioDatos = tmp.filePath(QStringLiteral("datos"));
    o.migradorRaizPaquetes = [&] {
        ++migraciones;
        return ResolucionRaizPaquetes{};
    };
    SingleInstanceCoordinator segunda(nombre);
    const PreparacionProceso p = prepararProceso(segunda, AppBootstrapper(o));
    QCOMPARE(p.tipo, PreparacionProceso::Tipo::Secundaria);
    QCOMPARE(migraciones, 0);

    // La primaria (otro canal) si migra, una vez, dentro de prepararProceso.
    SingleInstanceCoordinator otra(m_canal.filePath(QStringLiteral("d")));
    FileOpsSimulado fs = conLegado();
    o.migradorRaizPaquetes = [&] {
        ++migraciones;
        return MigracionRaizPaquetes(QString::fromLatin1(H), fs).resolver();
    };
    const PreparacionProceso lista = prepararProceso(otra, AppBootstrapper(o));
    QCOMPARE(lista.tipo, PreparacionProceso::Tipo::Lista);
    QCOMPARE(migraciones, 1);
    QCOMPARE(lista.arranque.resolucionRaizPaquetes.estado, E::MigradaPadre);
    QCOMPARE(lista.arranque.raizPaquetes, s(NP));
}

void TestMigracionRaizPaquetes::dataDirNoTocaHome()
{
    HomeReal h;
    QVERIFY(h.poblarLegado());
    const QStringList homeAntes = huella(h.home);
    QTemporaryDir datos;
    AppBootstrapper::Opciones o;
    o.directorioDatos = datos.filePath(QStringLiteral("dd"));
    AppBootstrapper::configurarRaizPaquetes(o, h.home);
    QVERIFY(!o.migradorRaizPaquetes);
    QCOMPARE(o.raizPaquetes, QDir(o.directorioDatos).absoluteFilePath(QStringLiteral("paquetes")));
    const auto a = AppBootstrapper(o).preparar();
    QVERIFY(a);
    QCOMPARE(a.valor().resolucionRaizPaquetes.estado, E::NoAplica);
    QVERIFY(!a.valor().resolucionRaizPaquetes.aviso);
    QCOMPARE(a.valor().raizPaquetes, QDir(o.directorioDatos).absoluteFilePath(QStringLiteral("paquetes")));
    QVERIFY(QFileInfo(a.valor().raizPaquetes).isDir());
    QCOMPARE(huella(h.home), homeAntes);
    QVERIFY(!QFileInfo::exists(h.nuevo));
}

void TestMigracionRaizPaquetes::sinDataDirConfiguraMigrador()
{
    HomeReal h;
    QVERIFY(h.poblarLegado());
    AppBootstrapper::Opciones o;
    AppBootstrapper::configurarRaizPaquetes(o, h.home);
    QVERIFY(o.migradorRaizPaquetes);
    QCOMPARE(o.raizPaquetes, h.nuevaRaiz());
    // Solo la base va a un directorio temporal (no a AppDataLocation).
    QTemporaryDir datos;
    o.directorioDatos = datos.path();
    const auto a = AppBootstrapper(o).preparar();
    QVERIFY(a);
    QCOMPARE(a.valor().resolucionRaizPaquetes.estado, E::MigradaPadre);
    QCOMPARE(a.valor().raizPaquetes, h.nuevaRaiz());
    QVERIFY(QFileInfo::exists(h.nuevaRaiz() + QLatin1Char('/') + h.carpeta + QStringLiteral("/p1.zip")));
}

int main(int argc, char* argv[])
{
    // Red de seguridad: ninguna prueba toca rutas reales del usuario.
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    configurarIdentidadAplicacion();
    TestMigracionRaizPaquetes prueba;
    return QTest::qExec(&prueba, argc, argv);
}

#include "TestMigracionRaizPaquetes.moc"
