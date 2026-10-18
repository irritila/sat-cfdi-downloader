// T019 (D4, D8, integracion): la raiz efectiva que devuelve el bootstrap
// (migracion exitosa o raiz vieja por conflicto) alimenta todo el root real:
// existencia del ZIP, recuperacion, indice y PDF, "Mostrar en Finder" y
// "Abrir carpeta de paquetes". El aviso llega a AppCompositionRoot. Home
// temporal inyectado; SQLite real; FakeSatGateway y FakeOSIntegration.

#include "app_core/AccesoFinder.h"
#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"

#include "application/cfdi/CatalogoCfdiService.h"
#include "application/cfdi/IndexadorCfdi.h"
#include "application/cfdi/PrevisualizacionCfdiService.h"
#include "application/operaciones/OperacionExecutor.h"
#include "domain/solicitudes/DedupKey.h"
#include "ports/storage/RutaPaquete.h"
#include "presentation/viewmodels/AvisoRaizPaquetesViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"
#include "presentation/viewmodels/SolicitudDetailViewModel.h"

#include "fakes/FakeOSIntegration.h"
#include "fakes/FakeProgramador.h"
#include "fakes/FakeSatGateway.h"
#include "fakes/FakeSecretStore.h"
#include "fakes/ZipCfdiSintetico.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>
#include <optional>

using namespace satcfdi;

namespace {

constexpr int kEspera = 20000;
const QString kRfc = QStringLiteral("CCC030303CCC");
const QString kFechaInicial = QStringLiteral("2026-09-01T00:00:00");

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, kEspera) || f.isCanceled() || f.resultCount() == 0) {
        return std::nullopt;
    }
    return f.result();
}

QString sqlite(const QString& rutaBase, const QString& sql)
{
    QProcess p;
    p.start(QStringLiteral("/usr/bin/sqlite3"), {rutaBase, sql});
    if (!p.waitForFinished(10000) || p.exitCode() != 0) {
        return QStringLiteral("<error>") + QString::fromUtf8(p.readAllStandardError());
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString citar(const QString& v)
{
    return QLatin1Char('\'') + QString(v).replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\'');
}

// Base (en un directorio de datos temporal) con una solicitud Terminada y un
// paquete Descargado cuyo ZIP (3 CFDI) vive en la raiz LEGADA de un home
// temporal: ~/SAT-CFDI-Downloader/paquetes/<ruta relativa>.
struct Escenario {
    QTemporaryDir tmp;
    QString home;
    QString datos;
    SolicitudId solicitud;
    QString rutaRelativa;

    QString legadaRaiz() const { return home + QStringLiteral("/SAT-CFDI-Downloader/paquetes"); }
    QString nuevaRaiz() const { return home + QStringLiteral("/Archivo-Fiscal/paquetes"); }

    bool preparar()
    {
        home = tmp.filePath(QStringLiteral("home"));
        datos = tmp.filePath(QStringLiteral("datos"));
        AppBootstrapper::Opciones o;
        o.directorioDatos = datos;
        const auto a = AppBootstrapper(o).preparar();
        if (!a) {
            return false;
        }
        const QString perfil = uuid::generarCanonico();
        solicitud = SolicitudId::generar();
        const QString paquete = uuid::generarCanonico();
        const QString ahora = QStringLiteral("2026-10-01T12:00:00.000Z");
        const auto ruta =
            rutapaquete::derivarRutaRelativa({kRfc, kFechaInicial, solicitud.texto(), QStringLiteral("PAQ_1")});
        if (!ruta) {
            return false;
        }
        rutaRelativa = ruta.valor();
        const QString zip = QDir(legadaRaiz()).filePath(rutaRelativa);
        if (!QDir().mkpath(QFileInfo(zip).absolutePath()) || !fakes::escribirArchivo(zip, fakes::zipConCfdi(1, 3))) {
            return false;
        }
        const QString sql =
            QStringLiteral(
                "INSERT INTO perfil_sat (id, rfc, nombre, activo, creado_en, actualizado_en) VALUES "
                "(%1, %2, 'Perfil T019', 1, %3, %3);"
                "INSERT INTO solicitud_masiva (id, perfil_sat_id, id_solicitud_sat, tipo_cfdi, operacion_sat, "
                "rfc_solicitante, rfc_emisor, fecha_inicial_sat, fecha_final_sat, dedup_key, estado_local, "
                "cod_estatus_solicitud, estado_solicitud_sat, creada_en, envio_iniciado_en, enviada_en, "
                "ultima_verificacion_en) VALUES (%4, %1, %5, 'emitidos', 'SolicitaDescargaEmitidos', %2, %2, "
                "%6, '2026-09-30T23:59:59', %7, 'Enviada', '5000', 'Terminada', %3, %3, %3, %3);")
                .arg(citar(perfil), citar(kRfc), citar(ahora), citar(solicitud.texto()),
                     citar(solicitud.texto().toUpper()), citar(kFechaInicial),
                     citar(DedupKey::calcularV1("t019").texto()))
            + QStringLiteral("INSERT INTO paquete_solicitud (id, solicitud_masiva_id, id_paquete_sat, estado_descarga, "
                             "ruta_local, disponible_en, descarga_iniciada_en, descargado_en) VALUES (%1, %2, "
                             "'PAQ_1', 'Descargado', %3, %4, %4, %4);")
                  .arg(citar(paquete), citar(solicitud.texto()), citar(rutaRelativa), citar(ahora));
        return sqlite(a.valor().rutaBase, sql).isEmpty();
    }

    // Segundo arranque: bootstrap con el migrador sobre el home temporal
    // (como main sin --data-dir) y la base en `datos`.
    std::optional<ArranquePreparado> arrancar() const
    {
        AppBootstrapper::Opciones o;
        AppBootstrapper::configurarRaizPaquetes(o, home);
        o.directorioDatos = datos;
        const auto a = AppBootstrapper(o).preparar();
        return a ? std::optional(a.valor()) : std::nullopt;
    }
};

struct Dependencias {
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone::UTC)};
    fakes::FakeProgramador programadorEjecutor{reloj};
    fakes::FakeProgramador programadorWorker{reloj};
    fakes::FakeProgramador programadorVencimiento{reloj};
    fakes::FakeSecretStore secretos;
    fakes::FakeSatGateway gateway;
    fakes::FakeOSIntegration os;

    Dependencias() { gateway.ahora = reloj.funcion(); }

    // Igual que main: raiz efectiva y aviso del ArranquePreparado.
    std::unique_ptr<AppCompositionRoot> root(const ArranquePreparado& a)
    {
        OpcionesMonitoreo m;
        m.satGateway = &gateway;
        m.reloj = reloj.funcion();
        m.programadorEjecutor = &programadorEjecutor;
        m.programadorWorker = &programadorWorker;
        m.programadorVencimiento = &programadorVencimiento;
        m.raizPaquetes = a.raizPaquetes;
        m.avisoRaizPaquetes = a.resolucionRaizPaquetes.aviso;
        auto r = std::make_unique<AppCompositionRoot>(a.rutaBase, secretos, m);
        r->accesoFinder().setOS(&os);
        return r;
    }
};

QString canonica(const QString& ruta)
{
    return QFileInfo(ruta).canonicalFilePath();
}

// Comprueba que todo lo que lee paquetes usa `raiz`.
void verificarRaizEfectiva(AppCompositionRoot& root, Dependencias& d, const Escenario& e, const QString& raiz)
{
    const QString zip = QDir(raiz).filePath(e.rutaRelativa);
    QVERIFY(QFileInfo::exists(zip));
    QCOMPARE(root.raizPaquetes(), raiz);

    // Recuperacion de arranque sobre la raiz efectiva: el ZIP se conserva.
    const auto rec = esperar(root.ejecutor().recuperar());
    QVERIFY(rec);
    QVERIFY(QFileInfo::exists(zip));

    // Existencia del ZIP (detalle real).
    SolicitudDetailViewModel* vm = root.viewModels().detalle();
    vm->cargar(e.solicitud.texto());
    QTRY_VERIFY_WITH_TIMEOUT(vm->cargada(), kEspera);
    QTRY_COMPARE_WITH_TIMEOUT(vm->paquetes().first().toMap().value(QStringLiteral("existencia")).toString(),
                              QStringLiteral("Presente"), kEspera);

    // Indice de CFDI y PDF leen el ZIP de la raiz efectiva.
    root.iniciarMonitoreo();
    QTest::qWait(20);
    QVERIFY(QTest::qWaitFor([&] { return !root.indexadorCfdi().ocupado(); }, kEspera));
    const auto lista = esperar(root.catalogoCfdi().listar(e.solicitud, {}));
    QVERIFY(lista && lista->esExito());
    QCOMPARE(lista->valor().size(), 3);
    const CfdiListado& cfdi = lista->valor().first();
    const auto pdf = esperar(root.previsualizacionCfdi().generar(e.solicitud, cfdi.paqueteId, cfdi.posicion));
    QVERIFY(pdf && pdf->esExito());
    QVERIFY(QFileInfo::exists(pdf->valor().rutaArchivo));

    // Finder: el ZIP y la carpeta de paquetes de la raiz efectiva.
    AccesoFinder& finder = root.accesoFinder();
    auto r = esperar(finder.mostrarPaquete(e.solicitud, QStringLiteral("PAQ_1")));
    QVERIFY(r && r->estado == ResultadoAccionFinder::Estado::Mostrado);
    QCOMPARE(canonica(d.os.peticionesFinder.last().ruta), canonica(zip));
    r = esperar(finder.abrirCarpetaPaquetes());
    QVERIFY(r && r->estado == ResultadoAccionFinder::Estado::Mostrado);
    QCOMPARE(d.os.peticionesFinder.last().metodo, QStringLiteral("abrirCarpetaEnFinder"));
    QCOMPARE(canonica(d.os.peticionesFinder.last().ruta), canonica(raiz));
}

} // namespace

class TestRaizPaquetesMigrada : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(
            "^(?!Populating font family aliases took|This plugin does not support |raiz de paquetes: ).*")));
    }

    void migracionExitosaUsaRaizNueva();
    void conflictoUsaRaizViejaYAvisa();
    void errorDeRenameUsaRaizViejaYAvisa();
};

void TestRaizPaquetesMigrada::migracionExitosaUsaRaizNueva()
{
    Escenario e;
    QVERIFY(e.preparar());
    const auto a = e.arrancar();
    QVERIFY(a);
    QCOMPARE(a->resolucionRaizPaquetes.estado, EstadoRaizPaquetes::MigradaPadre);
    QCOMPARE(a->raizPaquetes, e.nuevaRaiz());
    QVERIFY(!QFileInfo::exists(e.home + QStringLiteral("/SAT-CFDI-Downloader")));
    Dependencias d;
    auto root = d.root(*a);
    QVERIFY(!root->avisoRaizPaquetes());
    // Cableado al view model del aviso (corte 2): sin aviso.
    QVERIFY(!root->viewModels().avisoRaizPaquetes()->activo());
    verificarRaizEfectiva(*root, d, e, e.nuevaRaiz());
}

void TestRaizPaquetesMigrada::conflictoUsaRaizViejaYAvisa()
{
    Escenario e;
    QVERIFY(e.preparar());
    const QString ajeno = QDir(e.nuevaRaiz()).filePath(QStringLiteral("otro.txt"));
    QVERIFY(QDir().mkpath(e.nuevaRaiz()));
    QVERIFY(fakes::escribirArchivo(ajeno, QByteArrayLiteral("del usuario")));
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("^raiz de paquetes: usa_legada_por_conflicto")));
    const auto a = e.arrancar();
    QVERIFY(a);
    QCOMPARE(a->resolucionRaizPaquetes.estado, EstadoRaizPaquetes::UsaLegadaPorConflicto);
    QCOMPARE(a->raizPaquetes, e.legadaRaiz());
    Dependencias d;
    auto root = d.root(*a);
    QVERIFY(root->avisoRaizPaquetes());
    QCOMPARE(root->avisoRaizPaquetes()->motivo, AvisoRaizPaquetes::Motivo::Conflicto);
    QCOMPARE(root->avisoRaizPaquetes()->rutaEfectiva, QStringLiteral("~/SAT-CFDI-Downloader/paquetes"));
    QCOMPARE(root->avisoRaizPaquetes()->rutaNueva, QStringLiteral("~/Archivo-Fiscal/paquetes"));
    AvisoRaizPaquetesViewModel* vm = root->viewModels().avisoRaizPaquetes();
    QVERIFY(vm->activo());
    QCOMPARE(vm->motivo(), AvisoRaizPaquetesViewModel::Motivo::Conflicto);
    QCOMPARE(vm->rutaEfectiva(), QStringLiteral("~/SAT-CFDI-Downloader/paquetes"));
    QCOMPARE(vm->rutaNueva(), QStringLiteral("~/Archivo-Fiscal/paquetes"));
    verificarRaizEfectiva(*root, d, e, e.legadaRaiz());
    // La raiz nueva no se toco.
    QCOMPARE(QDir(e.nuevaRaiz()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
             QStringList{QStringLiteral("otro.txt")});
}

void TestRaizPaquetesMigrada::errorDeRenameUsaRaizViejaYAvisa()
{
    Escenario e;
    QVERIFY(e.preparar());
    // Home sin permiso de escritura: el rename del padre falla con EACCES.
    const QFileDevice::Permissions original = QFile::permissions(e.home);
    QVERIFY(QFile::setPermissions(e.home, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^raiz de paquetes: usa_legada_por_error")));
    const auto a = e.arrancar();
    QVERIFY(QFile::setPermissions(e.home, original));
    QVERIFY(a);
    QCOMPARE(a->resolucionRaizPaquetes.estado, EstadoRaizPaquetes::UsaLegadaPorError);
    QCOMPARE(a->raizPaquetes, e.legadaRaiz());
    QVERIFY(!QFileInfo::exists(e.home + QStringLiteral("/Archivo-Fiscal")));
    Dependencias d;
    auto root = d.root(*a);
    AvisoRaizPaquetesViewModel* vm = root->viewModels().avisoRaizPaquetes();
    QVERIFY(vm->activo());
    QCOMPARE(vm->motivo(), AvisoRaizPaquetesViewModel::Motivo::Error);
    QCOMPARE(vm->rutaEfectiva(), QStringLiteral("~/SAT-CFDI-Downloader/paquetes"));
    verificarRaizEfectiva(*root, d, e, e.legadaRaiz());
}

#include "TestRaizPaquetesMigrada.moc"

int ejecutarTestRaizPaquetesMigrada(int argc, char* argv[])
{
    TestRaizPaquetesMigrada prueba;
    return QTest::qExec(&prueba, argc, argv);
}
