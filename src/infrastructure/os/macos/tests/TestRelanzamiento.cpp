// T025.5: auxiliar de relanzamiento. Comprueba el comando construido y lo
// ejecuta de verdad con un pid sintetico (un `sleep` hijo) y /usr/bin/touch
// como "ejecutable", sin lanzar la app ni `open`. Datos sinteticos en
// QTemporaryDir.

#include "infrastructure/os/macos/Relanzamiento.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace satcfdi::macos;

namespace {

// Auxiliar en primer plano (para observar su salida); la app lo inicia separado.
std::unique_ptr<QProcess> iniciar(const std::optional<ComandoRelanzamiento>& comando)
{
    Q_ASSERT(comando.has_value());
    const ComandoRelanzamiento& c = *comando;
    auto p = std::make_unique<QProcess>();
    p->setProgram(c.programa);
    p->setArguments(c.argumentos);
    p->start();
    return p;
}

} // namespace

class TestRelanzamiento : public QObject {
    Q_OBJECT

private slots:
    void argumentosConservados()
    {
        const QStringList proceso{QStringLiteral("/Applications/Archivo Fiscal.app/Contents/MacOS/satcfdi_app"),
                                  QStringLiteral("-psn_0_12345"), QStringLiteral("--data-dir"),
                                  QStringLiteral("/tmp/datos sinteticos")};
        QCOMPARE(argumentosParaRelanzar(proceso),
                 (QStringList{QStringLiteral("--data-dir"), QStringLiteral("/tmp/datos sinteticos")}));
        QVERIFY(argumentosParaRelanzar({QStringLiteral("app")}).isEmpty());
        QVERIFY(argumentosParaRelanzar({}).isEmpty());
    }

    void comandoModoBundle()
    {
        const auto c = construirComandoRelanzamiento(4242, QStringLiteral("/Applications/Archivo Fiscal.app"),
                                                     QStringLiteral("/ignorado"), {QStringLiteral("--data-dir"),
                                                                                   QStringLiteral("/x y")});
        QVERIFY(c.has_value());
        QCOMPARE(c->programa, QStringLiteral("/bin/sh"));
        QCOMPARE(c->argumentos, (QStringList{QStringLiteral("-c"), guionRelanzamiento(),
                                            QStringLiteral("satcfdi-relanzar"), QStringLiteral("4242"),
                                            QString::number(kIntentosRelanzamiento), QStringLiteral("bundle"),
                                            QStringLiteral("/Applications/Archivo Fiscal.app"),
                                            QStringLiteral("--data-dir"), QStringLiteral("/x y")}));
        QCOMPARE(kIntentosRelanzamiento, 600);
        // El guion es fijo: no contiene pid, rutas ni argumentos.
        QVERIFY(!guionRelanzamiento().contains(QStringLiteral("4242")));
        QVERIFY(!guionRelanzamiento().contains(QStringLiteral("Archivo Fiscal")));
        QVERIFY(guionRelanzamiento().contains(QStringLiteral("exec /usr/bin/open -n \"$destino\" --args \"$@\"")));
    }

    void comandoModoEjecutable()
    {
        const auto sinBundle = construirComandoRelanzamiento(1, QString(), QStringLiteral("/build/satcfdi_app"), {});
        QVERIFY(sinBundle.has_value());
        QCOMPARE(sinBundle->argumentos.mid(5), (QStringList{QStringLiteral("ejecutable"), QStringLiteral("/build/satcfdi_app")}));
        // Una ruta que no es .app no se abre con `open`.
        const auto noApp = construirComandoRelanzamiento(1, QStringLiteral("/build"), QStringLiteral("/build/app"), {});
        QVERIFY(noApp.has_value());
        QCOMPARE(noApp->argumentos.at(5), QStringLiteral("ejecutable"));
    }

    // R6: App Translocation -> no se construye ni programa nada.
    void bundleTrasladadoNoSeRelanza()
    {
        const QString trasladado = QStringLiteral(
            "/private/var/folders/xy/abc123/T/AppTranslocation/0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9/d/Archivo Fiscal.app");
        QVERIFY(bundleTrasladado(trasladado));
        QVERIFY(!construirComandoRelanzamiento(4242, trasladado, trasladado + QStringLiteral("/Contents/MacOS/satcfdi_app"),
                                               {QStringLiteral("--data-dir"), QStringLiteral("/x")})
                     .has_value());
        // Tambien si solo el ejecutable delata el traslado.
        QVERIFY(!construirComandoRelanzamiento(1, QString(), trasladado + QStringLiteral("/Contents/MacOS/satcfdi_app"), {})
                     .has_value());

        QVERIFY(!bundleTrasladado(QStringLiteral("/Applications/Archivo Fiscal.app")));
        QVERIFY(!bundleTrasladado(QStringLiteral("/Users/demo/AppTranslocationX/Archivo Fiscal.app")));
        QVERIFY(!bundleTrasladado(QString()));
        QVERIFY(construirComandoRelanzamiento(1, QStringLiteral("/Applications/Archivo Fiscal.app"),
                                              QStringLiteral("/Applications/Archivo Fiscal.app/Contents/MacOS/satcfdi_app"), {})
                    .has_value());
    }

    void esperaAQueTermineElProceso()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // Argumento hostil: se pasa literal, sin expandirse en el shell.
        const QString marca = tmp.filePath(QStringLiteral("relanzada $(echo x) 'a' \"b\""));

        QProcess anterior; // la "instancia actual"
        anterior.start(QStringLiteral("/bin/sleep"), {QStringLiteral("1")});
        QVERIFY(anterior.waitForStarted());

        auto auxiliar = iniciar(construirComandoRelanzamiento(anterior.processId(), QString(),
                                                              QStringLiteral("/usr/bin/touch"), {marca}));
        QVERIFY(auxiliar->waitForStarted());
        QTest::qWait(400);
        QVERIFY2(!QFileInfo::exists(marca), "no relanza mientras el proceso anterior vive");
        QVERIFY(anterior.waitForFinished(5000)); // termina y se recoge (sin zombie)

        QVERIFY(auxiliar->waitForFinished(5000));
        QCOMPARE(auxiliar->exitCode(), 0);
        QVERIFY(QFileInfo::exists(marca));
        QCOMPARE(QDir(tmp.path()).entryList(QDir::Files).size(), 1);
    }

    void procesoYaTerminadoRelanzaDeInmediato()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QProcess anterior;
        anterior.start(QStringLiteral("/usr/bin/true"), {});
        QVERIFY(anterior.waitForFinished(5000));
        const qint64 pidMuerto = anterior.processId() > 0 ? anterior.processId() : 999999;
        const QString marca = tmp.filePath(QStringLiteral("inmediata"));
        QElapsedTimer t;
        t.start();
        auto auxiliar = iniciar(construirComandoRelanzamiento(pidMuerto, QString(), QStringLiteral("/usr/bin/touch"),
                                                              {marca}));
        QVERIFY(auxiliar->waitForFinished(5000));
        QCOMPARE(auxiliar->exitCode(), 0);
        QVERIFY(QFileInfo::exists(marca));
        QVERIFY(t.elapsed() < 3000);
    }

    void plazoVencidoNoRelanza()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString marca = tmp.filePath(QStringLiteral("no-debe-existir"));
        QProcess colgado;
        colgado.start(QStringLiteral("/bin/sleep"), {QStringLiteral("30")});
        QVERIFY(colgado.waitForStarted());

        auto auxiliar = iniciar(construirComandoRelanzamiento(colgado.processId(), QString(),
                                                              QStringLiteral("/usr/bin/touch"), {marca}, 3));
        QVERIFY(auxiliar->waitForFinished(5000));
        QCOMPARE(auxiliar->exitCode(), kSalidaRelanzamientoPlazo);
        QVERIFY(!QFileInfo::exists(marca));
        colgado.kill();
        colgado.waitForFinished(5000);
    }
};

QTEST_GUILESS_MAIN(TestRelanzamiento)
#include "TestRelanzamiento.moc"
