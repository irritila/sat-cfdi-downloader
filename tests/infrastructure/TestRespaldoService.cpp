// T025.3: RespaldoService (captura -> contenedor -> destino -> retencion) con
// el contenedor AFB1 real, el destino POSIX real sobre QTemporaryDir y un fake
// de destino con inyeccion de errores. La captura es sintetica (sin SQLite):
// archivos con XAXX010101000, /Users/usuario y <cfdi: en un staging temporal.

#include "application/respaldo/RespaldoService.h"
#include "infrastructure/crypto/ContenedorRespaldoOpenSsl.h"
#include "infrastructure/storage/DestinoRespaldoPosix.h"

#include "fakes/FakeDestinoRespaldo.h"
#include "fakes/FakeProgramador.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QPromise>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <sys/resource.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>

#include <atomic>
#include <memory>

using namespace satcfdi;
using namespace satcfdi::respaldo;
using Estado = ResultadoRespaldo::Estado;
using CD = ErrorDestino::Categoria;

namespace {

const QUuid kInstalacion = QUuid(QStringLiteral("{6f1c2a7e-3b9d-4c5e-8f10-2a3b4c5d6e7f}"));
const QUuid kOtraInstalacion = QUuid(QStringLiteral("{11111111-2222-4333-8444-555555555555}"));
const QByteArray kMarcadores[] = {QByteArrayLiteral("XAXX010101000"), QByteArrayLiteral("/Users/usuario"),
                                  QByteArrayLiteral("<cfdi:"), QByteArrayLiteral("contrasena-sintetica")};

bool escribir(const QString& ruta, const QByteArray& datos)
{
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(datos) == datos.size();
}

QByteArray leer(const QString& ruta)
{
    QFile f(ruta);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray relleno(qsizetype n, const QByteArray& semilla)
{
    QByteArray b;
    b.reserve(n);
    for (qint64 i = 0; b.size() < n; ++i) {
        b += semilla + QByteArray::number(i);
    }
    b.truncate(n);
    return b;
}

std::optional<HuellaArchivo> huella(const QString& ruta)
{
    struct stat st {};
    if (::lstat(QFile::encodeName(ruta).constData(), &st) != 0) {
        return std::nullopt;
    }
    HuellaArchivo h;
    h.tamano = static_cast<quint64>(st.st_size);
    h.mtimeNs = static_cast<qint64>(st.st_mtimespec.tv_sec) * 1'000'000'000 + st.st_mtimespec.tv_nsec;
    h.inodo = static_cast<quint64>(st.st_ino);
    h.dispositivo = static_cast<quint64>(st.st_dev);
    return h;
}

int modo(const QString& ruta)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(ruta).constData(), &st) == 0 ? static_cast<int>(st.st_mode & 07777) : -1;
}

bool sinMarcadores(const QByteArray& b)
{
    for (const QByteArray& m : kMarcadores) {
        if (b.contains(m)) {
            return false;
        }
    }
    return true;
}

QStringList publicados(const QString& carpeta)
{
    QStringList r;
    for (const QString& n : QDir(carpeta).entryList(QDir::Files | QDir::Hidden | QDir::System)) {
        if (n.endsWith(QLatin1String(".afbak")) && !QFileInfo(QDir(carpeta).filePath(n)).isSymLink()) {
            r.append(n);
        }
    }
    r.sort();
    return r;
}

// Captura sintetica: base + ZIP en un staging bajo `raiz`.
class FuenteSintetica final : public FuenteRespaldo {
public:
    explicit FuenteSintetica(QString raiz) : m_raiz(std::move(raiz)) {}

    qint64 tamanoZip = 300 * 1024;
    int zips = 2;
    bool zipFaltante = false;
    bool pendiente = false; // capturar() devuelve un future que la prueba completa
    std::atomic<int> limpiezas{0};

    QFuture<Resultado<CapturaRespaldo, ErrorCaptura>> capturar() override
    {
        auto c = construir();
        if (!pendiente) {
            return QtFuture::makeReadyValueFuture(Resultado<CapturaRespaldo, ErrorCaptura>::exito(std::move(c)));
        }
        m_promesa = std::make_shared<QPromise<Resultado<CapturaRespaldo, ErrorCaptura>>>();
        m_promesa->start();
        m_pendiente = std::move(c);
        return m_promesa->future();
    }

    void liberar()
    {
        m_promesa->addResult(Resultado<CapturaRespaldo, ErrorCaptura>::exito(std::move(*m_pendiente)));
        m_promesa->finish();
    }

    Resultado<Exito, ErrorCaptura> limpiar(const CapturaRespaldo& c) override
    {
        limpiadorAutonomo()(c);
        return Resultado<Exito, ErrorCaptura>::exito(Exito{});
    }
    int limpiarHuerfanos() override { return 0; }
    std::function<void(const CapturaRespaldo&)> limpiadorAutonomo() const override
    {
        auto* contador = const_cast<std::atomic<int>*>(&limpiezas);
        return [contador](const CapturaRespaldo& c) {
            QDir(c.staging).removeRecursively();
            contador->fetch_add(1);
        };
    }

    QString raizPaquetes() const { return QDir(m_raiz).filePath(QStringLiteral("paquetes")); }

private:
    CapturaRespaldo construir()
    {
        CapturaRespaldo c;
        c.staging = QDir(m_raiz).filePath(QStringLiteral("captura-%1").arg(++m_n));
        const QString base = QDir(c.staging).filePath(QStringLiteral("satcfdi.sqlite3"));
        escribir(base, relleno(64 * 1024, "SQLite format 3 XAXX010101000 /Users/usuario/Archivo-Fiscal "));
        c.metadatos.versionEsquema = 7;
        c.metadatos.versionApp = QStringLiteral("0.1.0");
        c.metadatos.capturadaEnUtc = QDateTime(QDate(2026, 10, 21), QTime(12, 0), QTimeZone::UTC);
        c.entradas.append(EntradaCaptura{EntradaCaptura::Tipo::Base, kRutaRespaldoBase, c.staging,
                                         QStringLiteral("satcfdi.sqlite3"), huella(base)});
        for (int i = 0; i < zips; ++i) {
            const QString rel = QStringLiteral("XAXX010101000/2026-01/PAQ_%1.zip").arg(i);
            const QString abs = QDir(raizPaquetes()).filePath(rel);
            if (!QFileInfo::exists(abs)) {
                escribir(abs, relleno(tamanoZip, "<cfdi:Comprobante Rfc=\"XAXX010101000\"/> "));
            }
            c.entradas.append(EntradaCaptura{EntradaCaptura::Tipo::Paquete, kPrefijoRespaldoPaquetes + rel,
                                             raizPaquetes(), rel, huella(abs)});
        }
        if (zipFaltante) {
            const QString rel = QStringLiteral("XAXX010101000/2026-02/PAQ_X.zip");
            c.entradas.append(EntradaCaptura{EntradaCaptura::Tipo::Paquete, kPrefijoRespaldoPaquetes + rel,
                                             raizPaquetes(), rel, std::nullopt});
        }
        return c;
    }

    QString m_raiz;
    int m_n = 0;
    std::shared_ptr<QPromise<Resultado<CapturaRespaldo, ErrorCaptura>>> m_promesa;
    std::optional<CapturaRespaldo> m_pendiente;
};

QMutex g_mutexMensajes;
QStringList g_mensajes;
void capturarMensaje(QtMsgType, const QMessageLogContext&, const QString& m)
{
    QMutexLocker l(&g_mutexMensajes);
    g_mensajes.append(m);
}

} // namespace

class TestRespaldoService : public QObject {
    Q_OBJECT

private:
    std::shared_ptr<crypto::ContenedorRespaldoOpenSsl> cr = std::make_shared<crypto::ContenedorRespaldoOpenSsl>();
    QByteArray kekPwBytes;
    ParametrosKdf parametros;
    QByteArray kekRecBytes;
    ParametrosKdf parametros2;
    QByteArray kekPw2Bytes;
    std::unique_ptr<QTemporaryDir> tmp;
    std::unique_ptr<fakes::FakeReloj> reloj;

    KekRespaldo kekPw() const
    {
        return KekRespaldo::desdeBytes(KekRespaldo::Tipo::Contrasena, parametros,
                                       BufferSecreto::desdeBytes(kekPwBytes.constData(), 32));
    }
    KekRespaldo kekRec() const
    {
        return KekRespaldo::desdeBytes(KekRespaldo::Tipo::Recuperacion, {},
                                       BufferSecreto::desdeBytes(kekRecBytes.constData(), 32));
    }

    PeticionRespaldo peticion(std::shared_ptr<DestinoRespaldo> destino, bool contrasenaNueva = false) const
    {
        PeticionRespaldo p;
        p.destino = std::move(destino);
        p.kekContrasena = contrasenaNueva
            ? KekRespaldo::desdeBytes(KekRespaldo::Tipo::Contrasena, parametros2,
                                      BufferSecreto::desdeBytes(kekPw2Bytes.constData(), 32))
            : kekPw();
        p.kekRecuperacion = kekRec();
        return p;
    }

    std::unique_ptr<RespaldoService> servicio(FuenteRespaldo& fuente, QUuid id = kInstalacion,
                                              std::function<QString()> sufijo = {})
    {
        RespaldoService::Opciones o;
        o.idInstalacion = id;
        o.reloj = reloj->funcion();
        o.generadorSufijo = std::move(sufijo);
        o.registro = std::make_shared<RegistroPublicadosJson>(tmp->filePath(QStringLiteral("app/respaldo/publicados.json")));
        return std::make_unique<RespaldoService>(fuente, cr, std::move(o));
    }

    ResultadoRespaldo esperar(QFuture<ResultadoRespaldo> f)
    {
        if (!QTest::qWaitFor([&] { return f.isFinished(); }, 60000) || f.resultCount() == 0) {
            ResultadoRespaldo r;
            r.estado = Estado::ErrorContenedor;
            r.diagnostico = QStringLiteral("sin_resultado");
            return r;
        }
        return f.result();
    }

    QString carpeta(const QString& nombre)
    {
        const QString d = tmp->filePath(nombre);
        QDir().mkpath(d);
        return d;
    }

    // Avanza el reloj un minuto por copia: orden estable por fecha.
    void avanzar() { reloj->fijar(reloj->ahora().addSecs(60)); }

    // Contenedor valido de otra instalacion, escrito a mano.
    QByteArray respaldoDeOtraInstalacion(const QString& raiz)
    {
        QBuffer b;
        b.open(QIODevice::WriteOnly);
        DatosNuevoRespaldo d;
        d.idInstalacion = kOtraInstalacion;
        d.creadoEnUtc = QDateTime(QDate(2020, 1, 1), QTime(0, 0), QTimeZone::UTC);
        auto esc = cr->crearEscritor(b, kekPw(), kekRec(), d);
        Q_UNUSED(raiz);
        if (!esc || !esc.valor()->agregarDatos(QStringLiteral("x"), "y") || !esc.valor()->finalizar()) {
            return {};
        }
        return b.data();
    }

private slots:
    void initTestCase()
    {
        auto sal = cr->generarSal();
        QVERIFY(sal);
        parametros = ParametrosKdf::predeterminados(sal.valor());
        auto k = cr->derivarKekContrasena(BufferSecreto::desdeTexto(QStringLiteral("contrasena-sintetica-1")),
                                          parametros);
        QVERIFY(k);
        kekPwBytes = QByteArray(reinterpret_cast<const char*>(k.valor().clave().datos()), 32);
        auto clave = cr->generarClaveRecuperacion();
        QVERIFY(clave);
        auto r = cr->derivarKekRecuperacion(clave.valor());
        QVERIFY(r);
        kekRecBytes = QByteArray(reinterpret_cast<const char*>(r.valor().clave().datos()), 32);
        // D16: contrasena nueva (otra sal) con la MISMA clave de recuperacion.
        auto sal2 = cr->generarSal();
        QVERIFY(sal2);
        parametros2 = ParametrosKdf::predeterminados(sal2.valor());
        auto k2 = cr->derivarKekContrasena(BufferSecreto::desdeTexto(QStringLiteral("contrasena-sintetica-2")),
                                           parametros2);
        QVERIFY(k2);
        kekPw2Bytes = QByteArray(reinterpret_cast<const char*>(k2.valor().clave().datos()), 32);
    }

    void init()
    {
        tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(tmp->isValid());
        reloj = std::make_unique<fakes::FakeReloj>(QDateTime(QDate(2026, 10, 21), QTime(12, 0), QTimeZone::UTC));
    }

    void cleanup() { tmp.reset(); }

    // --- Flujo feliz ---------------------------------------------------------
    void respaldoConFakeYProgreso()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.zipFaltante = true;
        auto destino = std::make_shared<fakes::FakeDestinoRespaldo>();
        auto s = servicio(fuente);
        QSignalSpy progreso(s.get(), &RespaldoService::progreso);
        const auto r = esperar(s->respaldar(peticion(destino)));
        QCOMPARE(claveEstable(r.estado), claveEstable(Estado::ExitoConAdvertencias)); // un faltante
        QCOMPARE(r.faltantes, 1);
        QVERIFY(QRegularExpression(QStringLiteral("^archivo-fiscal-20261021T120000Z-[0-9a-f]{8}\\.afbak$"))
                    .match(r.nombrePublicado)
                    .hasMatch());
        QVERIFY(destino->parciales().isEmpty());
        QCOMPARE(fuente.limpiezas.load(), 1);
        const QByteArray contenido = destino->archivos().value(r.nombrePublicado);
        QVERIFY(r.tamanoEstimado >= quint64(contenido.size()));
        QBuffer b;
        b.setData(contenido);
        b.open(QIODevice::ReadOnly);
        auto v = cr->verificar(b, kekPw());
        QVERIFY(v);
        QCOMPARE(v.valor().cabecera.idInstalacion, kInstalacion);
        QCOMPARE(v.valor().faltantes, 1u);
        QVERIFY(QTest::qWaitFor([&] { return !progreso.isEmpty(); }, 5000));
        QVERIFY(QTest::qWaitFor([&] {
            return progreso.last().at(0).value<RespaldoService::Fase>() == RespaldoService::Fase::Depuracion;
        }, 5000));
        bool hayEscritura = false;
        for (const auto& a : progreso) {
            if (a.at(0).value<RespaldoService::Fase>() == RespaldoService::Fase::Escritura && a.at(2).toULongLong() > 0) {
                hayEscritura = true;
                QVERIFY(a.at(1).toULongLong() <= a.at(2).toULongLong());
            }
        }
        QVERIFY(hayEscritura);
    }

    // --- Errores del destino (criterio 1) -------------------------------------
    void erroresDelDestino_data()
    {
        QTest::addColumn<int>("falla");
        QTest::addColumn<int>("estado");
        QTest::addColumn<int>("categoria");
        using F = fakes::FakeDestinoRespaldo::Falla;
        QTest::newRow("carpeta inexistente") << int(F::CarpetaInexistente) << int(Estado::ErrorDestino) << int(CD::CarpetaInexistente);
        QTest::newRow("sin escritura") << int(F::SinEscritura) << int(Estado::ErrorDestino) << int(CD::SinEscritura);
        QTest::newRow("volumen desmontado") << int(F::VolumenDesmontado) << int(Estado::ErrorDestino) << int(CD::VolumenNoDisponible);
        QTest::newRow("disco lleno al validar") << int(F::SinEspacioAlValidar) << int(Estado::ErrorDestino) << int(CD::EspacioInsuficiente);
        QTest::newRow("disco lleno al escribir") << int(F::SinEspacioAlEscribir) << int(Estado::ErrorDestino) << int(CD::EspacioInsuficiente);
        QTest::newRow("fallo en fsync") << int(F::Fsync) << int(Estado::ErrorPublicacion) << int(CD::Sincronizacion);
        QTest::newRow("fallo en rename") << int(F::Rename) << int(Estado::ErrorPublicacion) << int(CD::Publicacion);
        QTest::newRow("rename exclusivo no soportado") << int(F::ExclusivoNoSoportado) << int(Estado::ErrorPublicacion)
                                                       << int(CD::ExclusivoNoSoportado);
        QTest::newRow("carpeta cambiada") << int(F::CarpetaCambiada) << int(Estado::ErrorDestino) << int(CD::CarpetaCambiada);
    }

    void erroresDelDestino()
    {
        QFETCH(int, falla);
        QFETCH(int, estado);
        QFETCH(int, categoria);
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        auto destino = std::make_shared<fakes::FakeDestinoRespaldo>();
        destino->falla = static_cast<fakes::FakeDestinoRespaldo::Falla>(falla);
        destino->ponerArchivo(QStringLiteral(".otra-app.partial"), "ajeno");
        auto s = servicio(fuente);
        const auto r = esperar(s->respaldar(peticion(destino)));
        QCOMPARE(claveEstable(r.estado), claveEstable(static_cast<Estado>(estado)));
        QVERIFY(r.errorDestino);
        QCOMPARE(claveEstable(*r.errorDestino), claveEstable(static_cast<CD>(categoria)));
        // Solo se borro el .partial propio; nada publicado; captura limpia.
        QCOMPARE(destino->parciales(), QStringList{QStringLiteral(".otra-app.partial")});
        QCOMPARE(destino->archivos().size(), qsizetype(1));
        QCOMPARE(fuente.limpiezas.load(), 1);
    }

    void espacioInsuficienteConMargen()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        auto destino = std::make_shared<fakes::FakeDestinoRespaldo>();
        destino->bytesLibres = 1024; // menos que el estimado + margen
        auto s = servicio(fuente);
        const auto r = esperar(s->respaldar(peticion(destino)));
        QCOMPARE(claveEstable(r.estado), claveEstable(Estado::ErrorDestino));
        QCOMPARE(claveEstable(*r.errorDestino), claveEstable(CD::EspacioInsuficiente));
        QVERIFY(r.tamanoEstimado > 600 * 1024);
        QVERIFY(RespaldoService::espacioNecesario(r.tamanoEstimado) > r.tamanoEstimado);
    }

    // --- D18 -------------------------------------------------------------------
    void respaldoPedidoDuranteOtroNoIniciaOtro()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.pendiente = true;
        auto destino = std::make_shared<fakes::FakeDestinoRespaldo>();
        auto s = servicio(fuente);
        auto primero = s->respaldar(peticion(destino));
        QVERIFY(s->enCurso());
        const auto segundo = esperar(s->respaldar(peticion(destino)));
        QCOMPARE(claveEstable(segundo.estado), claveEstable(Estado::YaEnCurso));
        fuente.liberar();
        QVERIFY(esperar(primero).exito());
        QVERIFY(QTest::qWaitFor([&] { return !s->enCurso(); }, 5000));
        fuente.pendiente = false;
        avanzar();
        QVERIFY(esperar(s->respaldar(peticion(destino))).exito());
        QCOMPARE(destino->validaciones(), 2);
    }

    // --- Cancelacion en cada fase ------------------------------------------------
    void cancelacionDuranteLaCaptura()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.pendiente = true;
        auto destino = std::make_shared<fakes::FakeDestinoRespaldo>();
        auto s = servicio(fuente);
        auto f = s->respaldar(peticion(destino));
        s->cancelar();
        fuente.liberar();
        const auto r = esperar(f);
        QCOMPARE(claveEstable(r.estado), claveEstable(Estado::Cancelado));
        QVERIFY(destino->archivos().isEmpty());
        QCOMPARE(destino->validaciones(), 0);
        QCOMPARE(fuente.limpiezas.load(), 1);
    }

    void cancelacionDuranteElCifrado()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.tamanoZip = 4 * 1024 * 1024;
        auto destino = std::make_shared<fakes::FakeDestinoRespaldo>();
        auto s = servicio(fuente);
        RespaldoService* ps = s.get();
        connect(ps, &RespaldoService::progreso, ps,
                [ps](RespaldoService::Fase fase, quint64 bytes, quint64) {
                    if (fase == RespaldoService::Fase::Escritura && bytes > 0) {
                        ps->cancelar(); // desde el hilo del servicio, a mitad de escritura
                    }
                },
                Qt::DirectConnection);
        const auto r = esperar(s->respaldar(peticion(destino)));
        QCOMPARE(claveEstable(r.estado), claveEstable(Estado::Cancelado));
        QVERIFY(destino->archivos().isEmpty());
        QCOMPARE(fuente.limpiezas.load(), 1);
    }

    void cancelacionAntesDePublicar()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        auto destino = std::make_shared<fakes::FakeDestinoRespaldo>();
        auto s = servicio(fuente);
        RespaldoService* ps = s.get();
        destino->alSincronizar = [ps] { ps->cancelar(); };
        const auto r = esperar(s->respaldar(peticion(destino)));
        QCOMPARE(claveEstable(r.estado), claveEstable(Estado::Cancelado));
        QVERIFY(destino->archivos().isEmpty());
    }

    void cerrarConCapturaPendiente()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.pendiente = true;
        auto destino = std::make_shared<fakes::FakeDestinoRespaldo>();
        auto s = servicio(fuente);
        auto f = s->respaldar(peticion(destino));
        s->cerrar(); // no se bloquea esperando la captura
        QVERIFY(f.isFinished());
        QCOMPARE(claveEstable(f.result().estado), claveEstable(Estado::Detenido));
        QCOMPARE(claveEstable(esperar(s->respaldar(peticion(destino))).estado), claveEstable(Estado::Detenido));
        s.reset();
        fuente.liberar(); // la captura termina despues: su limpiador la borra
        QVERIFY(QTest::qWaitFor([&] { return fuente.limpiezas.load() == 1; }, 5000));
        QVERIFY(destino->archivos().isEmpty());
    }

    // --- Destino POSIX real -----------------------------------------------------
    void conflictoDeNombreNuncaSobrescribe()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        const QString dir = carpeta(QStringLiteral("nube"));
        const QString ocupado = QStringLiteral("archivo-fiscal-20261021T120000Z-aaaaaaaa.afbak");
        QVERIFY(escribir(QDir(dir).filePath(ocupado), "AJENO-NO-TOCAR"));
        int llamadas = 0;
        auto s = servicio(fuente, kInstalacion, [&llamadas] {
            return ++llamadas == 1 ? QStringLiteral("aaaaaaaa") : QStringLiteral("bbbbbbbb");
        });
        const auto r = esperar(s->respaldar(peticion(std::make_shared<DestinoRespaldoPosix>(dir))));
        QVERIFY2(r.exito(), qPrintable(r.diagnostico));
        QCOMPARE(r.nombrePublicado, QStringLiteral("archivo-fiscal-20261021T120000Z-bbbbbbbb.afbak"));
        QCOMPARE(leer(QDir(dir).filePath(ocupado)), QByteArrayLiteral("AJENO-NO-TOCAR"));
    }

    void sextaCopiaDejaLasCincoMasRecientes()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.tamanoZip = 1024;
        const QString dir = carpeta(QStringLiteral("nube"));
        auto destino = std::make_shared<DestinoRespaldoPosix>(dir);
        auto s = servicio(fuente);
        QStringList nombres;
        for (int i = 0; i < 6; ++i) {
            const auto r = esperar(s->respaldar(peticion(destino)));
            QVERIFY2(r.estado == Estado::Exito, qPrintable(claveEstable(r.estado) + QLatin1Char(' ') + r.diagnostico));
            QCOMPARE(r.depurados, i == 5 ? 1 : 0);
            nombres.append(r.nombrePublicado);
            avanzar();
        }
        nombres.removeFirst();
        nombres.sort();
        QCOMPARE(publicados(dir), nombres);
    }

    void retencionIgnoraAjenosEnlacesParcialesEIdsRepetidos()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.tamanoZip = 1024;
        const QString dir = carpeta(QStringLiteral("nube"));
        const QString fuera = carpeta(QStringLiteral("fuera"));
        auto destino = std::make_shared<DestinoRespaldoPosix>(dir);
        // Ajenos: AFB1 valido de otra instalacion, basura .afbak, enlace,
        // .partial de otro dueno y .partial cualquiera.
        QVERIFY(escribir(QDir(dir).filePath(QStringLiteral("archivo-fiscal-otra-mac.afbak")),
                         respaldoDeOtraInstalacion(dir)));
        QVERIFY(escribir(QDir(dir).filePath(QStringLiteral("archivo-fiscal-basura.afbak")), "no es AFB1"));
        QVERIFY(escribir(QDir(dir).filePath(QStringLiteral(".archivo-fiscal-ffffffff-0123456789abcdef.partial")), "x"));
        QVERIFY(escribir(QDir(dir).filePath(QStringLiteral("otro.partial")), "x"));
        auto s = servicio(fuente);
        QStringList propios;
        for (int i = 0; i < 6; ++i) {
            const auto r = esperar(s->respaldar(peticion(destino)));
            QVERIFY2(r.exito(), qPrintable(r.diagnostico));
            propios.append(r.nombrePublicado);
            if (i == 0) {
                // Copia con la cabecera (y el bloque 0) del primero: mismo
                // idRespaldo, autentica. Y un enlace a el desde fuera.
                const QByteArray primero = leer(QDir(dir).filePath(r.nombrePublicado));
                QVERIFY(primero.size() < 204 + 5 + 1024 * 1024 + 16); // un solo bloque
                QVERIFY(escribir(QDir(dir).filePath(QStringLiteral("archivo-fiscal-copia.afbak")), primero));
                QVERIFY(escribir(QDir(fuera).filePath(QStringLiteral("objetivo.afbak")), primero));
                QVERIFY(QFile::link(QDir(fuera).filePath(QStringLiteral("objetivo.afbak")),
                                    QDir(dir).filePath(QStringLiteral("archivo-fiscal-enlace.afbak"))));
            }
            // B2: el id repetido se avisa una sola vez (en la segunda) y nunca
            // se borra (la sexta no depura el grupo mas viejo).
            QCOMPARE(r.duplicadosProveedor, i == 1 ? 1 : 0);
            if (i == 1) {
                QCOMPARE(claveEstable(r.estado), claveEstable(Estado::ExitoConAdvertencias));
            }
            if (i == 5) {
                QCOMPARE(claveEstable(r.estado), claveEstable(Estado::Exito));
                QCOMPARE(r.depurados, 0);
            }
            avanzar();
        }
        const QStringList quedan = QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const QString& n : propios) {
            QVERIFY2(quedan.contains(n), qPrintable(n));
        }
        for (const char* ajeno : {"archivo-fiscal-otra-mac.afbak", "archivo-fiscal-basura.afbak", "archivo-fiscal-copia.afbak",
                                  "archivo-fiscal-enlace.afbak", ".archivo-fiscal-ffffffff-0123456789abcdef.partial",
                                  "otro.partial"}) {
            QVERIFY2(quedan.contains(QString::fromLatin1(ajeno)), ajeno);
        }
        QVERIFY(QFileInfo(QDir(dir).filePath(QStringLiteral("archivo-fiscal-enlace.afbak"))).isSymLink());
        QVERIFY(QFileInfo::exists(QDir(fuera).filePath(QStringLiteral("objetivo.afbak"))));

        // Sin el duplicado, la siguiente depura los dos mas viejos (5 quedan).
        QVERIFY(QFile::remove(QDir(dir).filePath(QStringLiteral("archivo-fiscal-copia.afbak"))));
        const auto r = esperar(s->respaldar(peticion(destino)));
        QVERIFY(r.exito());
        QCOMPARE(r.depurados, 2);
        QVERIFY(!QFileInfo::exists(QDir(dir).filePath(propios.at(0))));
        QVERIFY(!QFileInfo::exists(QDir(dir).filePath(propios.at(1))));
        QVERIFY(QFileInfo(QDir(dir).filePath(QStringLiteral("archivo-fiscal-enlace.afbak"))).isSymLink());
        QVERIFY(QFileInfo::exists(QDir(dir).filePath(QStringLiteral("archivo-fiscal-otra-mac.afbak"))));
    }

    void terminacionForzadaYLimpiezaDeParcialesPropios()
    {
        const QString dir = carpeta(QStringLiteral("nube"));
        const QString fuera = carpeta(QStringLiteral("fuera"));
        auto destino = std::make_shared<DestinoRespaldoPosix>(dir);
        const QString propietario = RespaldoService::propietarioDe(kInstalacion);
        QString huerfano;
        {
            auto p = destino->crearParcial(propietario);
            QVERIFY(p);
            huerfano = p.valor()->nombre();
            p.valor()->dispositivo().write("datos a medias");
            // Se destruye sin publicar ni descartar: como una terminacion forzada.
        }
        QVERIFY(QFileInfo::exists(QDir(dir).filePath(huerfano)));
        QCOMPARE(QFileInfo(QDir(dir).filePath(huerfano)).permissions() & QFile::ReadOther, QFileDevice::Permissions{});
        // Un enlace con nombre de .partial propio y un .partial de otro dueno.
        const QString enlace = nombres::prefijoParcial(propietario) + QStringLiteral("00000000000000ff.partial");
        QVERIFY(escribir(QDir(fuera).filePath(QStringLiteral("victima")), "NO-TOCAR"));
        QVERIFY(QFile::link(QDir(fuera).filePath(QStringLiteral("victima")), QDir(dir).filePath(enlace)));
        const QString ajeno = QStringLiteral(".archivo-fiscal-ffffffff-0123456789abcdef.partial");
        QVERIFY(escribir(QDir(dir).filePath(ajeno), "x"));

        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        auto s = servicio(fuente);
        auto f = s->limpiarPartialPropios(destino);
        QVERIFY(QTest::qWaitFor([&] { return f.isFinished(); }, 5000));
        QCOMPARE(f.result(), 1);
        QVERIFY(!QFileInfo::exists(QDir(dir).filePath(huerfano)));
        QVERIFY(QFileInfo(QDir(dir).filePath(enlace)).isSymLink());
        QCOMPARE(leer(QDir(fuera).filePath(QStringLiteral("victima"))), QByteArrayLiteral("NO-TOCAR"));
        QVERIFY(QFileInfo::exists(QDir(dir).filePath(ajeno)));
    }

    void sinTextoClaroEnParcialNiPublicadoNiMensajes()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.tamanoZip = 3 * 1024 * 1024;
        const QString dir = carpeta(QStringLiteral("nube"));
        auto s = servicio(fuente);
        QByteArray parcialLeido;
        RespaldoService* ps = s.get();
        connect(ps, &RespaldoService::progreso, ps,
                [&](RespaldoService::Fase fase, quint64 bytes, quint64) {
                    if (fase == RespaldoService::Fase::Escritura && bytes > 2 * 1024 * 1024 && parcialLeido.isEmpty()) {
                        for (const QString& n : QDir(dir).entryList(QDir::Files | QDir::Hidden)) {
                            if (n.endsWith(QLatin1String(".partial"))) {
                                parcialLeido = leer(QDir(dir).filePath(n));
                            }
                        }
                    }
                },
                Qt::DirectConnection);
        {
            QMutexLocker l(&g_mutexMensajes);
            g_mensajes.clear();
        }
        const QtMessageHandler previo = qInstallMessageHandler(capturarMensaje);
        const auto r = esperar(s->respaldar(peticion(std::make_shared<DestinoRespaldoPosix>(dir))));
        qInstallMessageHandler(previo);
        QVERIFY2(r.exito(), qPrintable(r.diagnostico));
        {
            QMutexLocker l(&g_mutexMensajes);
            QVERIFY2(g_mensajes.isEmpty(), qPrintable(g_mensajes.join(QLatin1Char('\n'))));
        }
        QVERIFY(parcialLeido.size() > 2 * 1024 * 1024);
        QVERIFY(sinMarcadores(parcialLeido));
        const QByteArray publicado = leer(QDir(dir).filePath(r.nombrePublicado));
        QVERIFY(publicado.size() > 6 * 1024 * 1024);
        QVERIFY(sinMarcadores(publicado));
        QVERIFY(!r.nombrePublicado.contains(QLatin1String("XAXX")));
        QCOMPARE(QFileInfo(QDir(dir).filePath(r.nombrePublicado)).permissions() & QFile::ReadOther,
                 QFileDevice::Permissions{});
    }

    // --- D20: retencion sin descargar nunca --------------------------------------
    void retencionDeCopiasSoloEnLaNubePorRegistro()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.tamanoZip = 1024;
        auto destino = std::make_shared<fakes::FakeDestinoRespaldo>();
        // El "proveedor" deja en la nube (sin descargar) toda copia anterior a
        // la recien publicada.
        fakes::FakeDestinoRespaldo* d = destino.get();
        destino->alPublicar = [d](const QString& nuevo) {
            for (const QString& n : d->publicados()) {
                if (n != nuevo) {
                    d->marcarSinDescargar(n);
                }
            }
        };
        auto s = servicio(fuente);
        QStringList nombres;
        for (int i = 0; i < 7; ++i) {
            const auto r = esperar(s->respaldar(peticion(destino)));
            QVERIFY2(r.estado == Estado::Exito, qPrintable(claveEstable(r.estado) + QLatin1Char(' ') + r.diagnostico));
            QCOMPARE(r.depurados, i >= 5 ? 1 : 0);
            nombres.append(r.nombrePublicado);
            avanzar();
        }
        QCOMPARE(destino->lecturasSinDescargar(), 0); // nunca se intento leer (descargar) una copia
        nombres = nombres.mid(2);
        nombres.sort();
        QCOMPARE(destino->publicados(), nombres);
        // El registro solo conserva las 5 que quedan.
        auto estado = RegistroPublicadosJson(tmp->filePath(QStringLiteral("app/respaldo/publicados.json"))).cargar();
        QVERIFY(estado);
        QCOMPARE(estado.valor().publicados.size(), qsizetype(5));
    }

    void copiaSinDescargarSinRegistroNoSeToca()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.tamanoZip = 1024;
        auto destino = std::make_shared<fakes::FakeDestinoRespaldo>();
        auto s = servicio(fuente);
        // Seis copias propias en disco, despues: la mas vieja "solo en la nube"
        // pero sin registro (otra Mac, registro perdido) y la segunda con su
        // archivo reemplazado (otro inodo).
        QStringList nombres;
        for (int i = 0; i < 5; ++i) {
            const auto r = esperar(s->respaldar(peticion(destino)));
            QVERIFY(r.estado == Estado::Exito);
            nombres.append(r.nombrePublicado);
            avanzar();
        }
        QVERIFY(QFile::remove(tmp->filePath(QStringLiteral("app/respaldo/publicados.json"))));
        const auto r1 = esperar(s->respaldar(peticion(destino))); // re-registra solo la nueva
        QVERIFY(r1.exito());
        QCOMPARE(r1.depurados, 1); // la mas vieja aun en disco: se autentica y se borra
        nombres.removeFirst();
        avanzar();
        destino->marcarSinDescargar(nombres.at(0)); // sin registro
        destino->marcarSinDescargar(r1.nombrePublicado);
        destino->cambiarInodo(r1.nombrePublicado); // registrada, pero ya no es el mismo archivo
        const auto r2 = esperar(s->respaldar(peticion(destino)));
        QCOMPARE(claveEstable(r2.estado), claveEstable(Estado::ExitoConAdvertencias));
        QCOMPARE(r2.copiasSinRegistro, 2);
        QCOMPARE(r2.depurados, 0); // quedan 5 contables: nada que borrar, y las 2 no se tocan
        QVERIFY(destino->archivos().contains(nombres.at(0)));
        QVERIFY(destino->archivos().contains(r1.nombrePublicado));
        QCOMPARE(destino->lecturasSinDescargar(), 0);
    }

    // --- D16: retencion tras cambiar la contrasena ---------------------------------
    void cambioDeContrasenaLasAnterioresCuentanPorRecuperacion_data()
    {
        QTest::addColumn<int>("tipoDestino");
        QTest::newRow("posix") << 0;
        QTest::newRow("fake con lectura secuencial (sin seek)") << 1;
    }

    void cambioDeContrasenaLasAnterioresCuentanPorRecuperacion()
    {
        QFETCH(int, tipoDestino);
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.tamanoZip = 1024;
        const QString dir = carpeta(QStringLiteral("nube"));
        std::shared_ptr<DestinoRespaldo> destino;
        std::shared_ptr<fakes::FakeDestinoRespaldo> fake;
        if (tipoDestino == 0) {
            destino = std::make_shared<DestinoRespaldoPosix>(dir);
        } else {
            fake = std::make_shared<fakes::FakeDestinoRespaldo>();
            fake->lecturaSecuencial = true;
            destino = fake;
        }
        auto s = servicio(fuente);
        QStringList nombres;
        for (int i = 0; i < 7; ++i) {
            // Las 5 primeras con la contrasena anterior; despues, la nueva.
            const auto r = esperar(s->respaldar(peticion(destino, i >= 5)));
            QVERIFY2(r.estado == Estado::Exito, qPrintable(claveEstable(r.estado) + QLatin1Char(' ') + r.diagnostico));
            // Las anteriores solo autentican con la clave de recuperacion: aun
            // asi cuentan y la sexta y la septima depuran una cada una.
            QCOMPARE(r.depurados, i >= 5 ? 1 : 0);
            nombres.append(r.nombrePublicado);
            avanzar();
        }
        const QStringList quedan = tipoDestino == 0 ? publicados(dir) : fake->publicados();
        QStringList esperados = nombres.mid(2);
        esperados.sort();
        QCOMPARE(quedan, esperados);
    }

    // --- N1: politica de materializacion acotada ---------------------------------
    void politicaSinMaterializarSoloDuranteLaLecturaDeCabeceras()
    {
        const int politicaInicial = ::getiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_THREAD);
        QVERIFY(politicaInicial != IOPOL_MATERIALIZE_DATALESS_FILES_OFF);
        const QString dir = carpeta(QStringLiteral("nube"));
        QVERIFY(escribir(QDir(dir).filePath(QStringLiteral("archivo-fiscal-a.afbak")), "x"));
        QVERIFY(QFile::link(QDir(dir).filePath(QStringLiteral("archivo-fiscal-a.afbak")),
                            QDir(dir).filePath(QStringLiteral("archivo-fiscal-enlace.afbak"))));
        DestinoRespaldoPosix d(dir);
        QVERIFY(d.abrir());
        QCOMPARE(::getiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_THREAD), politicaInicial);
        {
            auto f = d.abrirParaLeer(QStringLiteral("archivo-fiscal-a.afbak"));
            QVERIFY(f);
            QCOMPARE(::getiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_THREAD),
                     IOPOL_MATERIALIZE_DATALESS_FILES_OFF);
        }
        QCOMPARE(::getiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_THREAD), politicaInicial);
        QVERIFY(!d.abrirParaLeer(QStringLiteral("archivo-fiscal-enlace.afbak"))); // error: tambien restaura
        QCOMPARE(::getiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_THREAD), politicaInicial);

        // En el servicio: la escritura y el inicio de la depuracion corren con
        // la politica normal (la desactivacion solo vive en cada lectura).
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.tamanoZip = 2 * 1024 * 1024;
        auto s = servicio(fuente);
        std::atomic<int> durante{-99};
        std::atomic<int> depuracion{-99};
        RespaldoService* ps = s.get();
        connect(ps, &RespaldoService::progreso, ps,
                [&](RespaldoService::Fase fase, quint64 bytes, quint64) {
                    const int p = ::getiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_THREAD);
                    if (fase == RespaldoService::Fase::Escritura && bytes > 0) {
                        durante.store(p);
                    } else if (fase == RespaldoService::Fase::Depuracion) {
                        depuracion.store(p);
                    }
                },
                Qt::DirectConnection);
        for (int i = 0; i < 2; ++i) { // la segunda ya lee cabeceras en la retencion
            QVERIFY(esperar(s->respaldar(peticion(std::make_shared<DestinoRespaldoPosix>(dir)))).exito());
            QVERIFY(durante.load() != -99 && durante.load() != IOPOL_MATERIALIZE_DATALESS_FILES_OFF);
            QVERIFY(depuracion.load() != -99 && depuracion.load() != IOPOL_MATERIALIZE_DATALESS_FILES_OFF);
            avanzar();
        }
    }

    void edeadlkEsSinDescargar()
    {
        QCOMPARE(claveEstable(DestinoRespaldoPosix::categoriaDeErrno(EDEADLK, CD::Escritura)),
                 claveEstable(CD::SinDescargar));
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        const QString dir = carpeta(QStringLiteral("nube"));
        DestinoRespaldoPosix::Opciones o;
        o.renombrar = [](int, const char*, int, const char*, unsigned) {
            errno = EDEADLK;
            return -1;
        };
        auto s = servicio(fuente);
        const auto r = esperar(s->respaldar(peticion(std::make_shared<DestinoRespaldoPosix>(dir, o))));
        QCOMPARE(claveEstable(r.estado), claveEstable(Estado::ErrorPublicacion));
        QCOMPARE(claveEstable(*r.errorDestino), claveEstable(CD::SinDescargar));
        QCOMPARE(r.diagnostico, QStringLiteral("carpeta.sin_descargar"));
        QVERIFY(QDir(dir).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }

    // --- M2 tras publicar --------------------------------------------------------
    void carpetaCambiadaTrasPublicarConservaLaCopiaYNoDepura()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.tamanoZip = 1024;
        auto destino = std::make_shared<fakes::FakeDestinoRespaldo>();
        auto s = servicio(fuente);
        for (int i = 0; i < 5; ++i) {
            QVERIFY(esperar(s->respaldar(peticion(destino))).estado == Estado::Exito);
            avanzar();
        }
        // Sexta: la comprobacion previa a publicar pasa; la previa a depurar no.
        destino->fallarIdentidadTras = destino->m_comprobaciones + 1;
        const auto r = esperar(s->respaldar(peticion(destino)));
        QCOMPARE(claveEstable(r.estado), claveEstable(Estado::ExitoConAdvertencias));
        QVERIFY(r.avisoDepuracion);
        QCOMPARE(r.depurados, 0);
        QVERIFY(destino->archivos().contains(r.nombrePublicado));
        QCOMPARE(destino->publicados().size(), qsizetype(6));
        QVERIFY(destino->parciales().isEmpty());
    }

    // --- POSIX: D21, M1, M2, M3, B1, enlaces -------------------------------------
    void parcialEnLosDatosDeLaAppYFallbackOtroVolumen_data()
    {
        QTest::addColumn<bool>("otroVolumen");
        QTest::newRow("mismo volumen: datos de la app") << false;
        QTest::newRow("otro volumen (simulado): en la carpeta") << true;
    }

    void parcialEnLosDatosDeLaAppYFallbackOtroVolumen()
    {
        QFETCH(bool, otroVolumen);
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.tamanoZip = 3 * 1024 * 1024;
        const QString dir = carpeta(QStringLiteral("nube"));
        const QString parciales = tmp->filePath(QStringLiteral("app/respaldo/parciales"));
        DestinoRespaldoPosix::Opciones o;
        o.carpetaParciales = parciales;
        o.simularParcialesEnOtroVolumen = otroVolumen;
        auto destino = std::make_shared<DestinoRespaldoPosix>(dir, o);
        auto s = servicio(fuente);
        QStringList enCarpeta;
        QStringList enDatos;
        RespaldoService* ps = s.get();
        connect(ps, &RespaldoService::progreso, ps,
                [&](RespaldoService::Fase fase, quint64 bytes, quint64) {
                    if (fase == RespaldoService::Fase::Escritura && bytes > 1024 * 1024 && enCarpeta.isEmpty() && enDatos.isEmpty()) {
                        enCarpeta = QDir(dir).entryList(QStringList{QStringLiteral("*.partial")}, QDir::Files | QDir::Hidden);
                        enDatos = QDir(parciales).entryList(QStringList{QStringLiteral("*.partial")}, QDir::Files | QDir::Hidden);
                    }
                },
                Qt::DirectConnection);
        const auto r = esperar(s->respaldar(peticion(destino)));
        QVERIFY2(r.exito(), qPrintable(r.diagnostico));
        QCOMPARE(enCarpeta.size(), qsizetype(otroVolumen ? 1 : 0));
        QCOMPARE(enDatos.size(), qsizetype(otroVolumen ? 0 : 1));
        QCOMPARE(modo(parciales), 0700);
        QCOMPARE(publicados(dir), QStringList{r.nombrePublicado});
        QVERIFY(QDir(parciales).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }

    void limpiarParcialesCubreAmbasUbicaciones()
    {
        const QString dir = carpeta(QStringLiteral("nube"));
        const QString parciales = tmp->filePath(QStringLiteral("app/respaldo/parciales"));
        const QString propietario = RespaldoService::propietarioDe(kInstalacion);
        DestinoRespaldoPosix::Opciones local;
        local.carpetaParciales = parciales;
        DestinoRespaldoPosix::Opciones remoto = local;
        remoto.simularParcialesEnOtroVolumen = true;
        for (const auto& o : {local, remoto}) {
            DestinoRespaldoPosix d(dir, o);
            auto p = d.crearParcial(propietario);
            QVERIFY(p);
            p.valor()->dispositivo().write("a medias"); // terminacion forzada: queda
        }
        QCOMPARE(QDir(parciales).entryList(QDir::Files | QDir::Hidden).size(), qsizetype(1));
        QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden).size(), qsizetype(1));
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        auto s = servicio(fuente);
        auto f = s->limpiarPartialPropios(std::make_shared<DestinoRespaldoPosix>(dir, local));
        QVERIFY(QTest::qWaitFor([&] { return f.isFinished(); }, 5000));
        QCOMPARE(f.result(), 2);
        QVERIFY(QDir(parciales).entryList(QDir::Files | QDir::Hidden).isEmpty());
        QVERIFY(QDir(dir).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }

    void renameExclusivoNoSoportadoNoReintentaNiSobrescribe()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        const QString dir = carpeta(QStringLiteral("nube"));
        int llamadas = 0;
        DestinoRespaldoPosix::Opciones o;
        o.carpetaParciales = tmp->filePath(QStringLiteral("app/respaldo/parciales"));
        o.renombrar = [&llamadas](int, const char*, int, const char*, unsigned flags) {
            ++llamadas;
            errno = (flags & RENAME_EXCL) ? ENOTSUP : 0;
            return -1;
        };
        auto s = servicio(fuente);
        const auto r = esperar(s->respaldar(peticion(std::make_shared<DestinoRespaldoPosix>(dir, o))));
        QCOMPARE(claveEstable(r.estado), claveEstable(Estado::ErrorPublicacion));
        QCOMPARE(claveEstable(*r.errorDestino), claveEstable(CD::ExclusivoNoSoportado));
        QCOMPARE(r.diagnostico, QStringLiteral("publicar.excl_no_soportado"));
        QCOMPARE(llamadas, 1);
        QVERIFY(QDir(dir).entryList(QDir::Files | QDir::Hidden).isEmpty());
        QVERIFY(QDir(o.carpetaParciales).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }

    void erroresDeRedSonVolumenNoDisponible()
    {
        for (int codigo : {ENOTCONN, ESTALE, ETIMEDOUT}) {
            QCOMPARE(claveEstable(DestinoRespaldoPosix::categoriaDeErrno(codigo, CD::Escritura)),
                     claveEstable(CD::VolumenNoDisponible));
        }
        QCOMPARE(claveEstable(DestinoRespaldoPosix::categoriaDeErrno(ENOSPC, CD::Escritura)),
                 claveEstable(CD::EspacioInsuficiente));
        QCOMPARE(claveEstable(DestinoRespaldoPosix::categoriaDeErrno(EIO, CD::Escritura)), claveEstable(CD::Escritura));
        // Red caida al publicar.
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        const QString dir = carpeta(QStringLiteral("nube"));
        DestinoRespaldoPosix::Opciones o;
        o.renombrar = [](int, const char*, int, const char*, unsigned) {
            errno = ETIMEDOUT;
            return -1;
        };
        auto s = servicio(fuente);
        const auto r = esperar(s->respaldar(peticion(std::make_shared<DestinoRespaldoPosix>(dir, o))));
        QCOMPARE(claveEstable(r.estado), claveEstable(Estado::ErrorPublicacion));
        QCOMPARE(claveEstable(*r.errorDestino), claveEstable(CD::VolumenNoDisponible));
        QVERIFY(QDir(dir).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }

    void carpetaCambiadaEntreFasesAborta()
    {
        FuenteSintetica fuente(tmp->filePath(QStringLiteral("src")));
        fuente.tamanoZip = 3 * 1024 * 1024;
        const QString dir = carpeta(QStringLiteral("nube"));
        const QString vieja = tmp->filePath(QStringLiteral("nube-vieja"));
        DestinoRespaldoPosix::Opciones o;
        o.carpetaParciales = tmp->filePath(QStringLiteral("app/respaldo/parciales"));
        auto s = servicio(fuente);
        bool cambiada = false;
        RespaldoService* ps = s.get();
        connect(ps, &RespaldoService::progreso, ps,
                [&](RespaldoService::Fase fase, quint64 bytes, quint64) {
                    if (fase == RespaldoService::Fase::Escritura && bytes > 0 && !cambiada) {
                        cambiada = QDir().rename(dir, vieja) && QDir().mkpath(dir);
                    }
                },
                Qt::DirectConnection);
        const auto r = esperar(s->respaldar(peticion(std::make_shared<DestinoRespaldoPosix>(dir, o))));
        QVERIFY(cambiada);
        QCOMPARE(claveEstable(r.estado), claveEstable(Estado::ErrorDestino));
        QCOMPARE(claveEstable(*r.errorDestino), claveEstable(CD::CarpetaCambiada));
        QVERIFY(QDir(dir).entryList(QDir::Files | QDir::Hidden).isEmpty());
        QVERIFY(QDir(vieja).entryList(QDir::Files | QDir::Hidden).isEmpty());
        QVERIFY(QDir(o.carpetaParciales).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }

    void volumenSeDeduceDelDescriptor()
    {
        const QString dir = carpeta(QStringLiteral("nube"));
        // La ruta real dice /Volumes/<n>, pero el descriptor esta en el disco de
        // arranque: punto de montaje vacio = volumen no disponible.
        DestinoRespaldoPosix::Opciones o;
        o.rutaDeDescriptor = [](int) { return QStringLiteral("/Volumes/XAXX-FALSO/Respaldos"); };
        auto falso = DestinoRespaldoPosix(dir, o).abrir();
        QVERIFY(!falso);
        QCOMPARE(claveEstable(falso.error().categoria), claveEstable(CD::VolumenNoDisponible));
        // Enlace final hacia un volumen que no esta.
        QVERIFY(QFile::link(QStringLiteral("/Volumes/XAXX-NO-MONTADO-7Q2W/Respaldos"), tmp->filePath(QStringLiteral("enlace-vol"))));
        auto enlace = DestinoRespaldoPosix(tmp->filePath(QStringLiteral("enlace-vol"))).abrir();
        QCOMPARE(claveEstable(enlace.error().categoria), claveEstable(CD::VolumenNoDisponible));
        // /Volumes/Macintosh HD (enlace del sistema al disco de arranque): valido.
        const QString arranque = QStringLiteral("/Volumes/Macintosh HD");
        if (QFileInfo(arranque).isSymLink()) {
            auto r = DestinoRespaldoPosix(arranque + QFileInfo(dir).canonicalFilePath()).abrir();
            QVERIFY2(r, r ? "" : qPrintable(r.error().diagnostico));
        }
    }

    void condicionesDelEnlaceDeLaCarpeta()
    {
        const QString real = carpeta(QStringLiteral("real"));
        QVERIFY(QDir().mkpath(QDir(real).filePath(QStringLiteral("respaldos"))));
        // Ultimo componente enlace a una carpeta propia: valido.
        QVERIFY(QFile::link(real, tmp->filePath(QStringLiteral("enlace-final"))));
        QVERIFY(DestinoRespaldoPosix(tmp->filePath(QStringLiteral("enlace-final"))).abrir());
        // Enlace del usuario como componente intermedio: rechazado.
        auto intermedio = DestinoRespaldoPosix(tmp->filePath(QStringLiteral("enlace-final/respaldos"))).abrir();
        QVERIFY(!intermedio);
        QCOMPARE(claveEstable(intermedio.error().categoria), claveEstable(CD::EnlaceIntermedio));
        // Carpeta de otro dueno (root): rechazada sin escribir nada.
        auto ajena = DestinoRespaldoPosix(QStringLiteral("/usr")).abrir();
        QVERIFY(!ajena);
        QCOMPARE(claveEstable(ajena.error().categoria), claveEstable(CD::DuenoDistinto));
    }

    void pruebaDeEscrituraSeDesvinculaAntesDeEscribir()
    {
        const QString dir = carpeta(QStringLiteral("nube"));
        QStringList visibles = {QStringLiteral("<no llamado>")};
        DestinoRespaldoPosix::Opciones o;
        o.trasDesvincularPrueba = [&] {
            // Entre crear y escribir: el archivo de prueba ya no esta en la
            // carpeta (una terminacion aqui no deja nada).
            visibles = QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        };
        DestinoRespaldoPosix d(dir, o);
        QVERIFY(d.validar(1));
        QVERIFY2(visibles.isEmpty(), qPrintable(visibles.join(QLatin1Char(','))));
    }

    // --- Adaptador POSIX ---------------------------------------------------------
    void validacionDelDestinoPosix()
    {
        const QString dir = carpeta(QStringLiteral("nube"));
        QVERIFY(DestinoRespaldoPosix(dir).validar(1024));
        QVERIFY(QDir(dir).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)); // la prueba se borra

        auto inexistente = DestinoRespaldoPosix(tmp->filePath(QStringLiteral("no-existe"))).validar(1);
        QCOMPARE(claveEstable(inexistente.error().categoria), claveEstable(CD::CarpetaInexistente));

        QVERIFY(escribir(tmp->filePath(QStringLiteral("archivo")), "x"));
        auto archivo = DestinoRespaldoPosix(tmp->filePath(QStringLiteral("archivo"))).validar(1);
        QCOMPARE(claveEstable(archivo.error().categoria), claveEstable(CD::NoEsCarpeta));

        const QString soloLectura = carpeta(QStringLiteral("solo-lectura"));
        QVERIFY(QFile::setPermissions(soloLectura, QFile::ReadOwner | QFile::ExeOwner));
        auto ro = DestinoRespaldoPosix(soloLectura).validar(1);
        QFile::setPermissions(soloLectura, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QCOMPARE(claveEstable(ro.error().categoria), claveEstable(CD::SinEscritura));

        auto lleno = DestinoRespaldoPosix(dir).validar(std::numeric_limits<quint64>::max() / 2);
        QCOMPARE(claveEstable(lleno.error().categoria), claveEstable(CD::EspacioInsuficiente));

        auto volumen = DestinoRespaldoPosix(QStringLiteral("/Volumes/XAXX-NO-MONTADO-7Q2W/Respaldos")).validar(1);
        QCOMPARE(claveEstable(volumen.error().categoria), claveEstable(CD::VolumenNoDisponible));
    }

    void destinoPosixNoSigueEnlaces()
    {
        const QString dir = carpeta(QStringLiteral("nube"));
        const QString fuera = carpeta(QStringLiteral("fuera"));
        QVERIFY(escribir(QDir(fuera).filePath(QStringLiteral("objetivo")), "NO-TOCAR"));
        QVERIFY(QFile::link(QDir(fuera).filePath(QStringLiteral("objetivo")), QDir(dir).filePath(QStringLiteral("enlace.afbak"))));
        QVERIFY(escribir(QDir(dir).filePath(QStringLiteral("real.afbak")), "x"));
        DestinoRespaldoPosix d(dir);
        auto lista = d.listar();
        QVERIFY(lista);
        QCOMPARE(lista.valor().size(), qsizetype(1));
        QCOMPARE(lista.valor().constFirst().nombre, QStringLiteral("real.afbak"));
        QVERIFY(!lista.valor().constFirst().sinDescargar);
        QCOMPARE(claveEstable(d.abrirParaLeer(QStringLiteral("enlace.afbak")).error().categoria), claveEstable(CD::NoRegular));
        QCOMPARE(claveEstable(d.borrar(QStringLiteral("enlace.afbak")).error().categoria), claveEstable(CD::NoRegular));
        QVERIFY(!d.borrar(QStringLiteral("../fuera/objetivo")));
        QCOMPARE(leer(QDir(fuera).filePath(QStringLiteral("objetivo"))), QByteArrayLiteral("NO-TOCAR"));
        QVERIFY(d.borrar(QStringLiteral("real.afbak")));
    }
};

int main(int argc, char* argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    TestRespaldoService prueba;
    return QTest::qExec(&prueba, argc, argv);
}

#include "TestRespaldoService.moc"
