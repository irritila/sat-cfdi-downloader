// Pruebas de la CLI del spike SAT (T006, criterio 7 y reglas de operacion),
// sin red ni e.firma real: fixtures sinteticos de tests/secrets. La logica se
// prueba en proceso con costuras (contrasena, confirmaciones, fabrica del
// cliente HTTP); un caso ejecuta el binario real alimentando stdin.

#include "FixturesEFirma.h"
#include "SpikeCli.h"

#include "infrastructure/sat/ClienteHttpSat.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <QTest>

#include <deque>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>

using namespace satcfdi;
using namespace satcfdi::spike;

namespace {

struct Escenario {
    explicit Escenario(const fixtures::FixturesEFirma& fx)
        : m_fx(fx)
    {
        salida = tmp.filePath(QStringLiteral("salida"));
        deps.leerContrasena = [this]() -> std::optional<BufferSecreto> {
            ++contrasenasPedidas;
            return fixtures::FixturesEFirma::bufferDe(contrasena);
        };
        deps.leerLinea = [this]() -> std::optional<QString> {
            ++lineasPedidas;
            if (respuestas.empty()) {
                return std::nullopt;
            }
            QString r = respuestas.front();
            respuestas.pop_front();
            return r;
        };
        deps.imprimir = [this](const QString& t) { salidaTexto += t + QLatin1Char('\n'); };
        deps.imprimirError = [this](const QString& t) { errorTexto += t + QLatin1Char('\n'); };
        deps.crearCliente = [this] {
            ++clientesCreados;
            return std::unique_ptr<sat::ClienteHttpSat>();
        };
        deps.relojUtc = [] { return fixtures::ahoraVigente(); };
        deps.directorioSalidaPorDefecto = salida;
        deps.raizRepositorio = QStringLiteral(SATCFDI_SOURCE_DIR);
        contrasena = m_fx.contrasena();
    }

    int correr(const QStringList& extra, const QString& cer = QStringLiteral("efirma.cer"),
               const QString& key = QStringLiteral("efirma.key"))
    {
        QStringList args{QStringLiteral("satcfdi_sat_spike")};
        args << extra << QStringLiteral("--cer") << m_fx.ruta(cer) << QStringLiteral("--key") << m_fx.ruta(key);
        return ejecutarSpike(args, deps);
    }

    QString todo() const { return salidaTexto + errorTexto; }

    const fixtures::FixturesEFirma& m_fx;
    QTemporaryDir tmp;
    QString salida;
    DependenciasSpike deps;
    QByteArray contrasena;
    std::deque<QString> respuestas;
    QString salidaTexto;
    QString errorTexto;
    int contrasenasPedidas = 0;
    int lineasPedidas = 0;
    int clientesCreados = 0;
};

QString hashCriterio(const QString& operacion, const QString& rfc, const QString& dia)
{
    const QString c = QStringLiteral("%1|%2|%3|CFDI|Vigente").arg(operacion, rfc, dia);
    return QString::fromLatin1(QCryptographicHash::hash(c.toUtf8(), QCryptographicHash::Sha256).toHex());
}

int modo(const QString& ruta)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(ruta).constData(), &st) == 0 ? static_cast<int>(st.st_mode & 0777) : -1;
}

QByteArray verificacionEn(const QString& id, const QDateTime& en)
{
    return QStringLiteral(R"({"evento":"verificacion","registro":"%1","idSolicitud":"%2","en":"%3"})")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces), id, en.toString(Qt::ISODateWithMs))
        .toUtf8();
}

void escribirLedger(const QString& dir, const QList<QByteArray>& lineas)
{
    QDir().mkpath(dir);
    QFile f(QDir(dir).filePath(QStringLiteral("ledger.jsonl")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    for (const QByteArray& l : lineas) {
        f.write(l + '\n');
    }
}

} // namespace

class TestSpikeCli : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void materialInvalidoTerminaSinRedNiArchivos_data();
    void materialInvalidoTerminaSinRedNiArchivos();
    void dryRunMuestraSobresEnmascaradosSinRed();
    void rutasDentroDelRepositorioSeRechazan();
    void contrasenaPorArgumentoSeRechazaSinReproducirla();
    void salidaDentroDelRepoEnTodosLosModosReales();
    void directorioYLockPrivadosYSymlinkFallaCerrado();
    void limitesDeVerificacion();
    void descargaUnicaPorPaquete();
    void guardadoFallidoNoBloqueaYNoEsExito();
    void idsConFormatoInvalidoSeRechazanSinRed();
    void ultimaYPaqueteSeResuelvenDelLedger();
    void ledgerEnUsoPorOtroProcesoFallaCerrado();
    void ledgerCorruptoFallaCerrado();
    void solicitudSinConfirmacionNoUsaRed();
    void ledgerRechazaCriterioRepetidoYTope();
    void binarioDryRunConStdin();
    void cleanupTestCase();

private:
    std::unique_ptr<fixtures::FixturesEFirma> m_fx;
    QString m_dia; // dia cerrado de hace 10 dias respecto al reloj de prueba
};

void TestSpikeCli::initTestCase()
{
    // ctest fija TMPDIR a un directorio del build; los fixtures y salidas
    // quedan alli y se borran al terminar (QTemporaryDir autoRemove).
    qInfo("temporales en %s", qPrintable(QDir::tempPath()));
    m_fx = std::make_unique<fixtures::FixturesEFirma>();
    QVERIFY2(m_fx->valido(), qPrintable(m_fx->error()));
    m_dia = fixtures::ahoraVigente().toLocalTime().date().addDays(-10).toString(Qt::ISODate);
}

void TestSpikeCli::materialInvalidoTerminaSinRedNiArchivos_data()
{
    QTest::addColumn<QString>("cer");
    QTest::addColumn<QString>("key");
    QTest::addColumn<bool>("contrasenaIncorrecta");
    QTest::addColumn<QString>("categoria");
    QTest::newRow("contrasena incorrecta") << "efirma.cer" << "efirma.key" << true << "ContrasenaIncorrecta";
    QTest::newRow("pareja incompatible") << "efirma.cer" << "segundo_par.key" << false << "ParejaIncompatible";
    QTest::newRow("no es e.firma (CSD)") << "csd.cer" << "csd.key" << false << "NoEsEFirma";
}

void TestSpikeCli::materialInvalidoTerminaSinRedNiArchivos()
{
    QFETCH(QString, cer);
    QFETCH(QString, key);
    QFETCH(bool, contrasenaIncorrecta);
    QFETCH(QString, categoria);

    for (const QStringList& modo : {QStringList{QStringLiteral("--dry-run")}, QStringList{QStringLiteral("autentica")},
                                    QStringList{QStringLiteral("solicita"), QStringLiteral("--tipo"),
                                                QStringLiteral("emitidos"), QStringLiteral("--desde"), m_dia}}) {
        Escenario e(*m_fx);
        if (contrasenaIncorrecta) {
            e.contrasena = m_fx->contrasena() + QByteArrayLiteral("-x");
        }
        const QStringList antes = QDir(m_fx->directorio()).entryList(QDir::Files);
        QCOMPARE(e.correr(modo, cer, key), kSalidaMaterial);
        QVERIFY2(e.errorTexto.contains(QStringLiteral("e.firma invalida: ") + categoria), qPrintable(e.errorTexto));
        QCOMPARE(e.clientesCreados, 0); // sin red
        QCOMPARE(e.lineasPedidas, 0);   // ni siquiera se pide confirmacion
        QVERIFY(!QFileInfo::exists(e.salida));
        QCOMPARE(QDir(m_fx->directorio()).entryList(QDir::Files), antes);
        QVERIFY(QDir(e.tmp.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(!e.todo().toUtf8().contains(m_fx->contrasena()));
    }
}

void TestSpikeCli::dryRunMuestraSobresEnmascaradosSinRed()
{
    Escenario e(*m_fx);
    QCOMPARE(e.correr({QStringLiteral("--dry-run"), QStringLiteral("--desde"), m_dia}), kSalidaOk);
    QCOMPARE(e.clientesCreados, 0);
    QCOMPARE(e.lineasPedidas, 0);
    QVERIFY(!QFileInfo::exists(e.salida));
    const QString t = e.todo();
    for (const char* op : {"Autentica", "SolicitaDescargaEmitidos", "SolicitaDescargaRecibidos",
                           "VerificaSolicitudDescarga", "Descargar"}) {
        QVERIFY2(t.contains(QLatin1String("-- ") + QLatin1String(op)), op);
    }
    QVERIFY(t.contains(QStringLiteral("[certificado]")));
    QVERIFY(t.contains(QStringLiteral("[firma]")));
    QVERIFY(t.contains(QStringLiteral("dry-run")));
    // Sin secretos ni identificadores reales.
    const QByteArray u = t.toUtf8();
    QVERIFY(!u.contains(m_fx->contrasena()));
    const QByteArray cer = m_fx->leer(QStringLiteral("efirma.cer"));
    const QByteArray cer64 = cer.toBase64();
    for (qsizetype i = 0; i + 40 <= cer64.size(); i += 40) {
        QVERIFY(!u.contains(cer64.mid(i, 40)));
    }
    QVERIFY(!t.contains(fixtures::kRfc));
}

void TestSpikeCli::rutasDentroDelRepositorioSeRechazan()
{
    const QString dentro = QStringLiteral(SATCFDI_SOURCE_DIR "/CMakeLists.txt");
    QVERIFY(rutaDentroDe(dentro, QStringLiteral(SATCFDI_SOURCE_DIR)));
    QVERIFY(rutaDentroDe(QStringLiteral(SATCFDI_SOURCE_DIR "/no/existe/x.key"), QStringLiteral(SATCFDI_SOURCE_DIR)));
    QVERIFY(!rutaDentroDe(m_fx->ruta(QStringLiteral("efirma.cer")), QStringLiteral(SATCFDI_SOURCE_DIR)));

    Escenario e(*m_fx);
    QStringList args{QStringLiteral("satcfdi_sat_spike"), QStringLiteral("--dry-run"), QStringLiteral("--cer"),
                     dentro, QStringLiteral("--key"), m_fx->ruta(QStringLiteral("efirma.key"))};
    QCOMPARE(ejecutarSpike(args, e.deps), kSalidaUso);
    QVERIFY(e.errorTexto.contains(QStringLiteral("dentro del repositorio")));
    QCOMPARE(e.contrasenasPedidas, 0);

    // Directorio de salida dentro del repo: rechazado antes de la red.
    Escenario s(*m_fx);
    QCOMPARE(s.correr({QStringLiteral("solicita"), QStringLiteral("--tipo"), QStringLiteral("emitidos"),
                       QStringLiteral("--desde"), m_dia, QStringLiteral("--salida"),
                       QStringLiteral(SATCFDI_SOURCE_DIR "/sat-spike-salida")}),
             kSalidaUso);
    QCOMPARE(s.clientesCreados, 0);
}

void TestSpikeCli::contrasenaPorArgumentoSeRechazaSinReproducirla()
{
    Escenario e(*m_fx);
    const QString pegada = QStringLiteral("Secreto-Pegado-9137");
    QCOMPARE(e.correr({QStringLiteral("--dry-run"), QStringLiteral("--contrasena"), pegada}), kSalidaUso);
    QVERIFY(e.errorTexto.contains(QStringLiteral("argumento no reconocido")));
    QVERIFY(!e.todo().contains(pegada));
    QVERIFY(!e.todo().contains(QStringLiteral("--contrasena ")));
    QCOMPARE(e.contrasenasPedidas, 0);

    Escenario s(*m_fx);
    QCOMPARE(s.correr({QStringLiteral("--dry-run"), pegada}), kSalidaUso);
    QVERIFY(!s.todo().contains(pegada));
    // El binario real ya no acepta --contrasena-stdin.
    Escenario t(*m_fx);
    QCOMPARE(t.correr({QStringLiteral("--dry-run"), QStringLiteral("--contrasena-stdin")}), kSalidaUso);
}

void TestSpikeCli::salidaDentroDelRepoEnTodosLosModosReales()
{
    const QString dentro = QStringLiteral(SATCFDI_SOURCE_DIR "/sat-spike-salida");
    for (const QStringList& modo :
         {QStringList{QStringLiteral("autentica")},
          QStringList{QStringLiteral("verifica"), QStringLiteral("--id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
          QStringList{QStringLiteral("descarga"), QStringLiteral("--id-paquete"), QStringLiteral("0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0_01")},
          QStringList{QStringLiteral("solicita"), QStringLiteral("--tipo"), QStringLiteral("emitidos"),
                      QStringLiteral("--desde"), m_dia}}) {
        Escenario e(*m_fx);
        QCOMPARE(e.correr(modo + QStringList{QStringLiteral("--salida"), dentro}), kSalidaUso);
        QVERIFY(e.errorTexto.contains(QStringLiteral("dentro del repositorio")));
        QCOMPARE(e.contrasenasPedidas, 0);
        QCOMPARE(e.clientesCreados, 0);
        QVERIFY(!QFileInfo::exists(dentro));
    }
}

void TestSpikeCli::directorioYLockPrivadosYSymlinkFallaCerrado()
{
    {
        // Autentica sin cliente: llega a abrir el ledger (0700/0600) y falla
        // AntesDeEnvio sin red.
        Escenario e(*m_fx);
        e.respuestas = {QStringLiteral("yes")};
        QCOMPARE(e.correr({QStringLiteral("autentica")}), kSalidaSat);
        QCOMPARE(e.clientesCreados, 1);
        QCOMPARE(modo(e.salida), 0700);
        QCOMPARE(modo(QDir(e.salida).filePath(QStringLiteral("ledger.lock"))), 0600);
    }
    {
        // Directorio previo con 0755 y lock 0644: se corrigen y se confirman.
        Escenario e(*m_fx);
        QDir().mkpath(e.salida);
        ::chmod(QFile::encodeName(e.salida).constData(), 0755);
        QFile lock(QDir(e.salida).filePath(QStringLiteral("ledger.lock")));
        QVERIFY(lock.open(QIODevice::WriteOnly));
        lock.close();
        ::chmod(QFile::encodeName(lock.fileName()).constData(), 0644);
        e.respuestas = {QStringLiteral("no")};
        QCOMPARE(e.correr({QStringLiteral("autentica")}), kSalidaCancelada);
        QCOMPARE(modo(e.salida), 0700);
        QCOMPARE(modo(lock.fileName()), 0600);
    }
    {
        // Directorio de salida que es un enlace simbolico: falla cerrado.
        Escenario e(*m_fx);
        const QString real = e.tmp.filePath(QStringLiteral("real"));
        QDir().mkpath(real);
        QVERIFY(QFile::link(real, e.salida));
        QCOMPARE(e.correr({QStringLiteral("autentica")}), kSalidaLedger);
        QCOMPARE(e.clientesCreados, 0);
        QCOMPARE(e.lineasPedidas, 0);
        QVERIFY(QDir(real).isEmpty());
    }
    {
        // ledger.lock que es un enlace simbolico: falla cerrado.
        Escenario e(*m_fx);
        QDir().mkpath(e.salida);
        QVERIFY(QFile::link(e.tmp.filePath(QStringLiteral("otro")),
                            QDir(e.salida).filePath(QStringLiteral("ledger.lock"))));
        QCOMPARE(e.correr({QStringLiteral("autentica")}), kSalidaLedger);
        QCOMPARE(e.clientesCreados, 0);
        QVERIFY(!QFileInfo::exists(e.tmp.filePath(QStringLiteral("otro"))));
    }
}

void TestSpikeCli::limitesDeVerificacion()
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QDateTime ahora = fixtures::ahoraVigente();
    const QStringList args{QStringLiteral("verifica"), QStringLiteral("--id"), id};
    {
        Escenario e(*m_fx);
        QList<QByteArray> lineas;
        for (int i = 0; i < kMaxVerificacionesPorSolicitud; ++i) {
            lineas.append(verificacionEn(id, ahora.addSecs(-3600 * (i + 1))));
        }
        escribirLedger(e.salida, lineas);
        QCOMPARE(e.correr(args), kSalidaLedger);
        QVERIFY(e.errorTexto.contains(QStringLiteral("maximo 10")));
        QCOMPARE(e.clientesCreados, 0);
        QCOMPARE(e.lineasPedidas, 0);
    }
    {
        Escenario e(*m_fx);
        escribirLedger(e.salida, {verificacionEn(id, ahora.addSecs(-5 * 60))});
        QCOMPARE(e.correr(args), kSalidaLedger);
        QVERIFY2(e.errorTexto.contains(QStringLiteral("espere 10 min 0 s")), qPrintable(e.errorTexto));
        QCOMPARE(e.clientesCreados, 0);
    }
    {
        // Otra solicitud o mas de 15 min: se permite (llega a la confirmacion).
        Escenario e(*m_fx);
        escribirLedger(e.salida, {verificacionEn(id, ahora.addSecs(-16 * 60)),
                                  verificacionEn(QStringLiteral("otro-id"), ahora.addSecs(-60))});
        e.respuestas = {QStringLiteral("no")};
        QCOMPARE(e.correr(args), kSalidaCancelada);
        QVERIFY(e.salidaTexto.contains(QStringLiteral("1 de 10 verificaciones")));
        QCOMPARE(e.clientesCreados, 0);
    }
}

void TestSpikeCli::descargaUnicaPorPaquete()
{
    const QString paquete = QStringLiteral("0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0_01");
    {
        // Descarga previa SIN guardar: se permite repetir (llega a confirmar).
        Escenario e(*m_fx);
        escribirLedger(e.salida, {QStringLiteral(R"({"evento":"descarga","registro":"r","idPaquete":"%1"})").arg(paquete).toUtf8(),
                                  QByteArray(R"({"evento":"resultado","registro":"r","fase":"RespuestaExplicita","guardado":false})")});
        e.respuestas = {QStringLiteral("no")};
        QCOMPARE(e.correr({QStringLiteral("descarga"), QStringLiteral("--id-paquete"), paquete}), kSalidaCancelada);
    }
    Escenario e(*m_fx);
    escribirLedger(e.salida, {QStringLiteral(R"({"evento":"descarga","registro":"r","idPaquete":"%1"})")
                                  .arg(paquete)
                                  .toUtf8(),
                              QByteArray(R"({"evento":"resultado","registro":"r","fase":"RespuestaExplicita","guardado":true})")});
    QCOMPARE(e.correr({QStringLiteral("descarga"), QStringLiteral("--id-paquete"), paquete}), kSalidaLedger);
    QVERIFY(e.errorTexto.contains(QStringLiteral("una sola descarga")));
    QVERIFY(!e.todo().contains(paquete));
    QCOMPARE(e.clientesCreados, 0);
    QCOMPARE(e.lineasPedidas, 0);
}

void TestSpikeCli::guardadoFallidoNoBloqueaYNoEsExito()
{
    QTemporaryDir tmp;
    const QByteArray zip("PK\x03\x04 contenido de prueba");
    // Costura: la escritura falla (p. ej. disco lleno).
    const ResultadoGuardado fallo = guardarPaquete(tmp.path(), QStringLiteral("paquete-a.zip"), zip,
                                                   [](int, const QByteArray&) { return false; });
    QVERIFY(!fallo.guardado);
    QVERIFY(!fallo.error.isEmpty());
    QVERIFY(!QFileInfo::exists(fallo.ruta)); // sin archivo parcial
    // El ledger con ese intento no bloquea la nueva descarga.
    const QList<QJsonObject> ledger{
        QJsonObject{{QStringLiteral("evento"), QStringLiteral("descarga")}, {QStringLiteral("registro"), QStringLiteral("r1")},
                    {QStringLiteral("idPaquete"), QStringLiteral("P_01")}},
        QJsonObject{{QStringLiteral("evento"), QStringLiteral("resultado")}, {QStringLiteral("registro"), QStringLiteral("r1")},
                    {QStringLiteral("guardado"), false}}};
    QVERIFY(!paqueteYaGuardado(ledger, QStringLiteral("P_01")));
    // Reintento con escritura normal: 0600 y bloquea las siguientes.
    const ResultadoGuardado ok = guardarPaquete(tmp.path(), QStringLiteral("paquete-a.zip"), zip);
    QVERIFY2(ok.guardado, qPrintable(ok.error));
    QCOMPARE(modo(ok.ruta), 0600);
    QCOMPARE(modo(QDir(tmp.path()).filePath(QStringLiteral("paquetes"))), 0700);
    QList<QJsonObject> conGuardado = ledger;
    conGuardado.append(QJsonObject{{QStringLiteral("evento"), QStringLiteral("resultado")},
                                   {QStringLiteral("registro"), QStringLiteral("r1")},
                                   {QStringLiteral("guardado"), true}});
    QVERIFY(paqueteYaGuardado(conGuardado, QStringLiteral("P_01")));
    QVERIFY(!paqueteYaGuardado(conGuardado, QStringLiteral("P_02")));
    // Nunca sobrescribe un ZIP existente.
    QVERIFY(!guardarPaquete(tmp.path(), QStringLiteral("paquete-a.zip"), zip).guardado);
    QVERIFY(kSalidaPaqueteNoGuardado != kSalidaOk);
}

void TestSpikeCli::idsConFormatoInvalidoSeRechazanSinRed()
{
    const QStringList invalidos{QStringLiteral("<IdSolicitud>"), QStringLiteral("abc"),
                                QStringLiteral("0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0_01"),
                                QStringLiteral("0F1E2D3C4B5A69788796A5B4C3D2E1F0")};
    for (const QString& v : invalidos) {
        Escenario e(*m_fx);
        QCOMPARE(e.correr({QStringLiteral("verifica"), QStringLiteral("--id"), v}), kSalidaUso);
        QVERIFY(e.errorTexto.contains(QStringLiteral("no tiene formato de IdSolicitud")));
        QVERIFY(!e.todo().contains(v));
        QCOMPARE(e.contrasenasPedidas, 0);
        QCOMPARE(e.clientesCreados, 0);
    }
    for (const QString& v : {QStringLiteral("<IdPaquete>"), QStringLiteral("0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0"),
                             QStringLiteral("0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0_x1")}) {
        Escenario e(*m_fx);
        QCOMPARE(e.correr({QStringLiteral("descarga"), QStringLiteral("--id-paquete"), v}), kSalidaUso);
        QVERIFY(e.errorTexto.contains(QStringLiteral("no tiene formato de IdPaquete")));
        QVERIFY(!e.todo().contains(v));
        QCOMPARE(e.contrasenasPedidas, 0);
    }
    // Formatos validos llegan a la confirmacion.
    Escenario e(*m_fx);
    e.respuestas = {QStringLiteral("no")};
    QCOMPARE(e.correr({QStringLiteral("descarga"), QStringLiteral("--id-paquete"),
                       QStringLiteral("0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0_01")}),
             kSalidaCancelada);
    // Exclusiones.
    Escenario x(*m_fx);
    QCOMPARE(x.correr({QStringLiteral("verifica"), QStringLiteral("--ultima"), QStringLiteral("--id"),
                       QUuid::createUuid().toString(QUuid::WithoutBraces)}),
             kSalidaUso);
    Escenario y(*m_fx);
    QCOMPARE(y.correr({QStringLiteral("descarga"), QStringLiteral("--paquete"), QStringLiteral("0")}), kSalidaUso);
}

void TestSpikeCli::ultimaYPaqueteSeResuelvenDelLedger()
{
    const QString aceptada = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString rechazada = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString p1 = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper() + QStringLiteral("_01");
    const QString p2 = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper() + QStringLiteral("_02");
    const QList<QByteArray> base{
        QStringLiteral(R"({"evento":"resultado","registro":"s1","codEstatus":"5000","idSolicitud":"%1"})").arg(aceptada).toUtf8(),
        QStringLiteral(R"({"evento":"resultado","registro":"s2","codEstatus":"5002","idSolicitud":"%1"})").arg(rechazada).toUtf8(),
        QStringLiteral(R"({"evento":"resultado","registro":"v1","codEstatus":"5000","idsPaquetes":["%1","%2"]})").arg(p1, p2).toUtf8()};
    {
        // --ultima toma la ultima ACEPTADA (no la 5002) y no la muestra.
        Escenario e(*m_fx);
        escribirLedger(e.salida, base);
        e.respuestas = {QStringLiteral("no")};
        QCOMPARE(e.correr({QStringLiteral("verifica"), QStringLiteral("--ultima")}), kSalidaCancelada);
        QVERIFY(e.salidaTexto.contains(QStringLiteral("ultima solicitud aceptada del ledger: [Id]")));
        QVERIFY(!e.todo().contains(aceptada));
        QCOMPARE(e.clientesCreados, 0);
    }
    {
        // Prueba de que se resolvio la aceptada: su guardia de 15 min aplica.
        Escenario e(*m_fx);
        escribirLedger(e.salida, base + QList<QByteArray>{verificacionEn(aceptada, fixtures::ahoraVigente().addSecs(-60))});
        QCOMPARE(e.correr({QStringLiteral("verifica"), QStringLiteral("--ultima")}), kSalidaLedger);
        QVERIFY(e.errorTexto.contains(QStringLiteral("espere")));
        QVERIFY(!e.todo().contains(aceptada));
    }
    {
        Escenario e(*m_fx);
        QCOMPARE(e.correr({QStringLiteral("verifica"), QStringLiteral("--ultima")}), kSalidaUso);
        QVERIFY(e.errorTexto.contains(QStringLiteral("ninguna solicitud aceptada")));
        QCOMPARE(e.clientesCreados, 0);
    }
    {
        // --paquete 2 resuelve p2 (ya guardado -> rechazado) sin mostrarlo.
        Escenario e(*m_fx);
        escribirLedger(e.salida, base + QList<QByteArray>{
            QStringLiteral(R"({"evento":"descarga","registro":"d1","idPaquete":"%1"})").arg(p2).toUtf8(),
            QByteArray(R"({"evento":"resultado","registro":"d1","guardado":true})")});
        QCOMPARE(e.correr({QStringLiteral("descarga"), QStringLiteral("--paquete"), QStringLiteral("2")}), kSalidaLedger);
        QVERIFY(e.salidaTexto.contains(QStringLiteral("paquete 2 de 2")));
        QVERIFY(!e.todo().contains(p2));
        QCOMPARE(e.clientesCreados, 0);
    }
    {
        Escenario e(*m_fx);
        escribirLedger(e.salida, base);
        e.respuestas = {QStringLiteral("no")};
        QCOMPARE(e.correr({QStringLiteral("descarga"), QStringLiteral("--paquete"), QStringLiteral("1")}), kSalidaCancelada);
        QVERIFY(!e.todo().contains(p1));
    }
    {
        Escenario e(*m_fx);
        escribirLedger(e.salida, base);
        QCOMPARE(e.correr({QStringLiteral("descarga"), QStringLiteral("--paquete"), QStringLiteral("3")}), kSalidaUso);
        QVERIFY(e.errorTexto.contains(QStringLiteral("tiene 2 paquete(s)")));
    }
}

void TestSpikeCli::ledgerEnUsoPorOtroProcesoFallaCerrado()
{
    Escenario e(*m_fx);
    QDir().mkpath(e.salida);
    // Otra "instancia" sostiene el lock (descripcion de archivo distinta).
    const int fd = ::open(QFile::encodeName(QDir(e.salida).filePath(QStringLiteral("ledger.lock"))).constData(),
                          O_RDWR | O_CREAT, 0600);
    QVERIFY(fd >= 0);
    QCOMPARE(::flock(fd, LOCK_EX | LOCK_NB), 0);
    QCOMPARE(e.correr({QStringLiteral("solicita"), QStringLiteral("--tipo"), QStringLiteral("emitidos"),
                       QStringLiteral("--desde"), m_dia}),
             kSalidaLedger);
    QVERIFY(e.errorTexto.contains(QStringLiteral("en uso por otro proceso")));
    QCOMPARE(e.clientesCreados, 0);
    QCOMPARE(e.lineasPedidas, 0);
    ::flock(fd, LOCK_UN);
    ::close(fd);

    // Liberado el lock, la misma ejecucion avanza hasta la confirmacion.
    Escenario s(*m_fx);
    s.salida = e.salida;
    s.deps.directorioSalidaPorDefecto = e.salida;
    s.respuestas = {QStringLiteral("no")};
    QCOMPARE(s.correr({QStringLiteral("solicita"), QStringLiteral("--tipo"), QStringLiteral("emitidos"),
                       QStringLiteral("--desde"), m_dia}),
             kSalidaCancelada);
}

void TestSpikeCli::ledgerCorruptoFallaCerrado()
{
    Escenario e(*m_fx);
    escribirLedger(e.salida, {QByteArray(R"({"evento":"solicitud","registro":"a","criterio":"x1"})"),
                              QByteArray("esto no es json")});
    QCOMPARE(e.correr({QStringLiteral("solicita"), QStringLiteral("--tipo"), QStringLiteral("emitidos"),
                       QStringLiteral("--desde"), m_dia}),
             kSalidaLedger);
    QVERIFY(e.errorTexto.contains(QStringLiteral("ledger corrupto (linea 2)")));
    QCOMPARE(e.clientesCreados, 0);
    QCOMPARE(e.lineasPedidas, 0);
}

void TestSpikeCli::solicitudSinConfirmacionNoUsaRed()
{
    Escenario e(*m_fx);
    e.respuestas = {QStringLiteral("si")}; // cualquier cosa distinta de "yes"
    QCOMPARE(e.correr({QStringLiteral("solicita"), QStringLiteral("--tipo"), QStringLiteral("emitidos"),
                       QStringLiteral("--desde"), m_dia}),
             kSalidaCancelada);
    QCOMPARE(e.lineasPedidas, 1);
    QCOMPARE(e.clientesCreados, 0);
    QVERIFY(e.salidaTexto.contains(QStringLiteral("OPERACION REAL")));
    QVERIFY(!e.salidaTexto.contains(fixtures::kRfc)); // RFC enmascarado en la confirmacion
    QVERIFY(!QFileInfo::exists(QDir(e.salida).filePath(QStringLiteral("ledger.jsonl"))));
}

void TestSpikeCli::ledgerRechazaCriterioRepetidoYTope()
{
    const QString op = QStringLiteral("SolicitaDescargaEmitidos");
    {
        Escenario e(*m_fx);
        escribirLedger(e.salida, {QStringLiteral(R"({"evento":"solicitud","registro":"r1","criterio":"%1"})")
                                      .arg(hashCriterio(op, fixtures::kRfc, m_dia))
                                      .toUtf8()});
        QCOMPARE(e.correr({QStringLiteral("solicita"), QStringLiteral("--tipo"), QStringLiteral("emitidos"),
                           QStringLiteral("--desde"), m_dia}),
                 kSalidaLedger);
        QVERIFY(e.errorTexto.contains(QStringLiteral("ya se registro")));
        QCOMPARE(e.clientesCreados, 0);
        QCOMPARE(e.lineasPedidas, 0);
    }
    {
        Escenario e(*m_fx);
        escribirLedger(e.salida, {QByteArray(R"({"evento":"solicitud","registro":"a","criterio":"x1"})"),
                                  QByteArray(R"({"evento":"solicitud","registro":"b","criterio":"x2"})"),
                                  QByteArray(R"({"evento":"resultado","registro":"b","fase":"DespuesDeEnvio","incierta":true})"),
                                  QByteArray(R"({"evento":"solicitud","registro":"c","criterio":"x3"})")});
        QCOMPARE(e.correr({QStringLiteral("solicita"), QStringLiteral("--tipo"), QStringLiteral("recibidos"),
                           QStringLiteral("--desde"), m_dia}),
                 kSalidaLedger);
        QVERIFY(e.errorTexto.contains(QStringLiteral("tope de 3")));
        QCOMPARE(e.clientesCreados, 0);
    }
    {
        // Una solicitud que no llego al SAT (AntesDeEnvio) no consume el tope:
        // el flujo llega a la confirmacion (aqui rechazada).
        Escenario e(*m_fx);
        escribirLedger(e.salida, {QByteArray(R"({"evento":"solicitud","registro":"a","criterio":"x1"})"),
                                  QByteArray(R"({"evento":"solicitud","registro":"b","criterio":"x2"})"),
                                  QByteArray(R"({"evento":"solicitud","registro":"c","criterio":"x3"})"),
                                  QByteArray(R"({"evento":"resultado","registro":"c","fase":"AntesDeEnvio"})")});
        e.respuestas = {QStringLiteral("no")};
        QCOMPARE(e.correr({QStringLiteral("solicita"), QStringLiteral("--tipo"), QStringLiteral("recibidos"),
                           QStringLiteral("--desde"), m_dia}),
                 kSalidaCancelada);
        QVERIFY(e.salidaTexto.contains(QStringLiteral("2 de 3")));
        QCOMPARE(e.clientesCreados, 0);
    }
}

void TestSpikeCli::binarioDryRunConStdin()
{
    QTemporaryDir tmp;
    const QStringList args{QStringLiteral("--dry-run"), QStringLiteral("--cer"), m_fx->ruta(QStringLiteral("efirma.cer")),
                           QStringLiteral("--key"), m_fx->ruta(QStringLiteral("efirma.key")), QStringLiteral("--salida"),
                           tmp.filePath(QStringLiteral("salida"))};
    auto correr = [&](const char* binario, QByteArray& out, QByteArray& err) {
        QProcess p;
        p.start(QString::fromUtf8(binario), args);
        if (!p.waitForStarted()) {
            return -1;
        }
        p.write(m_fx->contrasena() + '\n');
        p.closeWriteChannel();
        if (!p.waitForFinished(60000) || p.exitStatus() != QProcess::NormalExit) {
            return -1;
        }
        out = p.readAllStandardOutput();
        err = p.readAllStandardError();
        return p.exitCode();
    };
    QByteArray out;
    QByteArray err;
    // Binario real: sin TTY no lee la contrasena (ninguna opcion lo evita).
    QCOMPARE(correr(SATCFDI_SAT_SPIKE_BIN, out, err), kSalidaUso);
    QVERIFY(err.contains("requiere una terminal interactiva"));
    QVERIFY(!(out + err).contains(m_fx->contrasena()));
    // Binario de prueba (misma main.cpp, definicion exclusiva de test).
    QCOMPARE(correr(SATCFDI_SAT_SPIKE_PRUEBA_BIN, out, err), kSalidaOk);
    QVERIFY(out.contains("-- SolicitaDescargaEmitidos"));
    QVERIFY(out.contains("[certificado]"));
    QVERIFY(!(out + err).contains(m_fx->contrasena()));
    QVERIFY(!(out + err).contains(fixtures::kRfc.toUtf8()));
    QVERIFY(!QFileInfo::exists(tmp.filePath(QStringLiteral("salida"))));
}

void TestSpikeCli::cleanupTestCase()
{
    m_fx.reset(); // borra los fixtures antes de comprobar
    const QStringList restos = QDir(QDir::tempPath())
                                   .entryList(QStringList{QStringLiteral("satcfdi_sat_spike_cli_tests-*")},
                                              QDir::AllEntries | QDir::NoDotAndDotDot);
    QVERIFY2(restos.isEmpty(), qPrintable(restos.join(u',')));
}

QTEST_GUILESS_MAIN(TestSpikeCli)
#include "TestSpikeCli.moc"
