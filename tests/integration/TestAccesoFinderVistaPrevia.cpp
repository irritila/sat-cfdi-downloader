// T015.4: AccesoFinder::abrirEnVistaPrevia sobre FakeOSIntegration y un
// filesystem real (QTemporaryDir). Solo se acepta un archivo regular cuya ruta
// canonica quede dentro de un directorio permitido; fuera, con `..`, por un
// enlace que escapa o sin configurar -> Fallido SIN llamar al SO; inexistente
// dentro -> NoEncontrado sin llamar al SO.
#include "app_core/AccesoFinder.h"
#include "application/paquetes/AccesoPaquetesService.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "domain/paquetes/PaquetePersistido.h"
#include "fakes/FakeOSIntegration.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace satcfdi;

namespace {

// Las acciones de Finder de T009.1 no se usan aqui.
class AccesoNulo final : public AccesoPaquetesService {
public:
    QFuture<ResolucionRevelable> resolverArchivoPaquete(const QString&) override { return nulo(); }
    QFuture<ResolucionRevelable> resolverCarpetaSolicitud(const SolicitudId&) override { return nulo(); }
    QFuture<ResolucionRevelable> resolverRaizPaquetes() override { return nulo(); }

private:
    static QFuture<ResolucionRevelable> nulo() { return QtFuture::makeReadyValueFuture(ResolucionRevelable{}); }
};

class PaquetesNulos final : public PaqueteSolicitudRepository {
public:
    Resultado<QList<PaquetePersistido>, ErrorPersistencia> listarVisiblesPorSolicitud(const SolicitudId&) override
    {
        return Resultado<QList<PaquetePersistido>, ErrorPersistencia>::exito({});
    }
    Resultado<QHash<SolicitudId, int>, ErrorPersistencia> contarVisiblesPorSolicitud() override
    {
        return Resultado<QHash<SolicitudId, int>, ErrorPersistencia>::exito({});
    }
    Resultado<QHash<SolicitudId, ConteoPaquetes>, ErrorPersistencia> contarVisiblesPorSolicitudYEstado() override
    {
        return Resultado<QHash<SolicitudId, ConteoPaquetes>, ErrorPersistencia>::exito({});
    }
    Resultado<int, ErrorPersistencia> marcarEliminadosPorSolicitud(const SolicitudId&, const QDateTime&) override
    {
        return Resultado<int, ErrorPersistencia>::exito(0);
    }
};

bool crearArchivo(const QString& ruta)
{
    QFile f(ruta);
    return f.open(QIODevice::WriteOnly) && f.write("%PDF-1.7\n") > 0;
}

ResultadoAccionFinder esperar(QFuture<ResultadoAccionFinder> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 2000) || f.resultCount() == 0) {
        return ResultadoAccionFinder{ResultadoAccionFinder::Estado::NoAplica, QStringLiteral("sin resultado")};
    }
    return f.result();
}

} // namespace

class TestAccesoFinderVistaPrevia : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_raiz; // contiene cache/, temporal/ y fuera/
    QString m_cache;
    QString m_temporal;
    QString m_fuera;
    AccesoNulo m_acceso;
    PaquetesNulos m_paquetes;
    PersistenceDispatcher m_dispatcher;
    std::unique_ptr<fakes::FakeOSIntegration> m_os;
    std::unique_ptr<AccesoFinder> m_finder;

    using Estado = ResultadoAccionFinder::Estado;

    QString enCache(const QString& nombre) const { return m_cache + QLatin1Char('/') + nombre; }

private slots:
    void initTestCase()
    {
        QVERIFY(m_raiz.isValid());
        // Canonica: en macOS el temporal vive bajo /var -> /private/var.
        const QString base = QFileInfo(m_raiz.path()).canonicalFilePath();
        m_cache = base + QStringLiteral("/cache");
        m_temporal = base + QStringLiteral("/temporal");
        m_fuera = base + QStringLiteral("/fuera");
        QVERIFY(QDir().mkpath(m_cache + QStringLiteral("/sub")));
        QVERIFY(QDir().mkpath(m_temporal));
        QVERIFY(QDir().mkpath(m_fuera));
        QVERIFY(QDir().mkpath(base + QStringLiteral("/cache-otro")));
        QVERIFY(crearArchivo(enCache(QStringLiteral("a.pdf"))));
        QVERIFY(crearArchivo(m_temporal + QStringLiteral("/t.pdf")));
        QVERIFY(crearArchivo(m_fuera + QStringLiteral("/secreto.pdf")));
        QVERIFY(crearArchivo(base + QStringLiteral("/cache-otro/b.pdf")));
        // Enlaces dentro de la cache: a un archivo de fuera, a un directorio de
        // fuera y a un archivo de la propia cache.
        QVERIFY(QFile::link(m_fuera + QStringLiteral("/secreto.pdf"), enCache(QStringLiteral("escapa.pdf"))));
        QVERIFY(QFile::link(m_fuera, enCache(QStringLiteral("dir-escapa"))));
        QVERIFY(QFile::link(enCache(QStringLiteral("a.pdf")), enCache(QStringLiteral("alias.pdf"))));
        // Directorio permitido que es a su vez un enlace (como /var en macOS).
        QVERIFY(QFile::link(m_cache, base + QStringLiteral("/cache-enlace")));
    }

    void init()
    {
        m_os = std::make_unique<fakes::FakeOSIntegration>();
        m_finder = std::make_unique<AccesoFinder>(m_acceso, m_dispatcher, m_paquetes);
        m_finder->setOS(m_os.get());
        m_finder->setDirectoriosVistaPrevia({m_cache, m_temporal});
    }

    void cleanup()
    {
        m_finder.reset();
        m_os.reset();
    }

    void dentroDeLaCacheAbreConRutaCanonica()
    {
        const auto r = esperar(m_finder->abrirEnVistaPrevia(enCache(QStringLiteral("a.pdf"))));
        QCOMPARE(r.estado, Estado::Mostrado);
        QVERIFY(r.mensaje.isEmpty());
        QCOMPARE(m_os->peticionesFinder.size(), 1);
        QCOMPARE(m_os->peticionesFinder.last().metodo, QStringLiteral("abrirEnVistaPrevia"));
        QCOMPARE(m_os->peticionesFinder.last().ruta, enCache(QStringLiteral("a.pdf")));
    }

    void dentroDelTemporal()
    {
        const auto r = esperar(m_finder->abrirEnVistaPrevia(m_temporal + QStringLiteral("/t.pdf")));
        QCOMPARE(r.estado, Estado::Mostrado);
        QCOMPARE(m_os->peticionesFinder.size(), 1);
    }

    void enlaceDentroDeLaCacheSeAbreComoSuDestinoCanonico()
    {
        const auto r = esperar(m_finder->abrirEnVistaPrevia(enCache(QStringLiteral("alias.pdf"))));
        QCOMPARE(r.estado, Estado::Mostrado);
        QCOMPARE(m_os->peticionesFinder.last().ruta, enCache(QStringLiteral("a.pdf")));
    }

    void directorioPermitidoPorEnlace()
    {
        // Configurado por el enlace; la ruta llega por el enlace o canonica.
        m_finder->setDirectoriosVistaPrevia({QFileInfo(m_cache).path() + QStringLiteral("/cache-enlace")});
        auto r = esperar(m_finder->abrirEnVistaPrevia(QFileInfo(m_cache).path()
                                                      + QStringLiteral("/cache-enlace/a.pdf")));
        QCOMPARE(r.estado, Estado::Mostrado);
        QCOMPARE(m_os->peticionesFinder.last().ruta, enCache(QStringLiteral("a.pdf")));
        r = esperar(m_finder->abrirEnVistaPrevia(enCache(QStringLiteral("a.pdf"))));
        QCOMPARE(r.estado, Estado::Mostrado);
        QCOMPARE(m_os->peticionesFinder.size(), 2);
    }

    void rechazosSinLlamarAlSO_data()
    {
        QTest::addColumn<QString>("ruta");
        const QString base = QFileInfo(m_cache).path();
        QTest::newRow("fuera") << m_fuera + QStringLiteral("/secreto.pdf");
        QTest::newRow("prefijo-enganoso") << base + QStringLiteral("/cache-otro/b.pdf");
        QTest::newRow("punto-punto-escapa") << m_cache + QStringLiteral("/../fuera/secreto.pdf");
        QTest::newRow("punto-punto-interno") << m_cache + QStringLiteral("/sub/../a.pdf");
        QTest::newRow("punto") << m_cache + QStringLiteral("/./a.pdf");
        QTest::newRow("doble-barra") << m_cache + QStringLiteral("//a.pdf");
        QTest::newRow("enlace-archivo-escapa") << enCache(QStringLiteral("escapa.pdf"));
        QTest::newRow("enlace-directorio-escapa") << enCache(QStringLiteral("dir-escapa/secreto.pdf"));
        QTest::newRow("directorio") << enCache(QStringLiteral("sub"));
        QTest::newRow("el-propio-directorio") << m_cache;
        QTest::newRow("relativa") << QStringLiteral("cache/a.pdf");
        QTest::newRow("vacia") << QString();
        QTest::newRow("inexistente-fuera") << m_fuera + QStringLiteral("/no-existe.pdf");
    }

    void rechazosSinLlamarAlSO()
    {
        QFETCH(QString, ruta);
        const auto r = esperar(m_finder->abrirEnVistaPrevia(ruta));
        QCOMPARE(r.estado, Estado::Fallido);
        QCOMPARE(r.mensaje, AccesoFinder::mensajeVistaPreviaFallida());
        QVERIFY(m_os->peticionesFinder.isEmpty());
    }

    void inexistenteDentroNoLlamaAlSO()
    {
        const auto r = esperar(m_finder->abrirEnVistaPrevia(enCache(QStringLiteral("no-existe.pdf"))));
        QCOMPARE(r.estado, Estado::NoEncontrado);
        QCOMPARE(r.mensaje, QStringLiteral("PDF no encontrado"));
        QVERIFY(m_os->peticionesFinder.isEmpty());
    }

    void sinDirectoriosConfigurados()
    {
        m_finder->setDirectoriosVistaPrevia({});
        const auto r = esperar(m_finder->abrirEnVistaPrevia(enCache(QStringLiteral("a.pdf"))));
        QCOMPARE(r.estado, Estado::Fallido);
        QVERIFY(m_os->peticionesFinder.isEmpty());
    }

    void directoriosRelativosSeIgnoran()
    {
        m_finder->setDirectoriosVistaPrevia({QStringLiteral("cache"), QStringLiteral("")});
        const auto r = esperar(m_finder->abrirEnVistaPrevia(enCache(QStringLiteral("a.pdf"))));
        QCOMPARE(r.estado, Estado::Fallido);
        QVERIFY(m_os->peticionesFinder.isEmpty());
    }

    void sinOS()
    {
        m_finder->setOS(nullptr);
        const auto r = esperar(m_finder->abrirEnVistaPrevia(enCache(QStringLiteral("a.pdf"))));
        QCOMPARE(r.estado, Estado::Fallido);
        QCOMPARE(r.mensaje, QStringLiteral("No se pudo abrir Vista Previa"));
    }

    void resultadosDelSOConMensajes_data()
    {
        QTest::addColumn<OSIntegration::ResultadoFinder>("resultadoSO");
        QTest::addColumn<ResultadoAccionFinder::Estado>("estado");
        QTest::addColumn<QString>("mensaje");
        QTest::newRow("mostrado") << OSIntegration::ResultadoFinder::Mostrado << Estado::Mostrado << QString();
        QTest::newRow("no-encontrado") << OSIntegration::ResultadoFinder::NoEncontrado << Estado::NoEncontrado
                                       << QStringLiteral("PDF no encontrado");
        QTest::newRow("fallido") << OSIntegration::ResultadoFinder::Fallido << Estado::Fallido
                                 << QStringLiteral("No se pudo abrir Vista Previa");
    }

    void resultadosDelSOConMensajes()
    {
        QFETCH(OSIntegration::ResultadoFinder, resultadoSO);
        QFETCH(ResultadoAccionFinder::Estado, estado);
        QFETCH(QString, mensaje);
        m_os->responderInmediato = false;
        auto f = m_finder->abrirEnVistaPrevia(enCache(QStringLiteral("a.pdf")));
        QVERIFY(!f.isFinished());
        QCOMPARE(m_os->pendientesFinder(), 1);
        m_os->completarFinder(resultadoSO);
        const auto r = esperar(f);
        QCOMPARE(r.estado, estado);
        QCOMPARE(r.mensaje, mensaje);
    }

    void pendienteAlDestruirTerminaFallido()
    {
        m_os->responderInmediato = false;
        auto f = m_finder->abrirEnVistaPrevia(enCache(QStringLiteral("a.pdf")));
        m_finder.reset();
        QVERIFY(f.isFinished());
        QCOMPARE(f.result().estado, Estado::Fallido);
        QCOMPARE(f.result().mensaje, QStringLiteral("No se pudo abrir Vista Previa"));
    }
};

QTEST_MAIN(TestAccesoFinderVistaPrevia)
#include "TestAccesoFinderVistaPrevia.moc"
