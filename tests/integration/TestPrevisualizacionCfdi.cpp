// T015.4 (integracion): PrevisualizacionCfdiService con el composition root
// real, SQLite real, LectorZipLibzip real, RenderizadorPdfCfdi real y
// AlmacenPdfCfdiFs real sobre ZIP sinteticos (libzip):
// - PDF real en <datos>/cache/cfdi-pdf, acierto de cache y regeneracion si se borra;
// - ZIP reemplazado con otro CFDI en esa posicion -> se reindexa y OrigenNoDisponible;
// - ZIP faltante -> OrigenNoDisponible y el indice lo registra;
// - cache no escribible -> temporal; "Guardar PDF..." atomico;
// - logs sin datos fiscales ni rutas (D11).

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"

#include "application/cfdi/CatalogoCfdiService.h"
#include "application/cfdi/IndexadorCfdi.h"
#include "application/cfdi/PrevisualizacionCfdiService.h"
#include "domain/solicitudes/DedupKey.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"
#include "ports/storage/RutaPaquete.h"

#include "fakes/FakeProgramador.h"
#include "fakes/FakeSatGateway.h"
#include "fakes/FakeSecretStore.h"
#include "fakes/ZipCfdiSintetico.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QPdfDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <algorithm>
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

QStringList g_logs;
QMutex g_mutexLogs;
QtMessageHandler g_anterior = nullptr;
void capturar(QtMsgType tipo, const QMessageLogContext& c, const QString& m)
{
    {
        QMutexLocker b(&g_mutexLogs);
        g_logs.append(m);
    }
    if (g_anterior) {
        g_anterior(tipo, c, m);
    }
}

// Base con una solicitud Terminada y un paquete Descargado cuyo ZIP (3 CFDI)
// existe en la raiz de paquetes.
struct Base {
    QTemporaryDir tmp;
    ArranquePreparado arranque;
    SolicitudId solicitud;
    QString paquete;
    QString zip;

    bool preparar()
    {
        AppBootstrapper::Opciones o;
        o.directorioDatos = tmp.path();
        const auto a = AppBootstrapper(o).preparar();
        if (!a) {
            return false;
        }
        arranque = a.valor();
        const QString perfil = uuid::generarCanonico();
        solicitud = SolicitudId::generar();
        paquete = uuid::generarCanonico();
        const QString ahora = QStringLiteral("2026-10-01T12:00:00.000Z");
        const auto ruta = rutapaquete::derivarRutaRelativa({kRfc, kFechaInicial, solicitud.texto(), QStringLiteral("PAQ_1")});
        if (!ruta) {
            return false;
        }
        zip = QDir(arranque.raizPaquetes).filePath(ruta.valor());
        if (!QDir().mkpath(QFileInfo(zip).absolutePath()) || !fakes::escribirArchivo(zip, fakes::zipConCfdi(1, 3))) {
            return false;
        }
        const QString sql =
            QStringLiteral(
                "INSERT INTO perfil_sat (id, rfc, nombre, activo, creado_en, actualizado_en) VALUES "
                "(%1, %2, 'Perfil PDF', 1, %3, %3);"
                "INSERT INTO solicitud_masiva (id, perfil_sat_id, id_solicitud_sat, tipo_cfdi, operacion_sat, "
                "rfc_solicitante, rfc_emisor, fecha_inicial_sat, fecha_final_sat, dedup_key, estado_local, "
                "cod_estatus_solicitud, estado_solicitud_sat, creada_en, envio_iniciado_en, enviada_en, "
                "ultima_verificacion_en) VALUES (%4, %1, %5, 'emitidos', 'SolicitaDescargaEmitidos', %2, %2, "
                "%6, '2026-09-30T23:59:59', %7, 'Enviada', '5000', 'Terminada', %3, %3, %3, %3);")
                .arg(citar(perfil), citar(kRfc), citar(ahora), citar(solicitud.texto()),
                     citar(solicitud.texto().toUpper()), citar(kFechaInicial),
                     citar(DedupKey::calcularV1("t0154").texto()))
            + QStringLiteral("INSERT INTO paquete_solicitud (id, solicitud_masiva_id, id_paquete_sat, estado_descarga, "
                             "ruta_local, disponible_en, descarga_iniciada_en, descargado_en) VALUES (%1, %2, "
                             "'PAQ_1', 'Descargado', %3, %4, %4, %4);")
                  .arg(citar(paquete), citar(solicitud.texto()), citar(ruta.valor()), citar(ahora));
        return sqlite(arranque.rutaBase, sql).isEmpty();
    }
};

struct Dependencias {
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone::UTC)};
    fakes::FakeProgramador programadorEjecutor{reloj};
    fakes::FakeProgramador programadorWorker{reloj};
    fakes::FakeProgramador programadorVencimiento{reloj};
    fakes::FakeSecretStore secretos;
    fakes::FakeSatGateway gateway;

    Dependencias() { gateway.ahora = reloj.funcion(); }

    std::unique_ptr<AppCompositionRoot> root(const ArranquePreparado& a, OpcionesMonitoreo m = {})
    {
        m.satGateway = &gateway;
        m.reloj = reloj.funcion();
        m.programadorEjecutor = &programadorEjecutor;
        m.programadorWorker = &programadorWorker;
        m.programadorVencimiento = &programadorVencimiento;
        m.raizPaquetes = a.raizPaquetes;
        return std::make_unique<AppCompositionRoot>(a.rutaBase, secretos, m);
    }
};

bool inactivo(AppCompositionRoot& root)
{
    QTest::qWait(20);
    return QTest::qWaitFor([&] { return !root.indexadorCfdi().ocupado(); }, kEspera);
}

QList<CfdiListado> listar(AppCompositionRoot& root, const SolicitudId& id)
{
    const auto r = esperar(root.catalogoCfdi().listar(id, {}));
    return r && r->esExito() ? r->valor() : QList<CfdiListado>{};
}

QString textoPdf(const QString& ruta)
{
    QPdfDocument doc;
    if (doc.load(ruta) != QPdfDocument::Error::None) {
        return {};
    }
    QString t;
    for (int p = 0; p < doc.pageCount(); ++p) {
        t += doc.getAllText(p).text().simplified() + QLatin1Char(' ');
    }
    return t;
}

} // namespace

class TestPrevisualizacionCfdiIntegracion : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(
            "^(?!Populating font family aliases took|This plugin does not support ).*")));
    }

    void generaPdfRealConCacheYGuardar();
    void zipReemplazadoConOtroCfdiEsOrigenNoDisponible();
    void zipFaltanteEsOrigenNoDisponible();
    void cacheNoEscribibleUsaTemporal();
};

void TestPrevisualizacionCfdiIntegracion::generaPdfRealConCacheYGuardar()
{
    g_anterior = qInstallMessageHandler(capturar);
    Base base;
    QVERIFY(base.preparar());
    Dependencias d;
    auto root = d.root(base.arranque);
    root->iniciarMonitoreo();
    QVERIFY(inactivo(*root));
    // Con directorio de datos explicito la cache vive dentro de el (D7).
    QCOMPARE(root->directorioCachePdf(), QDir::cleanPath(base.tmp.filePath(QStringLiteral("cache/cfdi-pdf"))));
    QVERIFY(!root->directorioTemporalPdf().isEmpty());

    const QList<CfdiListado> lista = listar(*root, base.solicitud);
    QCOMPARE(lista.size(), 3);
    const CfdiListado& cfdi = lista.first();
    auto r = esperar(root->previsualizacionCfdi().generar(base.solicitud, cfdi.paqueteId, cfdi.posicion));
    QVERIFY(r && r->esExito());
    const QString ruta = r->valor().rutaArchivo;
    QVERIFY(ruta.startsWith(root->directorioCachePdf() + QLatin1Char('/')));
    QVERIFY(QFileInfo(ruta).fileName().startsWith(cfdi.resumen.uuid));
    QCOMPARE(r->valor().nombreSugerido,
             QStringLiteral("CFDI-S%1-%2.pdf").arg(cfdi.resumen.folio, cfdi.resumen.uuid.left(8)));
    const QString texto = textoPdf(ruta);
    QVERIFY(texto.contains(cfdi.resumen.uuid));
    QVERIFY(texto.contains(QStringLiteral("EMISOR SINTETICO")));
    QVERIFY(texto.contains(QStringLiteral("116.00 MXN")));

    // Acierto de cache (misma ruta) y regeneracion si se borra.
    const QDateTime antes = QFileInfo(ruta).birthTime();
    r = esperar(root->previsualizacionCfdi().generar(base.solicitud, cfdi.paqueteId, cfdi.posicion));
    QVERIFY(r && r->esExito());
    QCOMPARE(r->valor().rutaArchivo, ruta);
    QCOMPARE(QFileInfo(ruta).birthTime(), antes);
    QVERIFY(QFile::remove(ruta));
    r = esperar(root->previsualizacionCfdi().generar(base.solicitud, cfdi.paqueteId, cfdi.posicion));
    QVERIFY(r && r->esExito());
    QVERIFY(QFile::exists(ruta));

    // Guardar PDF... : copia identica en el destino elegido.
    const QString destino = base.tmp.filePath(QStringLiteral("Documentos/") + r->valor().nombreSugerido);
    QVERIFY(QDir().mkpath(QFileInfo(destino).absolutePath()));
    const auto g = esperar(root->previsualizacionCfdi().guardarComo(ruta, destino));
    QVERIFY(g && g->esExito());
    QFile a(ruta);
    QFile b(destino);
    QVERIFY(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
    QCOMPARE(a.readAll(), b.readAll());
    // Destino no escribible: Fallo.
    const auto f = esperar(root->previsualizacionCfdi().guardarComo(ruta, base.tmp.filePath(QStringLiteral("no/x.pdf"))));
    QVERIFY(f && !f->esExito());
    QCOMPARE(f->error().tipo, ErrorPrevisualizacion::Tipo::Fallo);

    root.reset();
    qInstallMessageHandler(g_anterior);
    QMutexLocker bloqueo(&g_mutexLogs);
    for (const QString& linea : std::as_const(g_logs)) {
        for (const QString& p : {kRfc, QStringLiteral("XAXX010101000"), cfdi.resumen.uuid.left(8),
                                 QStringLiteral("116.00"), QStringLiteral("EMISOR SINTETICO"), base.tmp.path(),
                                 base.paquete}) {
            QVERIFY2(!linea.contains(p), qPrintable(linea));
        }
    }
}

void TestPrevisualizacionCfdiIntegracion::zipReemplazadoConOtroCfdiEsOrigenNoDisponible()
{
    Base base;
    QVERIFY(base.preparar());
    Dependencias d;
    auto root = d.root(base.arranque);
    root->iniciarMonitoreo();
    QVERIFY(inactivo(*root));
    const CfdiListado viejo = listar(*root, base.solicitud).last();

    // El ZIP se reemplaza por otro (otros CFDI) sin que el indice lo sepa.
    QVERIFY(fakes::escribirArchivo(base.zip, fakes::zipConCfdi(100, 4)));
    const auto r = esperar(root->previsualizacionCfdi().generar(base.solicitud, viejo.paqueteId, viejo.posicion));
    // Se reindexo antes de generar y en esa posicion ya hay otro CFDI: no se
    // genera PDF (ni del ZIP anterior ni de un CFDI distinto del elegido).
    QVERIFY(r && !r->esExito());
    QCOMPARE(r->error().tipo, ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
    QVERIFY(inactivo(*root));
    const QList<CfdiListado> nueva = listar(*root, base.solicitud);
    QCOMPARE(nueva.size(), 4);
    QVERIFY(QDir(root->directorioCachePdf()).entryList(QDir::Files).isEmpty());
    // Con la lista recargada, el CFDI nuevo de esa posicion si se previsualiza.
    const CfdiListado actual = *std::find_if(nueva.cbegin(), nueva.cend(), [&](const CfdiListado& c) {
        return c.paqueteId == viejo.paqueteId && c.posicion == viejo.posicion;
    });
    const auto r2 = esperar(root->previsualizacionCfdi().generar(base.solicitud, actual.paqueteId, actual.posicion));
    QVERIFY(r2 && r2->esExito());
    QVERIFY(QFileInfo(r2->valor().rutaArchivo).fileName().startsWith(actual.resumen.uuid));
    root.reset();
}

void TestPrevisualizacionCfdiIntegracion::zipFaltanteEsOrigenNoDisponible()
{
    Base base;
    QVERIFY(base.preparar());
    Dependencias d;
    auto root = d.root(base.arranque);
    root->iniciarMonitoreo();
    QVERIFY(inactivo(*root));
    const CfdiListado cfdi = listar(*root, base.solicitud).first();
    QVERIFY(QFile::remove(base.zip));
    const auto r = esperar(root->previsualizacionCfdi().generar(base.solicitud, cfdi.paqueteId, cfdi.posicion));
    QVERIFY(r && !r->esExito());
    QCOMPARE(r->error().tipo, ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
    // El indice registra el paquete como no disponible.
    QVERIFY(QTest::qWaitFor(
        [&] {
            const auto s = esperar(root->catalogoCfdi().resumen(base.solicitud));
            return s && s->esExito() && s->valor().paquetesNoDisponibles == 1;
        },
        kEspera));
    QVERIFY(QDir(root->directorioCachePdf()).entryList(QDir::Files).isEmpty());
    root.reset();
}

void TestPrevisualizacionCfdiIntegracion::cacheNoEscribibleUsaTemporal()
{
    Base base;
    QVERIFY(base.preparar());
    QVERIFY(fakes::escribirArchivo(base.tmp.filePath(QStringLiteral("bloqueo")), "x"));
    Dependencias d;
    OpcionesMonitoreo m;
    m.directorioCachePdf = base.tmp.filePath(QStringLiteral("bloqueo/cfdi-pdf")); // bajo un archivo: no se crea
    m.directorioTemporalPdf = base.tmp.filePath(QStringLiteral("temporal-app"));
    auto root = d.root(base.arranque, m);
    root->iniciarMonitoreo();
    QVERIFY(inactivo(*root));
    const CfdiListado cfdi = listar(*root, base.solicitud).first();
    const auto r = esperar(root->previsualizacionCfdi().generar(base.solicitud, cfdi.paqueteId, cfdi.posicion));
    QVERIFY(r && r->esExito());
    QVERIFY(r->valor().rutaArchivo.startsWith(root->directorioTemporalPdf() + QLatin1Char('/')));
    QVERIFY(QFile::exists(r->valor().rutaArchivo));
    root.reset();
}

#include "TestPrevisualizacionCfdi.moc"

int ejecutarTestPrevisualizacionCfdi(int argc, char* argv[])
{
    TestPrevisualizacionCfdiIntegracion prueba;
    return QTest::qExec(&prueba, argc, argv);
}
