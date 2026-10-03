// DS5 (T005): centinelas de fuga con el adaptador REAL MacOSSecretStore (sobre
// FakeKeychainApi, sin Keychain del sistema), CredencialesSatServicePersistido
// y SQLite real en un QTemporaryDir. Tras cada escenario (importacion exitosa
// y fallida, reemplazo valido y fallido, material de firma) se buscan, como
// bytes exactos, la contrasena, el DER del certificado (completo y en
// ventanas de 32 bytes), la .key original, la llave descifrada y sus
// codificaciones base64/hex en: .sqlite3/-wal/-shm (abierta y cerrada),
// credentials/ (incluidos los .scc: el ciphertext no debe contener texto en
// claro), temporales creados por la prueba, mensajes capturados y mensajes de
// error visibles. Un control positivo confirma que el escaneo detecta fugas.

#include "FakeKeychainApi.h"
#include "FixturesEFirma.h"

#include "app_core/AppBootstrapper.h"
#include "application/logging/RegexLogSanitizer.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "application/profiles/CredencialesSatServicePersistido.h"
#include "application/profiles/PerfilesSatServicePersistido.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"
#include "infrastructure/secrets/macos/MacOSSecretStore.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QStandardPaths>
#include <QTest>

#include <functional>
#include <memory>
#include <optional>

using namespace satcfdi;

namespace {

const QString kBundle = QStringLiteral("mx.adenium.satcfdi-downloader.pruebas");

QMutex gMutex;
QStringList gMensajes;
QtMessageHandler gPrevio = nullptr;

void capturar(QtMsgType tipo, const QMessageLogContext& contexto, const QString& mensaje)
{
    {
        QMutexLocker l(&gMutex);
        gMensajes.append(mensaje);
    }
    // El control positivo planta un centinela en un mensaje: se captura pero
    // no se reenvia a la salida de la prueba.
    if (gPrevio != nullptr && !mensaje.startsWith(QStringLiteral("fuga plantada"))) {
        gPrevio(tipo, contexto, mensaje);
    }
}

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 10000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

struct Centinela {
    QString nombre;
    QByteArray bytes;
};

QByteArray leerArchivo(const QString& ruta)
{
    QFile f(ruta);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestCentinelasCredenciales : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void centinelasEnTodosLosDestinos();

private:
    // Grafo: persistencia -> dispatcher -> servicios. El store y el keychain
    // falso viven mas que el grafo.
    void abrirGrafo();
    void cerrarGrafo();
    QStringList escanear(const QString& momento) const;
    void verificar(const QString& momento);
    void anotarError(const ErrorCredencialSat& e) { m_mensajesVisibles.append(e.mensaje); }

    std::unique_ptr<fixtures::FixturesEFirma> m_fx;
    std::unique_ptr<QTemporaryDir> m_raiz;
    QDateTime m_inicio;
    QList<Centinela> m_centinelas;
    QStringList m_mensajesVisibles;

    std::shared_ptr<fakes::FakeKeychainApi> m_keychain;
    std::unique_ptr<MacOSSecretStore> m_store;
    QString m_rutaBase;
    std::unique_ptr<SqlitePersistencia> m_persistencia;
    std::unique_ptr<PersistenceDispatcher> m_dispatcher;
    std::unique_ptr<RegexLogSanitizer> m_sanitizer;
    std::unique_ptr<PerfilesSatServicePersistido> m_perfiles;
    std::unique_ptr<CredencialesSatServicePersistido> m_credenciales;
};

void TestCentinelasCredenciales::initTestCase()
{
    m_inicio = QDateTime::currentDateTimeUtc().addSecs(-2);
    gPrevio = qInstallMessageHandler(capturar);

    m_fx = std::make_unique<fixtures::FixturesEFirma>();
    QVERIFY2(m_fx->valido(), qPrintable(m_fx->error()));
    m_raiz = std::make_unique<QTemporaryDir>();
    QVERIFY(m_raiz->isValid());

    // Centinelas por fixture (DS5).
    const QByteArray cer = m_fx->leer(QStringLiteral("efirma.cer"));
    const QByteArray key = m_fx->leer(QStringLiteral("efirma.key"));
    const QByteArray keyAes = m_fx->leer(QStringLiteral("efirma_aes.key"));
    const QByteArray llave = m_fx->leer(QStringLiteral("llave_sin_cifrar.key"));
    QVERIFY(cer.size() > 64 && key.size() > 64 && keyAes.size() > 64 && llave.size() > 64);
    const QList<Centinela> base{
        {QStringLiteral("contrasena"), m_fx->contrasena()},
        {QStringLiteral("cer"), cer},
        {QStringLiteral("key"), key},
        {QStringLiteral("key_aes"), keyAes},
        {QStringLiteral("llave_descifrada"), llave},
    };
    for (const Centinela& c : base) {
        m_centinelas.append(c);
        m_centinelas.append({c.nombre + QStringLiteral(".base64"), c.bytes.toBase64()});
        m_centinelas.append({c.nombre + QStringLiteral(".hex"), c.bytes.toHex()});
        m_centinelas.append({c.nombre + QStringLiteral(".HEX"), c.bytes.toHex().toUpper()});
    }
    for (qsizetype i = 0; i + 32 <= cer.size(); i += 32) {
        m_centinelas.append({QStringLiteral("cer[%1..+32]").arg(i), cer.mid(i, 32)});
    }

    m_keychain = std::make_shared<fakes::FakeKeychainApi>();
    m_store = std::make_unique<MacOSSecretStore>(m_raiz->filePath(QStringLiteral("credentials")), kBundle,
                                                 m_keychain);

    AppBootstrapper::Opciones opciones;
    opciones.directorioDatos = m_raiz->path();
    const auto arranque = AppBootstrapper(opciones).preparar();
    QVERIFY(arranque);
    m_rutaBase = arranque.valor().rutaBase;
}

void TestCentinelasCredenciales::cleanupTestCase()
{
    cerrarGrafo();
    m_store.reset();
    qInstallMessageHandler(gPrevio);
}

void TestCentinelasCredenciales::abrirGrafo()
{
    m_persistencia = std::make_unique<SqlitePersistencia>(m_rutaBase);
    m_dispatcher = std::make_unique<PersistenceDispatcher>();
    m_sanitizer = std::make_unique<RegexLogSanitizer>();
    SqlitePersistencia& p = *m_persistencia;
    m_perfiles = std::make_unique<PerfilesSatServicePersistido>(*m_dispatcher, p.perfiles(), p.unidadDeTrabajo());
    m_credenciales = std::make_unique<CredencialesSatServicePersistido>(
        *m_dispatcher, p.perfiles(), p.credenciales(), p.unidadDeTrabajo(), *m_store);
}

void TestCentinelasCredenciales::cerrarGrafo()
{
    if (!m_dispatcher) {
        return;
    }
    m_credenciales.reset();
    m_perfiles.reset();
    SqlitePersistencia* p = m_persistencia.get();
    m_dispatcher->despachar<void>(std::function<void()>([p] { p->cerrarConexionDelHiloActual(); }));
    m_dispatcher->cerrar();
    m_dispatcher.reset();
    m_sanitizer.reset();
    m_persistencia.reset();
}

QStringList TestCentinelasCredenciales::escanear(const QString& momento) const
{
    QStringList hallazgos;
    auto revisarBytes = [&](const QByteArray& contenido, const QString& donde) {
        for (const Centinela& c : m_centinelas) {
            if (contenido.contains(c.bytes)) {
                hallazgos.append(QStringLiteral("%1: %2 en %3").arg(momento, c.nombre, donde));
            }
        }
    };
    auto revisarArbol = [&](const QString& raiz, const QString& excluir) {
        if (QFileInfo(raiz).isFile()) {
            revisarBytes(leerArchivo(raiz), QFileInfo(raiz).fileName());
            return;
        }
        QDirIterator it(raiz, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString ruta = it.next();
            if (!excluir.isEmpty() && ruta.startsWith(excluir)) {
                continue;
            }
            revisarBytes(leerArchivo(ruta), QDir(m_raiz->path()).relativeFilePath(ruta));
        }
    };

    // 1. Directorio de datos completo: satcfdi.sqlite3, -wal, -shm,
    //    credentials/ (*.scc y temporales) y cualquier otro archivo.
    revisarArbol(m_raiz->path(), {});

    // 2. Temporales del sistema creados durante la prueba (excepto los
    //    fixtures, que contienen el material por diseno).
    const QString fixtures = QDir::cleanPath(m_fx->directorio());
    const QFileInfoList entradas = QDir(QDir::tempPath())
                                       .entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System
                                                      | QDir::NoDotAndDotDot);
    for (const QFileInfo& e : entradas) {
        const QString ruta = QDir::cleanPath(e.absoluteFilePath());
        if (ruta == fixtures || ruta == QDir::cleanPath(m_raiz->path()) || e.isSymLink()) {
            continue;
        }
        if (e.lastModified().toUTC() >= m_inicio || e.birthTime().toUTC() >= m_inicio) {
            revisarArbol(ruta, fixtures);
        }
    }

    // 3. Mensajes capturados y mensajes de error visibles.
    QStringList textos;
    {
        QMutexLocker l(&gMutex);
        textos = gMensajes;
    }
    for (const QString& t : std::as_const(textos)) {
        revisarBytes(t.toUtf8(), QStringLiteral("mensaje capturado"));
    }
    for (const QString& t : m_mensajesVisibles) {
        revisarBytes(t.toUtf8(), QStringLiteral("mensaje visible"));
    }
    return hallazgos;
}

void TestCentinelasCredenciales::verificar(const QString& momento)
{
    QVERIFY2(QFile::exists(m_rutaBase), "base ausente");
    const QStringList abierta = escanear(momento + QStringLiteral(" (base abierta)"));
    QVERIFY2(abierta.isEmpty(), qPrintable(abierta.join(u'\n')));
    // Cerrar (checkpoint de WAL) y volver a escanear; despues reabrir.
    cerrarGrafo();
    const QStringList cerrada = escanear(momento + QStringLiteral(" (base cerrada)"));
    QVERIFY2(cerrada.isEmpty(), qPrintable(cerrada.join(u'\n')));
    abrirGrafo();
}

void TestCentinelasCredenciales::centinelasEnTodosLosDestinos()
{
    abrirGrafo();
    const auto perfil = esperar(m_perfiles->crearPerfilSimulado({fixtures::kRfc, QStringLiteral("Perfil"), true}));
    QVERIFY(perfil && perfil->esExito());
    const PerfilId id = perfil->valor();
    auto entrada = [&](const QString& key, std::optional<QByteArray> contrasena = std::nullopt) {
        EntradaEFirma e = m_fx->entrada(QStringLiteral("efirma.cer"), key);
        if (contrasena) {
            e.contrasena = fixtures::FixturesEFirma::bufferDe(*contrasena);
        }
        return e;
    };
    const QByteArray incorrecta = m_fx->contrasena() + QByteArrayLiteral("-x");

    // Importacion fallida (contrasena incorrecta).
    {
        const auto r = esperar(m_credenciales->importar(id, entrada(QStringLiteral("efirma.key"), incorrecta)));
        QVERIFY(r && !r->esExito());
        QCOMPARE(r->error().categoria, std::optional(ErrorSecretStore::Categoria::ContrasenaIncorrecta));
        anotarError(r->error());
        verificar(QStringLiteral("importacion fallida"));
    }
    // Importacion exitosa.
    {
        const auto r = esperar(m_credenciales->importar(id, entrada(QStringLiteral("efirma.key"))));
        QVERIFY(r && r->esExito());
        QVERIFY(!QDir(m_raiz->filePath(QStringLiteral("credentials"))).isEmpty());
        verificar(QStringLiteral("importacion exitosa"));
    }
    // Reemplazo valido (misma llave en PBES2 AES-256).
    {
        const auto r = esperar(m_credenciales->reemplazar(id, entrada(QStringLiteral("efirma_aes.key"))));
        QVERIFY(r && r->esExito());
        verificar(QStringLiteral("reemplazo valido"));
    }
    // Reemplazo fallido.
    {
        const auto r = esperar(m_credenciales->reemplazar(id, entrada(QStringLiteral("efirma.key"), incorrecta)));
        QVERIFY(r && !r->esExito());
        anotarError(r->error());
        verificar(QStringLiteral("reemplazo fallido"));
    }
    // Material de firma (en el hilo del dispatcher, como una operacion de
    // trabajo). Se valida que la llave descifrada coincide con el centinela.
    {
        const QByteArray llaveEsperada = m_fx->leer(QStringLiteral("llave_sin_cifrar.key"));
        CredencialesSatServicePersistido* servicio = m_credenciales.get();
        const auto ok = esperar(m_dispatcher->despachar<bool>([servicio, id, llaveEsperada]() {
            auto material = servicio->obtenerMaterialFirma(id);
            if (!material) {
                return false;
            }
            const BufferSecreto& llave = material.valor().llavePrivadaDer();
            return QByteArray::fromRawData(reinterpret_cast<const char*>(llave.datos()),
                                           static_cast<qsizetype>(llave.tamano()))
                == llaveEsperada;
        }));
        QVERIFY(ok && *ok);
        verificar(QStringLiteral("material de firma"));
    }

    // Control positivo: el escaneo detecta fugas plantadas en cada destino.
    {
        const QByteArray hex = m_fx->leer(QStringLiteral("llave_sin_cifrar.key")).toHex();
        QFile plantado(m_raiz->filePath(QStringLiteral("credentials/plantado.txt")));
        QVERIFY(plantado.open(QIODevice::WriteOnly));
        plantado.write(hex);
        plantado.close();
        qInfo("fuga plantada: %s", m_fx->contrasena().constData());
        m_mensajesVisibles.append(QString::fromLatin1(m_fx->leer(QStringLiteral("efirma.cer")).toBase64()));
        const QStringList hallazgos = escanear(QStringLiteral("control"));
        QVERIFY(hallazgos.contains(QStringLiteral("control: llave_descifrada.hex en credentials/plantado.txt")));
        QVERIFY(hallazgos.contains(QStringLiteral("control: contrasena en mensaje capturado")));
        QVERIFY(hallazgos.contains(QStringLiteral("control: cer.base64 en mensaje visible")));
        QFile::remove(plantado.fileName());
        m_mensajesVisibles.removeLast();
        QMutexLocker l(&gMutex);
        gMensajes.clear();
    }
}

#include "TestCentinelasCredenciales.moc"

int main(int argc, char* argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    satcfdi::configurarIdentidadAplicacion();
    TestCentinelasCredenciales prueba;
    return QTest::qExec(&prueba, argc, argv);
}
