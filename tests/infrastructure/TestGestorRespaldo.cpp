// T025.4: GestorRespaldo y ProgramadorRespaldoDiario con preferencias.ini real
// (QTemporaryDir), RespaldoService real con captura sintetica, destino POSIX
// real y fakes de plataforma (KEK, bookmark, OS, reloj y programador). Solo
// datos sinteticos (XAXX010101000).

#include "app_core/ConfiguracionRespaldoIni.h"
#include "app_core/FuenteRespaldoLocal.h"
#include "application/respaldo/GestorRespaldo.h"
#include "application/respaldo/ProgramadorRespaldoDiario.h"
#include "infrastructure/crypto/ContenedorRespaldoOpenSsl.h"
#include "infrastructure/storage/DestinoRespaldoPosix.h"

#include "fakes/FakeAlmacenKekRespaldo.h"
#include "fakes/FakeCarpetaRespaldoSistema.h"
#include "fakes/FakeOSIntegration.h"
#include "fakes/FakeProgramador.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimeZone>

#include <sys/stat.h>

#include <atomic>
#include <memory>

using namespace satcfdi;
using namespace satcfdi::respaldo;
using Estado = ResultadoRespaldo::Estado;
using CG = ErrorGestorRespaldo::Categoria;
using Pendiente = EstadoRespaldoGestor::PendienteDiario;

namespace {

const QUuid kInstalacion = QUuid(QStringLiteral("{6f1c2a7e-3b9d-4c5e-8f10-2a3b4c5d6e7f}"));
const QString kUuidVolumen = QStringLiteral("00000000-0000-4000-8000-000000000001");

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

std::shared_ptr<BufferSecreto> secreto(const QString& s)
{
    return std::make_shared<BufferSecreto>(BufferSecreto::desdeTexto(s));
}

QString texto(const BufferSecreto& b)
{
    return QString::fromLatin1(reinterpret_cast<const char*>(b.datos()), static_cast<qsizetype>(b.tamano()));
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

template <typename T>
T esperar(QFuture<T> f)
{
    (void)QTest::qWaitFor([&] { return f.isFinished(); }, 60000);
    return f.result();
}

// Captura sintetica: una "base" en un staging propio. `pendiente` deja la
// captura sin terminar hasta liberar().
class FuenteSintetica final : public FuenteRespaldo {
public:
    explicit FuenteSintetica(QString raiz) : m_raiz(std::move(raiz)) {}
    bool pendiente = false;

    QFuture<Resultado<CapturaRespaldo, ErrorCaptura>> capturar() override
    {
        CapturaRespaldo c;
        c.staging = QDir(m_raiz).filePath(QStringLiteral("captura-%1").arg(++m_n));
        const QString base = QDir(c.staging).filePath(QStringLiteral("satcfdi.sqlite3"));
        escribir(base, QByteArray(8192, 'x') + "XAXX010101000");
        c.metadatos.versionEsquema = 7;
        c.metadatos.versionApp = QStringLiteral("0.1.0");
        c.metadatos.capturadaEnUtc = QDateTime::currentDateTimeUtc();
        c.entradas.append(EntradaCaptura{EntradaCaptura::Tipo::Base, kRutaRespaldoBase, c.staging,
                                         QStringLiteral("satcfdi.sqlite3"), huella(base)});
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
        QDir(c.staging).removeRecursively();
        return Resultado<Exito, ErrorCaptura>::exito(Exito{});
    }
    int limpiarHuerfanos() override { return 0; }
    std::function<void(const CapturaRespaldo&)> limpiadorAutonomo() const override
    {
        return [](const CapturaRespaldo& c) { QDir(c.staging).removeRecursively(); };
    }

private:
    QString m_raiz;
    int m_n = 0;
    std::shared_ptr<QPromise<Resultado<CapturaRespaldo, ErrorCaptura>>> m_promesa;
    std::optional<CapturaRespaldo> m_pendiente;
};

// Registra en que hilo se cargan las KEK (deben cargarse fuera del grafico).
class AlmacenObservado final : public AlmacenKekRespaldo {
public:
    explicit AlmacenObservado(AlmacenKekRespaldo& real) : m_real(real) {}
    Resultado<ReferenciaKek, ErrorAlmacenKek> guardar(const KekRespaldo& k) override
    {
        hilos.append(QThread::currentThread());
        return m_real.guardar(k);
    }
    Resultado<KekRespaldo, ErrorAlmacenKek> cargar(const ReferenciaKek& r) override
    {
        hilos.append(QThread::currentThread());
        return m_real.cargar(r);
    }
    Resultado<Exito, ErrorAlmacenKek> borrar(const ReferenciaKek& r) override { return m_real.borrar(r); }
    Resultado<QList<ReferenciaKek>, ErrorAlmacenKek> listar() override { return m_real.listar(); }
    QList<QThread*> hilos; // solo se lee al terminar

private:
    AlmacenKekRespaldo& m_real;
};

// Guarda de verdad en el ini pero reporta error (o no guarda), para el
// rollback que relee el ini.
class ConfiguracionQueReportaError final : public AlmacenConfiguracionRespaldo {
public:
    ConfiguracionQueReportaError(AlmacenConfiguracionRespaldo& real, bool escribe) : m_real(real), m_escribe(escribe) {}
    Resultado<ConfiguracionLeida, ErrorConfiguracionRespaldo> cargar() override { return m_real.cargar(); }
    Resultado<Exito, ErrorConfiguracionRespaldo> guardar(const ConfiguracionRespaldo& c) override
    {
        if (m_escribe) {
            (void)m_real.guardar(c);
        }
        return Resultado<Exito, ErrorConfiguracionRespaldo>::fallo(ErrorConfiguracionRespaldo::de(QStringLiteral("prueba")));
    }

private:
    AlmacenConfiguracionRespaldo& m_real;
    bool m_escribe;
};

class ConfiguracionQueFalla final : public AlmacenConfiguracionRespaldo {
public:
    Resultado<ConfiguracionLeida, ErrorConfiguracionRespaldo> cargar() override
    {
        return Resultado<ConfiguracionLeida, ErrorConfiguracionRespaldo>::fallo(
            ErrorConfiguracionRespaldo::de(QStringLiteral("prueba")));
    }
    Resultado<Exito, ErrorConfiguracionRespaldo> guardar(const ConfiguracionRespaldo&) override
    {
        return Resultado<Exito, ErrorConfiguracionRespaldo>::fallo(ErrorConfiguracionRespaldo::de(QStringLiteral("prueba")));
    }
};

// Todo el grafo de T025.4 sobre un directorio temporal.
struct Entorno {
    QTemporaryDir dir;
    QString datos;
    QString nube;
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 22), QTime(8, 0), QTimeZone::UTC)};
    fakes::FakeProgramador programador{reloj};
    fakes::FakeAlmacenKekRespaldo almacenReal;
    AlmacenObservado almacen{almacenReal};
    fakes::FakeCarpetaRespaldoSistema carpetas;
    fakes::FakeOSIntegration os;
    std::shared_ptr<crypto::ContenedorRespaldoOpenSsl> contenedor = std::make_shared<crypto::ContenedorRespaldoOpenSsl>();
    std::unique_ptr<FuenteSintetica> fuente;
    std::unique_ptr<RespaldoService> servicio;
    std::unique_ptr<ConfiguracionRespaldoIni> configuracionIni;
    AlmacenConfiguracionRespaldo* configuracion = nullptr;
    std::unique_ptr<GestorRespaldo> gestor;
    std::unique_ptr<ProgramadorRespaldoDiario> diario;

    bool preparar(AlmacenConfiguracionRespaldo* otraConfiguracion = nullptr)
    {
        if (!dir.isValid()) {
            return false;
        }
        datos = dir.filePath(QStringLiteral("datos"));
        nube = dir.filePath(QStringLiteral("nube"));
        QDir().mkpath(datos);
        QDir().mkpath(nube);
        fuente = std::make_unique<FuenteSintetica>(dir.filePath(QStringLiteral("src")));
        RespaldoService::Opciones so;
        so.idInstalacion = kInstalacion;
        so.reloj = reloj.funcion();
        so.registro = std::make_shared<RegistroPublicadosJson>(QDir(datos).filePath(QStringLiteral("respaldo/publicados.json")));
        servicio = std::make_unique<RespaldoService>(*fuente, contenedor, std::move(so));
        configuracionIni = std::make_unique<ConfiguracionRespaldoIni>(rutaIni());
        configuracion = otraConfiguracion ? otraConfiguracion : configuracionIni.get();
        GestorRespaldo::Dependencias d;
        d.configuracion = configuracion;
        d.almacenKek = &almacen;
        d.carpetas = &carpetas;
        d.contenedor = contenedor;
        d.servicio = servicio.get();
        const QString parciales = QDir(datos).filePath(QStringLiteral("respaldo/parciales"));
        d.fabricaDestino = [parciales](const QString& ruta, const QString& uuid) {
            DestinoRespaldoPosix::Opciones o;
            o.carpetaParciales = parciales;
            o.uuidVolumen = uuid;
            return std::shared_ptr<DestinoRespaldo>(std::make_shared<DestinoRespaldoPosix>(ruta, std::move(o)));
        };
        d.reloj = reloj.funcion();
        gestor = std::make_unique<GestorRespaldo>(std::move(d));
        gestor->fijarOS(&os);
        os.inicializar();
        diario = std::make_unique<ProgramadorRespaldoDiario>(*gestor, programador, reloj.funcion(),
                                                             ProgramadorRespaldoDiario::Opciones{});
        QObject::connect(&os, &OSIntegration::sistemaDesperto, diario.get(), &ProgramadorRespaldoDiario::evaluar);
        return true;
    }

    ~Entorno()
    {
        diario.reset();
        gestor.reset();
        servicio.reset();
    }

    QString rutaIni() const { return QDir(datos).filePath(QStringLiteral("preferencias.ini")); }

    QStringList publicados() const
    {
        return QDir(nube).entryList(QStringList{QStringLiteral("*.afbak")}, QDir::Files);
    }

    // Carpeta + contrasena; devuelve la clave de recuperacion formateada.
    QString configurarTodo(const QString& contrasena = QStringLiteral("contrasena-sintetica-1"))
    {
        if (!esperar(gestor->configurarCarpeta(nube))) {
            return {};
        }
        auto r = esperar(gestor->configurarContrasena(secreto(contrasena)));
        return r ? texto(*r.valor()) : QString();
    }

    // Espera a que el gestor vuelva a estar libre y su espejo al dia.
    bool esperarLibre()
    {
        return QTest::qWaitFor([&] { return !gestor->enCurso(); }, 60000);
    }
};

} // namespace

class TestGestorRespaldo : public QObject {
    Q_OBJECT

private slots:
    // --- Configuracion (D14) -----------------------------------------------------
    void configuracionEnLaSeccionRespaldoYFueraDeLaCaptura()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString ini = tmp.filePath(QStringLiteral("preferencias.ini"));
        {
            QSettings s(ini, QSettings::IniFormat);
            s.setValue(QStringLiteral("facturas/ultimoPerfil"), QStringLiteral("0b6f4c4e-6a8e-4d55-9a3f-2f1f6a1b9c01"));
            s.setValue(QStringLiteral("respaldo/idInstalacion"), kInstalacion.toString(QUuid::WithoutBraces));
        }
        ConfiguracionRespaldoIni almacen(ini);
        ConfiguracionRespaldo c;
        c.rutaCanonica = QStringLiteral("/Users/usuario/Library/CloudStorage/Dropbox/Respaldos");
        c.bookmark = QByteArray("\x00\x01bookmark-sintetico", 20);
        c.uuidVolumen = kUuidVolumen;
        c.diarioActivado = true;
        c.kekContrasena = ReferenciaKek::generar();
        c.kekRecuperacion = ReferenciaKek::generar();
        c.ultimoIntentoUtc = QDateTime(QDate(2026, 10, 22), QTime(9, 15, 30, 250), QTimeZone::UTC);
        c.ultimoExitoUtc = c.ultimoIntentoUtc;
        c.ultimoResultado = QStringLiteral("exito");
        QVERIFY(almacen.guardar(c));
        auto leida = almacen.cargar();
        QVERIFY(leida);
        QVERIFY(leida.valor().integra);
        QCOMPARE(leida.valor().configuracion, c);
        // Convive con lo demas y todo queda bajo [respaldo].
        QSettings s(ini, QSettings::IniFormat);
        QCOMPARE(s.value(QStringLiteral("respaldo/idInstalacion")).toString(), kInstalacion.toString(QUuid::WithoutBraces));
        for (const QString& k : s.allKeys()) {
            QVERIFY2(k.startsWith(QLatin1String("respaldo/")) || k == QLatin1String("facturas/ultimoPerfil"), qPrintable(k));
        }
        // T025.2: la captura no copia la seccion respaldo.
        const QString filtrado = tmp.filePath(QStringLiteral("filtrado.ini"));
        QCOMPARE(FuenteRespaldoLocal::filtrarPreferencias(ini, filtrado), 1);
        const QByteArray salida = leer(filtrado);
        QVERIFY(!salida.contains("respaldo"));
        QVERIFY(!salida.contains("bookmark"));
        QVERIFY(!salida.contains(c.kekContrasena.texto().toLatin1()));
    }

    void configurarContrasenaGuardaDosKekYDevuelveLaClaveUnaVez()
    {
        Entorno e;
        QVERIFY(e.preparar());
        QVERIFY(esperar(e.gestor->iniciar()) == -1); // sin carpeta: no limpia parciales
        const QString clave = e.configurarTodo();
        QVERIFY(QRegularExpression(QStringLiteral("^([0-9A-HJKMNP-TV-Z]{4}-){13}[0-9A-HJKMNP-TV-Z]{4}$")).match(clave).hasMatch());
        QCOMPARE(e.almacenReal.cantidad(), 2);
        QVERIFY(QTest::qWaitFor([&] { return e.gestor->estado().contrasenaConfigurada; }, 5000));
        // Una segunda configuracion no reemplaza nada.
        auto otra = esperar(e.gestor->configurarContrasena(secreto(QStringLiteral("otra"))));
        QVERIFY(!otra);
        QCOMPARE(claveEstable(otra.error().categoria), claveEstable(CG::YaConfigurada));
        QCOMPARE(e.almacenReal.cantidad(), 2);
        // Ni la contrasena ni la clave formateada quedan en preferencias.ini.
        const QByteArray ini = leer(e.rutaIni());
        QVERIFY(!ini.contains("contrasena-sintetica-1"));
        QVERIFY(!ini.contains(clave.left(9).toLatin1()));
        QVERIFY(!ini.contains(clave.mid(10, 9).toLatin1()));
        // Las KEK se derivaron y guardaron fuera del hilo grafico.
        QVERIFY(!e.almacen.hilos.isEmpty());
        for (QThread* h : e.almacen.hilos) {
            QVERIFY(h != QThread::currentThread());
        }
    }

    void cambioDeContrasenaD16YKekHuerfanas()
    {
        Entorno e;
        QVERIFY(e.preparar());
        QVERIFY(esperar(e.gestor->iniciar()) == -1);
        const QString clave = e.configurarTodo(QStringLiteral("contrasena-sintetica-1"));
        QVERIFY(!clave.isEmpty());
        const auto antes = esperar(e.gestor->respaldarAhora());
        QVERIFY2(antes.exito(), qPrintable(antes.diagnostico));
        const QString copiaAnterior = antes.nombrePublicado;
        const ConfiguracionRespaldo cfg1 = e.configuracionIni->cargar().valor().configuracion;

        // Verificacion incorrecta: nada cambia.
        VerificacionCambio mala{VerificacionCambio::Tipo::ContrasenaAnterior, secreto(QStringLiteral("no-es"))};
        auto r0 = esperar(e.gestor->cambiarContrasena(secreto(QStringLiteral("contrasena-sintetica-2")), mala));
        QVERIFY(!r0);
        QCOMPARE(claveEstable(r0.error().categoria), claveEstable(CG::VerificacionIncorrecta));
        QCOMPARE(e.configuracionIni->cargar().valor().configuracion.kekContrasena, cfg1.kekContrasena);

        // Con la contrasena anterior.
        VerificacionCambio buena{VerificacionCambio::Tipo::ContrasenaAnterior, secreto(QStringLiteral("contrasena-sintetica-1"))};
        QVERIFY(esperar(e.gestor->cambiarContrasena(secreto(QStringLiteral("contrasena-sintetica-2")), buena)));
        const ConfiguracionRespaldo cfg2 = e.configuracionIni->cargar().valor().configuracion;
        QVERIFY(cfg2.kekContrasena != cfg1.kekContrasena);
        QCOMPARE(cfg2.kekRecuperacion, cfg1.kekRecuperacion); // D16: misma recuperacion
        QVERIFY(!e.almacenReal.contiene(cfg1.kekContrasena)); // la vieja se borro
        QCOMPARE(e.almacenReal.cantidad(), 2);

        // Con la clave de recuperacion (tecleada en minusculas).
        VerificacionCambio porClave{VerificacionCambio::Tipo::ClaveRecuperacion, secreto(clave.toLower())};
        QVERIFY(esperar(e.gestor->cambiarContrasena(secreto(QStringLiteral("contrasena-sintetica-3")), porClave)));
        const ConfiguracionRespaldo cfg3 = e.configuracionIni->cargar().valor().configuracion;
        QCOMPARE(cfg3.kekRecuperacion, cfg1.kekRecuperacion);

        // La copia anterior: no abre con la contrasena nueva, si con la recuperacion.
        QFile f(QDir(e.nube).filePath(copiaAnterior));
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto kekNueva = e.almacenReal.cargar(cfg3.kekContrasena);
        auto kekRec = e.almacenReal.cargar(cfg3.kekRecuperacion);
        QVERIFY(kekNueva && kekRec);
        QVERIFY(!e.contenedor->verificar(f, kekNueva.valor()));
        QVERIFY(e.contenedor->verificar(f, kekRec.valor()));
        // Y un respaldo con la contrasena nueva sigue funcionando.
        e.reloj.fijar(e.reloj.ahora().addSecs(60));
        QVERIFY(esperar(e.gestor->respaldarAhora()).exito());

        // KEK huerfanas (p. ej. caida a mitad de un cambio): iniciar() las borra
        // y conserva las referenciadas.
        auto huerfana = e.almacenReal.guardar(kekNueva.valor());
        QVERIFY(huerfana);
        QCOMPARE(e.almacenReal.cantidad(), 3);
        // KEK de OTRA instalacion (otro --data-dir) en el mismo llavero: nunca
        // se toca.
        fakes::FakeAlmacenKekRespaldo otraInstalacion(QStringLiteral("11111111-2222-4333-8444-555555555555"),
                                                      e.almacenReal.llavero());
        QVERIFY(otraInstalacion.guardar(kekNueva.valor()));
        QVERIFY(otraInstalacion.guardar(kekNueva.valor()));
        auto otra = std::make_unique<GestorRespaldo>(GestorRespaldo::Dependencias{
            e.configuracionIni.get(), &e.almacen, &e.carpetas, e.contenedor, e.servicio.get(), {}, e.reloj.funcion()});
        esperar(otra->iniciar());
        QCOMPARE(e.almacenReal.cantidad(), 2);
        QVERIFY(!e.almacenReal.contiene(huerfana.valor()));
        QCOMPARE(otraInstalacion.cantidad(), 2);
        QVERIFY(e.almacenReal.contiene(cfg3.kekContrasena) && e.almacenReal.contiene(cfg3.kekRecuperacion));
    }

    void configuracionIlegibleNuncaBorraKek()
    {
        ConfiguracionQueFalla rota;
        Entorno e;
        QVERIFY(e.preparar(&rota));
        auto r = e.almacenReal.guardar(KekRespaldo::desdeBytes(KekRespaldo::Tipo::Recuperacion, {}, BufferSecreto(32)));
        QVERIFY(r);
        esperar(e.gestor->iniciar());
        QCOMPARE(e.almacenReal.cantidad(), 1);
    }

    // --- Seguridad 1: reconciliacion solo con configuracion integra ----------------
    void reconciliacionConservadoraNuncaBorraSinConfiguracionIntegra_data()
    {
        QTest::addColumn<int>("caso");
        QTest::newRow("ini inexistente") << 0;
        QTest::newRow("referencia mal escrita") << 1;
        QTest::newRow("una sola referencia") << 2;
    }

    void reconciliacionConservadoraNuncaBorraSinConfiguracionIntegra()
    {
        QFETCH(int, caso);
        Entorno e;
        QVERIFY(e.preparar());
        // Dos KEK de produccion y una tercera en el almacen.
        const KekRespaldo kek = KekRespaldo::desdeBytes(KekRespaldo::Tipo::Recuperacion, {}, BufferSecreto(32));
        auto a = e.almacenReal.guardar(kek);
        auto b = e.almacenReal.guardar(kek);
        auto c = e.almacenReal.guardar(kek);
        QVERIFY(a && b && c);
        if (caso > 0) {
            QSettings s(e.rutaIni(), QSettings::IniFormat);
            s.setValue(QStringLiteral("respaldo/kek/contrasena"),
                       caso == 1 ? QStringLiteral("no-es-una-referencia") : a.valor().texto());
            if (caso == 1) {
                s.setValue(QStringLiteral("respaldo/kek/recuperacion"), b.valor().texto());
            }
            s.sync();
        }
        auto leida = e.configuracionIni->cargar();
        QVERIFY(leida);
        QVERIFY(!leida.valor().integra);
        if (caso == 1) {
            // La mal escrita no se vuelve "ausente": la lectura es no integra.
            QVERIFY(leida.valor().configuracion.kekContrasena.nula());
        }
        esperar(e.gestor->iniciar());
        QCOMPARE(e.almacenReal.cantidad(), 3); // cero borrados
        // Tampoco un cambio de contrasena reconcilia sin configuracion integra.
        QVERIFY(!esperar(e.gestor->cambiarContrasena(secreto(QStringLiteral("x")),
                                                     VerificacionCambio{VerificacionCambio::Tipo::ContrasenaAnterior,
                                                                        secreto(QStringLiteral("y"))})));
        QCOMPARE(e.almacenReal.cantidad(), 3);
    }

    void rollbackReleeElIniAntesDeBorrar_data()
    {
        QTest::addColumn<bool>("escribe");
        QTest::newRow("escribio pero reporto error") << true;
        QTest::newRow("no escribio") << false;
    }

    void rollbackReleeElIniAntesDeBorrar()
    {
        QFETCH(bool, escribe);
        Entorno e;
        QVERIFY(e.dir.isValid());
        // El decorador necesita el ini real del entorno: se crea tras preparar.
        std::unique_ptr<ConfiguracionQueReportaError> falla;
        QVERIFY(e.preparar());
        falla = std::make_unique<ConfiguracionQueReportaError>(*e.configuracionIni, escribe);
        auto gestor = std::make_unique<GestorRespaldo>(GestorRespaldo::Dependencias{
            falla.get(), &e.almacen, &e.carpetas, e.contenedor, e.servicio.get(), {}, e.reloj.funcion()});
        auto r = esperar(gestor->configurarContrasena(secreto(QStringLiteral("contrasena-sintetica-1"))));
        const ConfiguracionRespaldo ini = e.configuracionIni->cargar().valor().configuracion;
        if (escribe) {
            // El ini si quedo con las referencias: se conservan y no es error.
            QVERIFY(r);
            QCOMPARE(e.almacenReal.cantidad(), 2);
            QVERIFY(e.almacenReal.contiene(ini.kekContrasena) && e.almacenReal.contiene(ini.kekRecuperacion));
        } else {
            QVERIFY(!r);
            QCOMPARE(claveEstable(r.error().categoria), claveEstable(CG::Configuracion));
            QCOMPARE(e.almacenReal.cantidad(), 0);
            QVERIFY(ini.kekContrasena.nula());
        }
    }

    // --- Seguridad 2: cambio de contrasena con un respaldo en curso ---------------
    void cambiarContrasenaConRespaldoEnCursoDaEnCurso()
    {
        Entorno e;
        QVERIFY(e.preparar());
        esperar(e.gestor->iniciar());
        QVERIFY(!e.configurarTodo(QStringLiteral("contrasena-sintetica-1")).isEmpty());
        const ConfiguracionRespaldo antes = e.configuracionIni->cargar().valor().configuracion;
        e.fuente->pendiente = true;
        auto f = e.gestor->respaldarAhora();
        auto c = esperar(e.gestor->cambiarContrasena(
            secreto(QStringLiteral("contrasena-sintetica-2")),
            VerificacionCambio{VerificacionCambio::Tipo::ContrasenaAnterior, secreto(QStringLiteral("contrasena-sintetica-1"))}));
        QVERIFY(!c);
        QCOMPARE(claveEstable(c.error().categoria), claveEstable(CG::EnCurso));
        QVERIFY(QTest::qWaitFor([&] { return e.servicio->enCurso(); }, 5000));
        e.fuente->liberar();
        QVERIFY(esperar(f).exito());
        QCOMPARE(e.configuracionIni->cargar().valor().configuracion.kekContrasena, antes.kekContrasena);
    }

    // --- Calidad: barrera de arranque del diario ------------------------------------
    void diarioPersistidoYVencidoEsperaALaLimpiezaDeParciales()
    {
        Entorno e;
        QVERIFY(e.preparar());
        // Configuracion ya persistida (sesion anterior): carpeta, KEK y diario
        // activo, sin exito previo -> vencido. Sin iniciar(): nunca elegible.
        QVERIFY(!e.configurarTodo().isEmpty());
        QVERIFY(esperar(e.gestor->activarDiario(true)));
        QTest::qWait(50);
        QCOMPARE(e.diario->ejecucionesLanzadas(), 0);
        e.diario->evaluar();
        QCOMPARE(e.diario->ejecucionesLanzadas(), 0);
        // Un .partial propio de una terminacion forzada anterior.
        QString huerfano;
        {
            DestinoRespaldoPosix d(e.nube);
            auto p = d.crearParcial(RespaldoService::propietarioDe(kInstalacion));
            QVERIFY(p);
            huerfano = QDir(e.nube).filePath(p.valor()->nombre());
        }
        QVERIFY(QFileInfo::exists(huerfano));
        bool parcialAlLanzar = true;
        QObject::connect(e.diario.get(), &ProgramadorRespaldoDiario::respaldoDiarioLanzado, e.diario.get(),
                         [&] { parcialAlLanzar = QFileInfo::exists(huerfano); });
        // Arranque: la limpieza termina y solo despues el diario lanza.
        auto arranque = e.gestor->iniciar();
        QCOMPARE(esperar(arranque), 1);
        QVERIFY(QTest::qWaitFor([&] { return e.diario->ejecucionesLanzadas() == 1; }, 5000));
        QVERIFY(!parcialAlLanzar);
        QVERIFY(e.esperarLibre());
        QVERIFY(QTest::qWaitFor([&] { return e.publicados().size() == 1; }, 10000));
    }

    // --- Carpeta: bookmark obsoleto, UUID ------------------------------------------
    void bookmarkObsoletoSeRenuevaYSePersiste()
    {
        Entorno e;
        QVERIFY(e.preparar());
        esperar(e.gestor->iniciar());
        QVERIFY(!e.configurarTodo().isEmpty());
        const ConfiguracionRespaldo antes = e.configuracionIni->cargar().valor().configuracion;
        // El usuario renombro la carpeta: el bookmark resuelve a la nueva ruta y
        // el sistema pide regenerarlo.
        const QString movida = e.dir.filePath(QStringLiteral("nube-renombrada"));
        QVERIFY(QDir().rename(e.nube, movida));
        e.carpetas.fijarResolucion(antes.bookmark, CarpetaRespaldoResuelta{movida, kUuidVolumen, true});
        const auto r = esperar(e.gestor->respaldarAhora());
        QVERIFY2(r.exito(), qPrintable(r.diagnostico));
        const ConfiguracionRespaldo despues = e.configuracionIni->cargar().valor().configuracion;
        QCOMPARE(despues.rutaCanonica, movida);
        QVERIFY(despues.bookmark != antes.bookmark);
        QCOMPARE(e.carpetas.llamadasCrear(), 2);
        QCOMPARE(QDir(movida).entryList(QStringList{QStringLiteral("*.afbak")}, QDir::Files).size(), qsizetype(1));

        // Bookmark que ya no resuelve: error especifico, sin escribir nada.
        e.carpetas.fallarResolver(despues.bookmark, ErrorCarpetaRespaldo::Categoria::NoResuelve);
        const auto r2 = esperar(e.gestor->respaldarAhora());
        QCOMPARE(claveEstable(r2.estado), claveEstable(Estado::ErrorDestino));
        QCOMPARE(r2.diagnostico, QStringLiteral("carpeta.no_resuelve"));
        QCOMPARE(e.configuracionIni->cargar().valor().configuracion.ultimoResultado, claveEstable(Estado::ErrorDestino));
    }

    void registroConUuidDeVolumenYSinUuid()
    {
        Entorno e;
        QVERIFY(e.preparar());
        esperar(e.gestor->iniciar());
        e.carpetas.fijarUuidVolumen(QString()); // volumen sin UUID (algunos de red)
        QVERIFY(!e.configurarTodo().isEmpty());
        QVERIFY(esperar(e.gestor->respaldarAhora()).exito());
        const QString registro = QDir(e.datos).filePath(QStringLiteral("respaldo/publicados.json"));
        const auto carpetas = [&] {
            QStringList r;
            for (const auto& v : QJsonDocument::fromJson(leer(registro)).object().value(QStringLiteral("publicados")).toArray()) {
                r.append(v.toObject().value(QStringLiteral("carpeta")).toString());
            }
            return r;
        };
        QVERIFY(QRegularExpression(QStringLiteral("^\\d+:\\d+$")).match(carpetas().value(0)).hasMatch());

        // Ahora con UUID: la entrada antigua de ESTA carpeta se migra a la
        // identidad estable (UUID, inodo) y la nueva ya nace asi.
        e.carpetas.fijarUuidVolumen(kUuidVolumen);
        QVERIFY(esperar(e.gestor->configurarCarpeta(e.nube)));
        e.reloj.fijar(e.reloj.ahora().addSecs(60));
        QVERIFY(esperar(e.gestor->respaldarAhora()).exito());
        const QStringList ids = carpetas();
        QCOMPARE(ids.size(), qsizetype(2));
        for (const QString& id : ids) {
            QVERIFY2(id.startsWith(QStringLiteral("vol:") + kUuidVolumen + QLatin1Char(':')), qPrintable(id));
        }
    }

    // --- Arranque: .partial propios ------------------------------------------------
    void arranqueLimpiaParcialesPropiosDeLaCarpetaConfigurada()
    {
        Entorno e;
        QVERIFY(e.preparar());
        esperar(e.gestor->iniciar());
        QVERIFY(!e.configurarTodo().isEmpty());
        {
            DestinoRespaldoPosix d(e.nube);
            auto p = d.crearParcial(RespaldoService::propietarioDe(kInstalacion));
            QVERIFY(p);
            p.valor()->dispositivo().write("a medias"); // terminacion forzada
        }
        QVERIFY(escribir(QDir(e.nube).filePath(QStringLiteral(".otra-app.partial")), "x"));
        auto otra = std::make_unique<GestorRespaldo>(GestorRespaldo::Dependencias{
            e.configuracionIni.get(), &e.almacen, &e.carpetas, e.contenedor, e.servicio.get(),
            [](const QString& ruta, const QString&) {
                return std::shared_ptr<DestinoRespaldo>(std::make_shared<DestinoRespaldoPosix>(ruta));
            },
            e.reloj.funcion()});
        QCOMPARE(esperar(otra->iniciar()), 1);
        QCOMPARE(QDir(e.nube).entryList(QDir::Files | QDir::Hidden), QStringList{QStringLiteral(".otra-app.partial")});
    }

    // --- Diario (D4, D18) ---------------------------------------------------------------
    void diarioVencidoUnaSolaEjecucionAunqueHayaRafagas()
    {
        Entorno e;
        QVERIFY(e.preparar());
        esperar(e.gestor->iniciar());
        QVERIFY(!e.configurarTodo().isEmpty());
        e.fuente->pendiente = true; // el respaldo queda en curso mientras llegan despertares
        // Activar el diario re-evalua: nunca hubo exito -> vencido -> una ejecucion.
        QVERIFY(esperar(e.gestor->activarDiario(true)));
        QVERIFY(QTest::qWaitFor([&] { return e.diario->ejecucionesLanzadas() == 1; }, 5000));
        e.diario->evaluar(); // evaluacion de arranque mientras corre: no duplica
        QCOMPARE(e.diario->ejecucionesLanzadas(), 1);
        QVERIFY(e.gestor->enCurso());
        QVERIFY(QTest::qWaitFor([&] { return e.os.actividadesActivas() == 1; }, 5000));
        for (int i = 0; i < 5; ++i) {
            QVERIFY(e.os.emitirDespertar());
        }
        QTest::qWait(50);
        QCOMPARE(e.diario->ejecucionesLanzadas(), 1);
        // Un manual durante el diario tampoco inicia otro (D18).
        QCOMPARE(claveEstable(esperar(e.gestor->respaldarAhora()).estado), claveEstable(Estado::YaEnCurso));

        QVERIFY(QTest::qWaitFor([&] { return e.servicio->enCurso(); }, 5000));
        e.fuente->liberar();
        QVERIFY(e.esperarLibre());
        QVERIFY(QTest::qWaitFor([&] { return e.gestor->estado().ultimoExitoUtc.has_value(); }, 5000));
        QCOMPARE(e.os.actividadesActivas(), 0);
        QCOMPARE(e.os.motivosActividad().value(0), OSIntegration::MotivoActividad::Respaldo);
        QCOMPARE(e.publicados().size(), qsizetype(1));

        // No vencido: programa la proxima evaluacion a las 24 h del exito.
        QVERIFY(QTest::qWaitFor([&] { return e.diario->proximaEvaluacionUtc().has_value(); }, 5000));
        QCOMPARE(*e.diario->proximaEvaluacionUtc(), e.gestor->estado().ultimoExitoUtc->addSecs(24 * 3600));
        e.fuente->pendiente = false;
        QVERIFY(e.os.emitirDespertar()); // despertar antes de vencer: nada
        QTest::qWait(20);
        QCOMPARE(e.diario->ejecucionesLanzadas(), 1);
        // Pasan 24 h (reloj inyectado): segunda ejecucion, una sola.
        e.programador.avanzarHasta(*e.diario->proximaEvaluacionUtc());
        QCOMPARE(e.diario->ejecucionesLanzadas(), 2);
        QVERIFY(e.esperarLibre());
        QVERIFY(QTest::qWaitFor([&] { return e.publicados().size() == 2; }, 10000));
    }

    void diarioSinPiezasInformaPendienteSinIntentar()
    {
        Entorno e;
        QVERIFY(e.preparar());
        esperar(e.gestor->iniciar());
        QVERIFY(esperar(e.gestor->activarDiario(true)));
        QVERIFY(QTest::qWaitFor([&] { return e.gestor->estado().pendienteDiario == Pendiente::FaltaCarpeta; }, 5000));
        QVERIFY(esperar(e.gestor->configurarCarpeta(e.nube)));
        QVERIFY(QTest::qWaitFor([&] { return e.gestor->estado().pendienteDiario == Pendiente::FaltaContrasena; }, 5000));
        for (int i = 0; i < 3; ++i) {
            QVERIFY(e.os.emitirDespertar());
        }
        QTest::qWait(30);
        QCOMPARE(e.diario->ejecucionesLanzadas(), 0);
        QCOMPARE(e.os.actividadesIniciadas(), 0);
        QVERIFY(e.publicados().isEmpty());
    }

    void diarioFallidoEsperaAntesDeReintentar()
    {
        Entorno e;
        QVERIFY(e.preparar());
        esperar(e.gestor->iniciar());
        QVERIFY(!e.configurarTodo().isEmpty());
        const QByteArray bookmark = e.configuracionIni->cargar().valor().configuracion.bookmark;
        e.carpetas.fallarResolver(bookmark, ErrorCarpetaRespaldo::Categoria::NoResuelve);
        QVERIFY(esperar(e.gestor->activarDiario(true)));
        QVERIFY(QTest::qWaitFor([&] { return e.diario->ejecucionesLanzadas() == 1; }, 5000));
        QVERIFY(e.esperarLibre());
        QVERIFY(QTest::qWaitFor([&] { return e.gestor->estado().ultimoIntentoUtc.has_value(); }, 5000));
        QCOMPARE(e.gestor->estado().ultimoResultado, claveEstable(Estado::ErrorDestino));
        // Rafaga de despertares: no reintenta en bucle (espera 1 h).
        for (int i = 0; i < 5; ++i) {
            QVERIFY(e.os.emitirDespertar());
        }
        QTest::qWait(50);
        QCOMPARE(e.diario->ejecucionesLanzadas(), 1);
        QVERIFY(e.diario->proximaEvaluacionUtc().has_value());
        QCOMPARE(*e.diario->proximaEvaluacionUtc(), e.gestor->estado().ultimoIntentoUtc->addSecs(3600));
        // La carpeta vuelve; a la hora reintenta y funciona.
        e.carpetas.fijarResolucion(bookmark, CarpetaRespaldoResuelta{e.nube, kUuidVolumen, false});
        e.programador.avanzarHasta(*e.diario->proximaEvaluacionUtc());
        QCOMPARE(e.diario->ejecucionesLanzadas(), 2);
        QVERIFY(QTest::qWaitFor([&] { return e.publicados().size() == 1; }, 10000));
    }

    void respaldoManualMantieneLaActividadDeSistema()
    {
        Entorno e;
        QVERIFY(e.preparar());
        esperar(e.gestor->iniciar());
        QVERIFY(!e.configurarTodo().isEmpty());
        e.fuente->pendiente = true;
        auto f = e.gestor->respaldarAhora();
        QCOMPARE(e.os.actividadesActivas(), 1);
        QVERIFY(QTest::qWaitFor([&] { return e.servicio->enCurso(); }, 5000));
        QCOMPARE(e.os.actividadesActivas(), 1);
        e.fuente->liberar();
        QVERIFY(esperar(f).exito());
        QCOMPARE(e.os.actividadesActivas(), 0);
        // Las KEK del respaldo se cargaron fuera del hilo grafico.
        for (QThread* h : e.almacen.hilos) {
            QVERIFY(h != QThread::currentThread());
        }
    }
};

int main(int argc, char* argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    TestGestorRespaldo prueba;
    return QTest::qExec(&prueba, argc, argv);
}

#include "TestGestorRespaldo.moc"
