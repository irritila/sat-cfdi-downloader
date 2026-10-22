// Pruebas del contenedor de respaldo AFB1 (T025.1, ADR 0021). Solo datos
// sinteticos (XAXX010101000, /Users/usuario) en QTemporaryDir. Pruebas largas,
// omitidas salvo su variable: SATCFDI_RESPALDO_PRUEBA_3GB=1 (un archivo de
// 3 GiB) y SATCFDI_RESPALDO_PRUEBA_100K=1 (100 000 entradas).

#include "infrastructure/crypto/ContenedorRespaldoFormato.h"
#include "infrastructure/crypto/ContenedorRespaldoOpenSsl.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#endif

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <optional>

using namespace satcfdi;
using namespace satcfdi::respaldo;
using crypto::ContenedorRespaldoOpenSsl;
using C = ErrorRespaldo::Categoria;

namespace {

constexpr qsizetype kCab = 204;
constexpr qsizetype kBloqueDisco = 5 + qsizetype(kTamanoBloque) + 16; // marco + datos + tag

const QByteArray kRfc = QByteArrayLiteral("XAXX010101000");
const QByteArray kHome = QByteArrayLiteral("/Users/usuario");
const QByteArray kCfdi = QByteArrayLiteral("<cfdi:");
const QByteArray kContrasenaSat = QByteArrayLiteral("CONTRASENA-SAT-SINTETICA-7Q2W9E");
const QString kContrasena1 = QStringLiteral("contrasena-sintetica-1");
const QString kContrasena2 = QStringLiteral("contrasena-sintetica-2");
const QUuid kInstalacion = QUuid(QStringLiteral("{6f1c2a7e-3b9d-4c5e-8f10-2a3b4c5d6e7f}"));
const QString kRutaZip = QStringLiteral("paquetes/XAXX010101000/2026-01/0b6f4c4e-6a8e-4d55-9a3f-2f1f6a1b9c01/PAQ_01.zip");
const QString kRutaBase = QStringLiteral("base/satcfdi.sqlite3");
const QString kRutaFaltante = QStringLiteral("paquetes/XAXX010101000/2026-02/0b6f4c4e-6a8e-4d55-9a3f-2f1f6a1b9c02/PAQ_02.zip");
const QString kPreferencias = QStringLiteral("preferencias.ini");
const QByteArray kDatosPreferencias = QByteArrayLiteral("[facturas]\nperfil=XAXX010101000\n");

BufferSecreto texto(const QString& s)
{
    return BufferSecreto::desdeTexto(s);
}

QByteArray bytesDe(const BufferSecreto& b)
{
    return QByteArray(reinterpret_cast<const char*>(b.datos()), static_cast<qsizetype>(b.tamano()));
}

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

// Contenido sintetico que se repite hasta `n` bytes.
QByteArray relleno(qsizetype n, const QByteArray& semilla)
{
    QByteArray b;
    b.reserve(n);
    qint64 i = 0;
    while (b.size() < n) {
        b.append(semilla);
        b.append(QByteArray::number(i++));
    }
    b.truncate(n);
    return b;
}

DatosNuevoRespaldo datosNuevos(QByteArray metadatos = QByteArrayLiteral("{\"esquema\":7,\"rfc\":\"XAXX010101000\"}"))
{
    DatosNuevoRespaldo d;
    d.idInstalacion = kInstalacion;
    d.creadoEnUtc = QDateTime(QDate(2026, 10, 21), QTime(12, 30, 15, 123), QTimeZone::UTC);
    d.metadatos = std::move(metadatos);
    return d;
}

bool esCategoria(const ErrorRespaldo& e, C c)
{
    if (e.categoria != c) {
        qWarning("categoria %s (%s), se esperaba %s", qPrintable(claveEstable(e.categoria)),
                 qPrintable(e.diagnostico), qPrintable(claveEstable(c)));
        return false;
    }
    return true;
}

bool diagnosticoLimpio(const ErrorRespaldo& e)
{
    const QByteArray d = e.diagnostico.toUtf8();
    return !d.contains(kRfc) && !d.contains(kHome) && !d.contains("..") && !d.contains('/')
        && !d.contains(kContrasena1.toUtf8()) && !d.contains(kContrasenaSat);
}

void ponerU32(QByteArray& b, qsizetype off, quint32 v)
{
    for (int i = 0; i < 4; ++i) {
        b[off + i] = static_cast<char>((v >> (24 - 8 * i)) & 0xFF);
    }
}

// Dispositivo secuencial que entrega fragmentos de 1 a 7 bytes y devuelve 0
// cada tercera llamada (C1). waitForReadyRead indica si quedan datos.
class DispositivoGoteo final : public QIODevice {
public:
    explicit DispositivoGoteo(QByteArray datos) : m_datos(std::move(datos)) {}

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (m_datos.size() - m_pos) + QIODevice::bytesAvailable(); }
    bool waitForReadyRead(int) override
    {
        ++esperas;
        return m_pos < m_datos.size();
    }

    int esperas = 0;
    int ceros = 0;

protected:
    qint64 readData(char* data, qint64 maximo) override
    {
        ++m_llamadas;
        if (m_pos >= m_datos.size()) {
            return 0;
        }
        if (m_llamadas % 3 == 0) {
            ++ceros;
            return 0;
        }
        const qint64 n = std::min({maximo, qint64(1 + m_llamadas % 7), qint64(m_datos.size() - m_pos)});
        std::memcpy(data, m_datos.constData() + m_pos, static_cast<std::size_t>(n));
        m_pos += n;
        return n;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    QByteArray m_datos;
    qint64 m_pos = 0;
    qint64 m_llamadas = 0;
};

// Captura de mensajes Qt durante un flujo (criterio 1: sin logs).
QMutex g_mutexMensajes;
QStringList g_mensajes;

void capturarMensaje(QtMsgType, const QMessageLogContext&, const QString& mensaje)
{
    QMutexLocker l(&g_mutexMensajes);
    g_mensajes.append(mensaje);
}

class CapturaMensajes {
public:
    CapturaMensajes()
    {
        QMutexLocker l(&g_mutexMensajes);
        g_mensajes.clear();
        m_previo = qInstallMessageHandler(capturarMensaje);
    }
    ~CapturaMensajes() { qInstallMessageHandler(m_previo); }
    QStringList mensajes() const
    {
        QMutexLocker l(&g_mutexMensajes);
        return g_mensajes;
    }

private:
    QtMessageHandler m_previo = nullptr;
};

#if defined(__APPLE__)
struct Huella {
    quint64 actual = 0;
    quint64 pico = 0;
};

Huella huella()
{
    task_vm_info_data_t info{};
    mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &n) != KERN_SUCCESS) {
        return {};
    }
    return {info.phys_footprint, static_cast<quint64>(std::max<std::int64_t>(info.ledger_phys_footprint_peak, 0))};
}

// Bytes en uso en todas las zonas de malloc: no se esconde detras de memoria
// ya liberada que el allocator retiene (la huella si).
quint64 enUso()
{
    malloc_statistics_t st{};
    malloc_zone_statistics(nullptr, &st);
    return st.size_in_use;
}
#endif

} // namespace

class TestContenedorRespaldo : public QObject {
    Q_OBJECT

private:
    ContenedorRespaldoOpenSsl cr;
    QByteArray sal1;
    QByteArray sal2;
    KekRespaldo kekPw1;
    KekRespaldo kekPw2;
    KekRespaldo kekPwIncorrecta;
    KekRespaldo kekRec;
    KekRespaldo kekRecOtra;
    std::unique_ptr<QTemporaryDir> tmp;
    quint64 huellaAntesDelPrimerKdf = 0;

    KekRespaldo derivar(const QString& contrasena, const QByteArray& sal)
    {
        auto k = cr.derivarKekContrasena(texto(contrasena), ParametrosKdf::predeterminados(sal));
        if (!k) {
            qWarning("derivar: %s", qPrintable(k.error().diagnostico));
            return {};
        }
        return std::move(k).valor();
    }

    KekRespaldo recuperacion()
    {
        auto clave = cr.generarClaveRecuperacion();
        if (!clave) {
            return {};
        }
        auto k = cr.derivarKekRecuperacion(clave.valor());
        return k ? std::move(k).valor() : KekRespaldo{};
    }

    QString ruta(const QString& relativa) const { return tmp->filePath(relativa); }
    QString origen() const { return ruta(QStringLiteral("origen")); }

    // Staging privado (0700, vacio).
    QString staging(const QString& nombre)
    {
        const QString d = ruta(nombre);
        QDir().mkpath(d);
        QFile::setPermissions(d, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return d;
    }

    bool stagingVacio(const QString& d) const
    {
        return QDir(d).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty();
    }

    // Fuentes sinteticas con los marcadores que nunca deben aparecer en claro.
    void crearFuentes(qsizetype tamanoZip = 2 * 1024 * 1024 + 12345)
    {
        QVERIFY(escribir(ruta(QStringLiteral("origen/satcfdi.sqlite3")),
                         relleno(300 * 1024, "SQLite format 3 perfil XAXX010101000 /Users/usuario/Archivo-Fiscal ")));
        QVERIFY(escribir(ruta(QStringLiteral("origen/XAXX010101000/2026-01/PAQ_01.zip")),
                         relleno(tamanoZip, "<cfdi:Comprobante Rfc=\"XAXX010101000\" Total=\"1160.00\"/> ")));
    }

    bool llenar(EscritorRespaldo& e)
    {
        return e.agregarArchivo(kRutaBase, origen(), QStringLiteral("satcfdi.sqlite3"))
            && e.agregarArchivo(kRutaZip, origen(), QStringLiteral("XAXX010101000/2026-01/PAQ_01.zip"))
            && e.agregarDatos(kPreferencias, kDatosPreferencias) && e.anotarFaltante(kRutaFaltante);
    }

    QByteArray respaldoCompleto(const KekRespaldo& kekContrasena, std::optional<qsizetype> tamanoZip = std::nullopt)
    {
        crearFuentes(tamanoZip.value_or(2 * 1024 * 1024 + 12345));
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekContrasena, kekRec, datosNuevos());
        if (!esc) {
            qWarning("crearEscritor: %s", qPrintable(esc.error().diagnostico));
            return {};
        }
        if (!llenar(*esc.valor()) || !esc.valor()->finalizar()) {
            return {};
        }
        return destino.data();
    }

    Resultado<InventarioRespaldo, ErrorRespaldo> verificarBytes(const QByteArray& contenedor, const KekRespaldo& kek,
                                                                QList<EntradaManifiesto>* entradas = nullptr)
    {
        QBuffer o;
        o.setData(contenedor);
        o.open(QIODevice::ReadOnly);
        OpcionesLectura op;
        if (entradas) {
            op.alLeerEntrada = [entradas](const EntradaManifiesto& e) { entradas->append(e); };
        }
        return cr.verificar(o, kek, op);
    }

    Resultado<InventarioRespaldo, ErrorRespaldo> extraerBytes(const QByteArray& contenedor, const KekRespaldo& kek,
                                                              const QString& destino)
    {
        QBuffer o;
        o.setData(contenedor);
        o.open(QIODevice::ReadOnly);
        return cr.extraer(o, kek, destino);
    }

    Resultado<CabeceraRespaldo, ErrorRespaldo> autenticarBytes(const QByteArray& contenedor, const KekRespaldo& kek)
    {
        QBuffer o;
        o.setData(contenedor);
        o.open(QIODevice::ReadOnly);
        return cr.autenticarCabecera(o, kek);
    }

    QByteArray crudo(const QByteArray& flujo)
    {
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto r = crypto::afb1::escribirFlujoCrudo(destino, kekPw1, kekRec, datosNuevos(), flujo);
        return r ? destino.data() : QByteArray();
    }

    // Un contenedor alterado no verifica ni deja nada en el staging al extraer.
    void esperarRechazo(const QByteArray& contenedor, const QString& caso)
    {
        auto v = verificarBytes(contenedor, kekPw1);
        QVERIFY2(!v, qPrintable(caso));
        QVERIFY2(diagnosticoLimpio(v.error()), qPrintable(caso));
        const QString destino = staging(QStringLiteral("staging-") + caso);
        QVERIFY2(!extraerBytes(contenedor, kekPw1, destino), qPrintable(caso));
        QVERIFY2(stagingVacio(destino), qPrintable(caso));
    }

private slots:
    void initTestCase()
    {
#if defined(__APPLE__)
        huellaAntesDelPrimerKdf = huella().actual;
#endif
        QVERIFY(cr.comprobarDisponible());
        auto s1 = cr.generarSal();
        auto s2 = cr.generarSal();
        QVERIFY(s1 && s2);
        sal1 = s1.valor();
        sal2 = s2.valor();
        QCOMPARE(sal1.size(), qsizetype(kTamanoSal));
        QVERIFY(sal1 != sal2);
        kekPw1 = derivar(kContrasena1, sal1);
        kekPw2 = derivar(kContrasena2, sal2);
        kekPwIncorrecta = derivar(QStringLiteral("contrasena-equivocada"), sal1);
        kekRec = recuperacion();
        kekRecOtra = recuperacion();
        QVERIFY(kekPw1.valida() && kekPw2.valida() && kekPwIncorrecta.valida());
        QVERIFY(kekRec.valida() && kekRecOtra.valida());
        QCOMPARE(kekRec.tipo(), KekRespaldo::Tipo::Recuperacion);
    }

    void init()
    {
        tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(tmp->isValid());
    }

    void cleanup() { tmp.reset(); }

    // --- KDF (D9, D12, seguridad 2). ---
    void argon2idDisponibleYVectorConocido()
    {
        QVERIFY(cr.comprobarDisponible());
        // Mismo calculo con el CLI de OpenSSL 3.6.3:
        // openssl kdf -keylen 32 -kdfopt pass:x -kdfopt salt:0123456789abcdef
        //   -kdfopt iter:3 -kdfopt memcost:65536 -kdfopt lanes:1 ARGON2ID
        auto k = cr.derivarKekContrasena(texto(QStringLiteral("x")),
                                         ParametrosKdf::predeterminados(QByteArrayLiteral("0123456789abcdef")));
        QVERIFY(k);
        QCOMPARE(bytesDe(k.valor().clave()).toHex().toUpper(),
                 QByteArrayLiteral("7CC03FA8AB040C8E3B8B63FF7E416981A68FE2EC140B7DD5E166E29D2078D7B4"));
        QCOMPARE(k.valor().parametros().memoriaKib, 65536u);
    }

    void kdfRechazaParametrosFueraDeRango_data()
    {
        QTest::addColumn<quint32>("memoria");
        QTest::addColumn<quint32>("iteraciones");
        QTest::addColumn<quint32>("paralelismo");
        QTest::addColumn<int>("tamanoSal");
        QTest::newRow("memoria 32 MiB") << 32768u << 3u << 1u << 16;
        QTest::newRow("t=2") << 65536u << 2u << 1u << 16;
        QTest::newRow("p=0") << 65536u << 3u << 0u << 16;
        QTest::newRow("sal 15") << 65536u << 3u << 1u << 15;
        QTest::newRow("memoria sobre el techo") << (256u * 1024u + 1u) << 3u << 1u << 16;
        QTest::newRow("t sobre el techo") << 65536u << 9u << 1u << 16;
        QTest::newRow("p sobre el techo") << 65536u << 3u << 5u << 16;
    }

    void kdfRechazaParametrosFueraDeRango()
    {
        QFETCH(quint32, memoria);
        QFETCH(quint32, iteraciones);
        QFETCH(quint32, paralelismo);
        QFETCH(int, tamanoSal);
        ParametrosKdf p;
        p.memoriaKib = memoria;
        p.iteraciones = iteraciones;
        p.paralelismo = paralelismo;
        p.sal = QByteArray(tamanoSal, 's');
        auto k = cr.derivarKekContrasena(texto(kContrasena1), p);
        QVERIFY(!k);
        QVERIFY(esCategoria(k.error(), C::ParametrosKdfInvalidos));
    }

    void kdfEnElTecho()
    {
        ParametrosKdf memoria = ParametrosKdf::predeterminados(sal1);
        memoria.memoriaKib = ParametrosKdf::kMemoriaKibMaxima; // 256 MiB
        QVERIFY(cr.derivarKekContrasena(texto(kContrasena1), memoria));
        ParametrosKdf iteraciones = ParametrosKdf::predeterminados(sal1);
        iteraciones.iteraciones = ParametrosKdf::kIteracionesMaximas; // 8
        QVERIFY(cr.derivarKekContrasena(texto(kContrasena1), iteraciones));

        // En la cabecera (lectura hostil): en el techo se acepta, encima no.
        const QByteArray contenedor = respaldoCompleto(kekPw1, 4096);
        struct Caso {
            qsizetype offset;
            quint32 valor;
            bool aceptado;
        };
        for (const Caso c : {Caso{48, 256u * 1024u, true}, Caso{48, 256u * 1024u + 1u, false}, Caso{52, 8u, true},
                             Caso{52, 9u, false}}) {
            QByteArray b = contenedor;
            ponerU32(b, c.offset, c.valor);
            QBuffer o;
            o.setData(b);
            o.open(QIODevice::ReadOnly);
            auto cab = cr.leerCabecera(o);
            QCOMPARE(bool(cab), c.aceptado);
            if (!c.aceptado) {
                QVERIFY(esCategoria(cab.error(), C::ParametrosKdfInvalidos));
            }
        }
    }

    void contrasenaVaciaRechazada()
    {
        auto k = cr.derivarKekContrasena(BufferSecreto(), ParametrosKdf::predeterminados(sal1));
        QVERIFY(!k);
        QVERIFY(esCategoria(k.error(), C::ParametrosInvalidos));
    }

    // --- Ida y vuelta. ---
    void idaYVueltaConExtraccion()
    {
        const QByteArray contenedor = respaldoCompleto(kekPw1);
        QVERIFY(!contenedor.isEmpty());

        QList<EntradaManifiesto> entradas;
        auto v = verificarBytes(contenedor, kekPw1, &entradas);
        QVERIFY2(v, v ? "" : qPrintable(v.error().diagnostico));
        const InventarioRespaldo& inv = v.valor();
        QCOMPARE(inv.metadatos, datosNuevos().metadatos);
        QCOMPARE(inv.entradas(), 4u);
        QCOMPARE(inv.presentes, 3u);
        QCOMPARE(inv.faltantes, 1u);
        QCOMPARE(entradas.size(), qsizetype(4));
        QCOMPARE(entradas.at(0).rutaRelativa, kRutaBase);
        QCOMPARE(entradas.at(1).rutaRelativa, kRutaZip);
        QCOMPARE(entradas.at(1).sha256,
                 crypto::afb1::sha256(leer(ruta(QStringLiteral("origen/XAXX010101000/2026-01/PAQ_01.zip")))));
        QCOMPARE(entradas.at(3).estado, EntradaManifiesto::Estado::Faltante);
        QCOMPARE(inv.cabecera.idInstalacion, kInstalacion);
        QCOMPARE(inv.cabecera.creadoEnUtc, datosNuevos().creadoEnUtc);
        QCOMPARE(inv.cabecera.kdf, kekPw1.parametros());

        const QString destino = staging(QStringLiteral("staging"));
        quint64 ultimo = 0;
        bool monotono = true;
        QBuffer o;
        o.setData(contenedor);
        o.open(QIODevice::ReadOnly);
        OpcionesLectura op;
        op.progreso = [&](quint64 n) {
            monotono = monotono && n >= ultimo;
            ultimo = n;
        };
        auto x = cr.extraer(o, kekPw1, destino, op);
        QVERIFY2(x, x ? "" : qPrintable(x.error().diagnostico));
        QVERIFY(monotono);
        QCOMPARE(ultimo, x.valor().bytesDatos);
        QCOMPARE(leer(destino + QLatin1Char('/') + kRutaBase), leer(ruta(QStringLiteral("origen/satcfdi.sqlite3"))));
        QCOMPARE(leer(destino + QLatin1Char('/') + kRutaZip),
                 leer(ruta(QStringLiteral("origen/XAXX010101000/2026-01/PAQ_01.zip"))));
        QCOMPARE(leer(destino + QLatin1Char('/') + kPreferencias), kDatosPreferencias);
        QVERIFY(!QFileInfo::exists(destino + QLatin1Char('/') + kRutaFaltante));
        QCOMPARE(QFileInfo(destino + QLatin1Char('/') + kRutaZip).permissions() & QFile::ReadOther,
                 QFileDevice::Permissions{});
    }

    void respaldoEnArchivoYCabeceraLegibleSinClave()
    {
        crearFuentes();
        QVERIFY(QDir().mkpath(ruta(QStringLiteral("destino"))));
        const QString archivo = ruta(QStringLiteral("destino/archivo-fiscal.afbak.partial"));
        QFile f(archivo);
        QVERIFY(f.open(QIODevice::WriteOnly));
        auto esc = cr.crearEscritor(f, kekPw1, kekRec, datosNuevos());
        QVERIFY(esc);
        QVERIFY(esc.valor()->agregarArchivo(kRutaZip, origen(), QStringLiteral("XAXX010101000/2026-01/PAQ_01.zip")));
        QVERIFY(esc.valor()->finalizar());
        f.close();

        QFile g(archivo);
        QVERIFY(g.open(QIODevice::ReadOnly));
        auto cab = cr.leerCabecera(g);
        QVERIFY(cab);
        QCOMPARE(cab.valor().version, quint8(1));
        QCOMPARE(cab.valor().idInstalacion, kInstalacion);
        QCOMPARE(cab.valor().idRespaldo.size(), qsizetype(kTamanoIdRespaldo));
        QCOMPARE(cab.valor().kdf, kekPw1.parametros());
        QCOMPARE(cab.valor().tamanoBloque, quint32(kTamanoBloque));
        QVERIFY(cr.autenticarCabecera(g, kekPw1));
        QVERIFY(cr.autenticarCabecera(g, kekRec));
        auto mal = cr.autenticarCabecera(g, kekPwIncorrecta);
        QVERIFY(!mal);
        QVERIFY(esCategoria(mal.error(), C::CredencialIncorrecta));
        QVERIFY(cr.verificar(g, kekPw1));
    }

    void dekEIdAleatoriosPorRespaldo()
    {
        const QByteArray a = respaldoCompleto(kekPw1);
        const QByteArray b = respaldoCompleto(kekPw1);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        QCOMPARE(a.size(), b.size());
        QVERIFY(a.mid(24, 16) != b.mid(24, 16));   // idRespaldo
        QVERIFY(a.mid(80, 4) != b.mid(80, 4));     // prefijo de nonce
        QVERIFY(a.mid(84, 120) != b.mid(84, 120)); // envolturas
        QVERIFY(a.mid(kCab + 5, 4096) != b.mid(kCab + 5, 4096));
        QCOMPARE(a.mid(60, 16), sal1); // sal fija por contrasena
        QCOMPARE(b.mid(60, 16), sal1);
    }

    // --- Nada en claro (criterio 1). ---
    void sinDatosSensiblesEnArchivoEnDisco()
    {
        crearFuentes();
        // Una contrasena SAT sintetica dentro de credentials/: nunca entra.
        QVERIFY(escribir(ruta(QStringLiteral("origen/credentials/0b6f4c4e.scc")), kContrasenaSat));
        QVERIFY(escribir(ruta(QStringLiteral("origen/credentials.txt")), kContrasenaSat));
        QVERIFY(QDir().mkpath(ruta(QStringLiteral("nube"))));
        const QString archivo = ruta(QStringLiteral("nube/archivo-fiscal-20261021.afbak.partial"));

        CapturaMensajes captura;
        {
            QFile f(archivo);
            QVERIFY(f.open(QIODevice::WriteOnly));
            auto esc = cr.crearEscritor(f, kekPw1, kekRec, datosNuevos());
            QVERIFY(esc);
            auto& e = *esc.valor();
            QVERIFY(llenar(e));
            auto c1 = e.agregarArchivo(QStringLiteral("credentials/0b6f4c4e.scc"), origen(),
                                       QStringLiteral("credentials/0b6f4c4e.scc"));
            QVERIFY(!c1);
            QCOMPARE(c1.error().categoria, C::RutaInvalida);
            auto c2 = e.agregarArchivo(QStringLiteral("CREDENTIALS/x"), origen(), QStringLiteral("credentials.txt"));
            QVERIFY(!c2);
            QVERIFY(e.finalizar());
            QVERIFY(f.flush());
            f.close();

            QFile g(archivo);
            QVERIFY(g.open(QIODevice::ReadOnly));
            QVERIFY(cr.verificar(g, kekPw1));
            QVERIFY(!cr.verificar(g, kekPwIncorrecta));
            const QString destino = staging(QStringLiteral("staging"));
            QVERIFY(cr.extraer(g, kekRec, destino));
            QVERIFY(!QFileInfo::exists(destino + QStringLiteral("/credentials")));
        }
        const QStringList mensajes = captura.mensajes();
        QVERIFY2(mensajes.isEmpty(), qPrintable(mensajes.join(QLatin1Char('\n'))));

        const QByteArray enDisco = leer(archivo);
        QVERIFY(enDisco.size() > 2 * 1024 * 1024);
        for (const QByteArray& marcador :
             {kRfc, kHome, kCfdi, kContrasenaSat, kContrasena1.toUtf8(), QByteArrayLiteral("preferencias"),
              QByteArrayLiteral("paquetes"), QByteArrayLiteral("SQLite format"), QByteArrayLiteral("credentials")}) {
            QVERIFY2(!enDisco.contains(marcador), marcador.constData());
        }
    }

    void credentialsNuncaEntra()
    {
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekPw1, kekRec, datosNuevos());
        QVERIFY(esc);
        for (const QString& r : {QStringLiteral("credentials/x.scc"), QStringLiteral("Credentials/x")}) {
            auto a = esc.valor()->agregarDatos(r, QByteArrayLiteral("contenedor"));
            QVERIFY(!a);
            QVERIFY(esCategoria(a.error(), C::RutaInvalida));
        }
        QVERIFY(esc.valor()->agregarDatos(QStringLiteral("datos/credentials"), QByteArrayLiteral("ok")));
        QVERIFY(esc.valor()->finalizar());
    }

    // --- Alteraciones (criterio 2). ---
    void alterarCualquierBloqueFalla()
    {
        const QByteArray contenedor = respaldoCompleto(kekPw1);
        QVERIFY(contenedor.size() - kCab > 2 * kBloqueDisco); // al menos 3 bloques
        const QList<qsizetype> offsets = {
            kCab,                         // largo del marco del bloque 0
            kCab + 4,                     // marca de final del bloque 0
            kCab + 5,                     // primer byte cifrado
            kCab + kBloqueDisco - 1,      // tag del bloque 0
            kCab + kBloqueDisco + 3,      // largo del marco del bloque 1
            kCab + kBloqueDisco + 500,    // datos del bloque 1
            kCab + 2 * kBloqueDisco + 4,  // marca de final del ultimo bloque
            contenedor.size() - 1,        // tag del ultimo
            contenedor.size() - 17,       // ultimo byte cifrado
        };
        for (qsizetype off : offsets) {
            QByteArray alterado = contenedor;
            alterado[off] = static_cast<char>(alterado.at(off) ^ 0x01);
            esperarRechazo(alterado, QStringLiteral("byte-%1").arg(off));
        }
    }

    void alterarCabeceraFalla_data()
    {
        QTest::addColumn<int>("offset");
        QTest::addColumn<int>("categoria");
        QTest::newRow("magic") << 0 << int(C::FormatoNoReconocido);
        QTest::newRow("version") << 4 << int(C::VersionNoSoportada);
        QTest::newRow("instalacion") << 10 << int(C::CredencialIncorrecta);
        QTest::newRow("id respaldo") << 30 << int(C::CredencialIncorrecta);
        QTest::newRow("fecha") << 47 << int(C::CredencialIncorrecta);
        QTest::newRow("iteraciones") << 55 << int(C::ParametrosKdfInvalidos);
        QTest::newRow("sal") << 61 << int(C::CredencialIncorrecta);
        QTest::newRow("prefijo nonce") << 81 << int(C::CredencialIncorrecta);
        QTest::newRow("envoltura contrasena") << 100 << int(C::CredencialIncorrecta);
        QTest::newRow("envoltura recuperacion (contrasena intacta)") << 160 << int(C::Danado);
    }

    void alterarCabeceraFalla()
    {
        QFETCH(int, offset);
        QFETCH(int, categoria);
        QByteArray contenedor = respaldoCompleto(kekPw1, 4096);
        contenedor[offset] = static_cast<char>(contenedor.at(offset) ^ 0x01);
        auto v = verificarBytes(contenedor, kekPw1);
        QVERIFY(!v);
        QVERIFY(esCategoria(v.error(), static_cast<C>(categoria)));
        QVERIFY(!autenticarBytes(contenedor, kekPw1));
        esperarRechazo(contenedor, QStringLiteral("cabecera"));
    }

    void alterarEnvolturaDelOtroRol()
    {
        const QByteArray contenedor = respaldoCompleto(kekPw1, 4096);
        QVERIFY(autenticarBytes(contenedor, kekPw1));
        QVERIFY(autenticarBytes(contenedor, kekRec));

        QByteArray sinContrasena = contenedor; // envoltura de contrasena alterada
        sinContrasena[100] = static_cast<char>(sinContrasena.at(100) ^ 0x01);
        auto a = autenticarBytes(sinContrasena, kekRec);
        QVERIFY(!a);
        QVERIFY(esCategoria(a.error(), C::Danado));
        QVERIFY(!verificarBytes(sinContrasena, kekRec));

        QByteArray sinRecuperacion = contenedor; // envoltura de recuperacion alterada
        sinRecuperacion[170] = static_cast<char>(sinRecuperacion.at(170) ^ 0x01);
        auto b = autenticarBytes(sinRecuperacion, kekPw1);
        QVERIFY(!b);
        QVERIFY(esCategoria(b.error(), C::Danado));
        QVERIFY(!verificarBytes(sinRecuperacion, kekPw1));
    }

    void truncarFalla()
    {
        const QByteArray contenedor = respaldoCompleto(kekPw1);
        esperarRechazo(contenedor.left(contenedor.size() - 1), QStringLiteral("trunc-1"));
        esperarRechazo(contenedor.left(contenedor.size() - 40), QStringLiteral("trunc-40"));
        qsizetype ultimo = (contenedor.size() - kCab) % kBloqueDisco;
        if (ultimo == 0) {
            ultimo = kBloqueDisco;
        }
        esperarRechazo(contenedor.left(contenedor.size() - ultimo), QStringLiteral("sin-ultimo"));
        esperarRechazo(contenedor.left(kCab + 3), QStringLiteral("marco-corto"));
        esperarRechazo(contenedor.left(kCab), QStringLiteral("solo-cabecera"));
        esperarRechazo(contenedor.left(150), QStringLiteral("cabecera-corta"));
    }

    void truncadoConCabeceraDeOtraCopia()
    {
        const QByteArray a = respaldoCompleto(kekPw1);
        const QByteArray b = respaldoCompleto(kekPw1);
        // Cabecera autentica de A sobre los bloques de B: no autentica.
        const QByteArray mezcla = a.left(kCab) + b.mid(kCab, kBloqueDisco);
        QVERIFY(!autenticarBytes(mezcla, kekPw1));
        esperarRechazo(a.left(kCab) + b.mid(kCab), QStringLiteral("mezcla"));
        // A truncado tras su bloque 0: la cabecera es autentica (documentado:
        // T025.3 deduplica por idRespaldo), pero el respaldo no verifica.
        const QByteArray truncado = a.left(kCab + kBloqueDisco);
        auto cab = autenticarBytes(truncado, kekPw1);
        QVERIFY(cab);
        QCOMPARE(cab.valor().idRespaldo, a.mid(24, 16));
        auto v = verificarBytes(truncado, kekPw1);
        QVERIFY(!v);
        QVERIFY(esCategoria(v.error(), C::Danado));
    }

    void reordenarODuplicarBloquesFalla()
    {
        const QByteArray contenedor = respaldoCompleto(kekPw1);
        const QByteArray cab = contenedor.left(kCab);
        const QByteArray b0 = contenedor.mid(kCab, kBloqueDisco);
        const QByteArray b1 = contenedor.mid(kCab + kBloqueDisco, kBloqueDisco);
        const QByteArray resto = contenedor.mid(kCab + 2 * kBloqueDisco);
        esperarRechazo(cab + b1 + b0 + resto, QStringLiteral("reordenado"));
        esperarRechazo(cab + b0 + b0 + b1 + resto, QStringLiteral("duplicado"));
        esperarRechazo(contenedor + QByteArray(32, 'x'), QStringLiteral("basura-final"));
        esperarRechazo(contenedor + b0, QStringLiteral("bloque-extra"));
    }

    void bloqueFinalExactamenteLleno()
    {
        // Flujo logico de exactamente 2 MiB: el ultimo bloque esta lleno y es
        // el final por su marco; cortar tras el primero debe fallar.
        const QByteArray rutaUtf8 = QByteArrayLiteral("a.bin");
        const QByteArray meta = QByteArrayLiteral("m");
        const qsizetype fijo = (5 + meta.size()) + (1 + 1 + 2 + rutaUtf8.size() + 8 + 32)
            + (5 + 1 + 2 + rutaUtf8.size() + 8 + 32);
        const QByteArray datos = relleno(qsizetype(2 * kTamanoBloque) - fijo, "XAXX010101000 ");
        const QByteArray sha = crypto::afb1::sha256(datos);
        const QByteArray flujo = crypto::afb1::registroMetadatos(meta)
            + crypto::afb1::registroEntrada(0, rutaUtf8, datos, sha)
            + crypto::afb1::registroManifiesto({{0, rutaUtf8, quint64(datos.size()), sha}});
        QCOMPARE(flujo.size(), qsizetype(2 * kTamanoBloque));
        const QByteArray contenedor = crudo(flujo);
        QCOMPARE(contenedor.size(), kCab + 2 * kBloqueDisco);
        QVERIFY(verificarBytes(contenedor, kekPw1));
        esperarRechazo(contenedor.left(kCab + kBloqueDisco), QStringLiteral("borde"));
    }

    // --- C1: dispositivo secuencial fragmentado. ---
    void dispositivoSecuencialFragmentado()
    {
        const QByteArray contenedor = respaldoCompleto(kekPw1, 1024 * 1024 + 4321);
        {
            DispositivoGoteo d(contenedor);
            QVERIFY(d.open(QIODevice::ReadOnly));
            auto cab = cr.autenticarCabecera(d, kekPw1);
            QVERIFY2(cab, cab ? "" : qPrintable(cab.error().diagnostico));
        }
        {
            DispositivoGoteo d(contenedor);
            QVERIFY(d.open(QIODevice::ReadOnly));
            auto v = cr.verificar(d, kekPw1);
            QVERIFY2(v, v ? "" : qPrintable(v.error().diagnostico));
            QCOMPARE(v.valor().entradas(), 4u);
            QVERIFY(d.ceros > 0);
            QVERIFY(d.esperas > 0);
        }
        {
            DispositivoGoteo d(contenedor);
            QVERIFY(d.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
            const QString destino = staging(QStringLiteral("staging-goteo"));
            auto x = cr.extraer(d, kekRec, destino);
            QVERIFY2(x, x ? "" : qPrintable(x.error().diagnostico));
            QCOMPARE(leer(destino + QLatin1Char('/') + kPreferencias), kDatosPreferencias);
        }
        {
            DispositivoGoteo d(contenedor.left(contenedor.size() - 1));
            QVERIFY(d.open(QIODevice::ReadOnly));
            auto v = cr.verificar(d, kekPw1);
            QVERIFY(!v);
            QVERIFY(esCategoria(v.error(), C::Danado));
        }
        {
            DispositivoGoteo d(contenedor + QByteArrayLiteral("x"));
            QVERIFY(d.open(QIODevice::ReadOnly));
            auto v = cr.verificar(d, kekPw1);
            QVERIFY(!v);
            QVERIFY(esCategoria(v.error(), C::Danado));
        }
    }

    // --- Credenciales (criterio 2). ---
    void contrasenaOClaveIncorrectaGenerica()
    {
        const QByteArray contenedor = respaldoCompleto(kekPw1, 4096);
        auto a = verificarBytes(contenedor, kekPwIncorrecta);
        auto b = verificarBytes(contenedor, kekRecOtra);
        auto c = verificarBytes(contenedor, kekPw2); // otra sal
        QVERIFY(!a && !b && !c);
        QVERIFY(esCategoria(a.error(), C::CredencialIncorrecta));
        QCOMPARE(a.error(), b.error());
        QCOMPARE(a.error(), c.error());
        QCOMPARE(a.error().diagnostico, QStringLiteral("credencial"));
        QCOMPARE(autenticarBytes(contenedor, kekRecOtra).error(), a.error());
    }

    void claveRecuperacionAbreElMismoRespaldo()
    {
        auto clave = cr.generarClaveRecuperacion();
        QVERIFY(clave);
        auto texto1 = cr.formatearClaveRecuperacion(clave.valor());
        QVERIFY(texto1);
        const QString impreso = QString::fromLatin1(bytesDe(texto1.valor()));
        QVERIFY2(QRegularExpression(QStringLiteral("^([0-9A-HJKMNP-TV-Z]{4}-){13}[0-9A-HJKMNP-TV-Z]{4}$"))
                     .match(impreso)
                     .hasMatch(),
                 qPrintable(impreso.left(4)));

        // El usuario la teclea en minusculas, con espacios y O/l en vez de 0/1.
        QString tecleado = impreso.toLower().replace(QLatin1Char('-'), QLatin1Char(' '));
        tecleado.replace(QLatin1Char('0'), QLatin1Char('o'));
        tecleado.replace(QLatin1Char('1'), QLatin1Char('l'));
        auto leida = cr.leerClaveRecuperacion(texto(tecleado));
        QVERIFY(leida);
        QVERIFY(leida.valor().bytes().igualA(clave.valor().bytes().datos(), clave.valor().bytes().tamano()));

        auto kekA = cr.derivarKekRecuperacion(clave.valor());
        auto kekB = cr.derivarKekRecuperacion(leida.valor());
        QVERIFY(kekA && kekB);

        crearFuentes(4096);
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekPw1, kekA.valor(), datosNuevos());
        QVERIFY(esc);
        QVERIFY(esc.valor()->agregarArchivo(kRutaZip, origen(), QStringLiteral("XAXX010101000/2026-01/PAQ_01.zip")));
        QVERIFY(esc.valor()->finalizar());
        QList<EntradaManifiesto> conPw;
        QList<EntradaManifiesto> conClave;
        QVERIFY(verificarBytes(destino.data(), kekPw1, &conPw));
        QVERIFY(verificarBytes(destino.data(), kekB.valor(), &conClave));
        QCOMPARE(conPw, conClave);
    }

    void claveRecuperacionMalFormada_data()
    {
        QTest::addColumn<int>("caso");
        QTest::newRow("caracter cambiado") << 0;
        QTest::newRow("corta") << 1;
        QTest::newRow("larga") << 2;
        QTest::newRow("caracter U") << 3;
        QTest::newRow("vacia") << 4;
    }

    void claveRecuperacionMalFormada()
    {
        QFETCH(int, caso);
        auto clave = cr.generarClaveRecuperacion();
        QVERIFY(clave);
        QString t = QString::fromLatin1(bytesDe(cr.formatearClaveRecuperacion(clave.valor()).valor()));
        switch (caso) {
        case 0: t[5] = t.at(5) == QLatin1Char('A') ? QLatin1Char('B') : QLatin1Char('A'); break;
        case 1: t.chop(1); break;
        case 2: t.append(QLatin1Char('A')); break;
        case 3: t[0] = QLatin1Char('U'); break;
        default: t.clear(); break;
        }
        auto r = cr.leerClaveRecuperacion(texto(t));
        QVERIFY(!r);
        QVERIFY(esCategoria(r.error(), C::ClaveRecuperacionMalFormada));
    }

    void cambioDeContrasenaConservaRecuperacionD16()
    {
        const QByteArray anterior = respaldoCompleto(kekPw1, 4096); // contrasena 1 + R
        const QByteArray nuevo = respaldoCompleto(kekPw2, 4096);    // contrasena 2 (sal nueva) + R
        QVERIFY(verificarBytes(anterior, kekPw1));
        QVERIFY(verificarBytes(anterior, kekRec));
        QVERIFY(verificarBytes(nuevo, kekPw2));
        QVERIFY(verificarBytes(nuevo, kekRec));
        QVERIFY(!verificarBytes(anterior, kekPw2));
        QVERIFY(!verificarBytes(nuevo, kekPw1));
        QBuffer b;
        b.setData(anterior);
        b.open(QIODevice::ReadOnly);
        auto cab = cr.leerCabecera(b);
        QVERIFY(cab);
        auto rederivada = cr.derivarKekContrasena(texto(kContrasena1), cab.valor().kdf);
        QVERIFY(rederivada);
        QVERIFY(verificarBytes(anterior, rederivada.valor()));
        QVERIFY(!verificarBytes(anterior, kekRecOtra)); // clave nueva: solo copias futuras
    }

    void kekReconstruidaDesdeBytes()
    {
        KekRespaldo copia = KekRespaldo::desdeBytes(
            KekRespaldo::Tipo::Contrasena, kekPw1.parametros(),
            BufferSecreto::desdeBytes(kekPw1.clave().datos(), kekPw1.clave().tamano()));
        QVERIFY(copia.valida());
        QVERIFY(verificarBytes(respaldoCompleto(copia, 4096), kekPw1));
        QVERIFY(!KekRespaldo::desdeBytes(KekRespaldo::Tipo::Contrasena, {}, BufferSecreto(32)).valida());
        QVERIFY(!KekRespaldo::desdeBytes(KekRespaldo::Tipo::Recuperacion, {}, BufferSecreto(16)).valida());
    }

    void escritorRechazaKekDeTipoIncorrecto()
    {
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto a = cr.crearEscritor(destino, kekRec, kekRec, datosNuevos());
        auto b = cr.crearEscritor(destino, kekPw1, kekPw1, datosNuevos());
        DatosNuevoRespaldo sinInstalacion = datosNuevos();
        sinInstalacion.idInstalacion = QUuid();
        auto c = cr.crearEscritor(destino, kekPw1, kekRec, std::move(sinInstalacion));
        QVERIFY(!a && !b && !c);
        QVERIFY(esCategoria(a.error(), C::ParametrosInvalidos));
        QVERIFY(esCategoria(b.error(), C::ParametrosInvalidos));
        QVERIFY(esCategoria(c.error(), C::ParametrosInvalidos));
        QCOMPARE(destino.data().size(), qsizetype(0));
    }

    // --- Rutas hostiles (criterio 3). ---
    void escritorRechazaRutasHostiles_data()
    {
        QTest::addColumn<QString>("rutaRelativa");
        QTest::newRow("vacia") << QString();
        QTest::newRow("padre") << QStringLiteral("../fuera.zip");
        QTest::newRow("padre interno") << QStringLiteral("paquetes/../../fuera.zip");
        QTest::newRow("absoluta") << QStringLiteral("/Users/usuario/fuera.zip");
        QTest::newRow("punto") << QStringLiteral("a/./b");
        QTest::newRow("oculta") << QStringLiteral("a/.DS_Store");
        QTest::newRow("doble barra") << QStringLiteral("a//b");
        QTest::newRow("barra final") << QStringLiteral("a/b/");
        QTest::newRow("tilde") << QStringLiteral("~/b");
        QTest::newRow("barra invertida") << QStringLiteral("a\\b");
        QTest::newRow("dos puntos") << QStringLiteral("C:x");
        QTest::newRow("control") << QStringLiteral("a\nb");
        QTest::newRow("no NFC") << QStringLiteral("é.zip");
        QTest::newRow("componente largo") << QString(256, QLatin1Char('a'));
        QTest::newRow("credentials") << QStringLiteral("credentials/0b6f4c4e.scc");
    }

    void escritorRechazaRutasHostiles()
    {
        QFETCH(QString, rutaRelativa);
        QVERIFY(!esRutaRelativaValida(rutaRelativa));
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekPw1, kekRec, datosNuevos());
        QVERIFY(esc);
        auto a = esc.valor()->agregarDatos(rutaRelativa, QByteArrayLiteral("x"));
        QVERIFY(!a);
        QVERIFY(esCategoria(a.error(), C::RutaInvalida));
        QVERIFY(diagnosticoLimpio(a.error()));
        QVERIFY(!esc.valor()->anotarFaltante(rutaRelativa));
        QVERIFY(esc.valor()->agregarDatos(QStringLiteral("ok.txt"), QByteArrayLiteral("x")));
        auto inv = esc.valor()->finalizar();
        QVERIFY(inv);
        QCOMPARE(inv.valor().entradas(), 1u);
    }

    void escritorRechazaDuplicadosYChoques()
    {
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekPw1, kekRec, datosNuevos());
        QVERIFY(esc);
        auto& e = *esc.valor();
        QVERIFY(e.agregarDatos(QStringLiteral("paquetes/XAXX010101000/a.zip"), QByteArrayLiteral("1")));
        QVERIFY(!e.agregarDatos(QStringLiteral("paquetes/XAXX010101000/a.zip"), QByteArrayLiteral("2")));
        QVERIFY(!e.agregarDatos(QStringLiteral("PAQUETES/xaxx010101000/A.ZIP"), QByteArrayLiteral("2")));
        QVERIFY(!e.agregarDatos(QStringLiteral("paquetes/XAXX010101000/a.zip/b"), QByteArrayLiteral("2")));
        QVERIFY(!e.anotarFaltante(QStringLiteral("paquetes")));
        QVERIFY(e.finalizar());
    }

    void lectorRechazaRutasHostiles_data()
    {
        QTest::addColumn<QByteArray>("rutaUtf8");
        QTest::addColumn<int>("clase");
        QTest::addColumn<bool>("faltante");
        QTest::newRow("padre") << QByteArrayLiteral("../evil") << 0 << false;
        QTest::newRow("padre profundo") << QByteArrayLiteral("a/../../evil") << 0 << false;
        QTest::newRow("absoluta") << QByteArrayLiteral("/tmp/evil") << 0 << false;
        QTest::newRow("absoluta home") << QByteArrayLiteral("/Users/usuario/evil") << 0 << false;
        QTest::newRow("utf8 invalido") << QByteArrayLiteral("a\xff\xfe") << 0 << false;
        QTest::newRow("nul") << QByteArray("a\0b", 3) << 0 << false;
        QTest::newRow("enlace") << QByteArrayLiteral("enlace") << 1 << false;
        QTest::newRow("dispositivo") << QByteArrayLiteral("dev") << 7 << false;
        QTest::newRow("faltante padre") << QByteArrayLiteral("../evil") << 0 << true;
        QTest::newRow("credentials") << QByteArrayLiteral("credentials/x") << 0 << false;
    }

    void lectorRechazaRutasHostiles()
    {
        QFETCH(QByteArray, rutaUtf8);
        QFETCH(int, clase);
        QFETCH(bool, faltante);
        const QByteArray datos = QByteArrayLiteral("XAXX010101000 contenido hostil");
        const QByteArray sha = crypto::afb1::sha256(datos);
        const QByteArray registro = faltante
            ? crypto::afb1::registroFaltante(rutaUtf8)
            : crypto::afb1::registroEntrada(static_cast<std::uint8_t>(clase), rutaUtf8, datos, sha);
        const QByteArray flujo = crypto::afb1::registroMetadatos({}) + registro
            + crypto::afb1::registroManifiesto(
                {{static_cast<std::uint8_t>(faltante ? 1 : 0), rutaUtf8, faltante ? 0u : quint64(datos.size()),
                  faltante ? QByteArray(32, '\0') : sha}});
        const QByteArray contenedor = crudo(flujo);
        QVERIFY(!contenedor.isEmpty());

        auto v = verificarBytes(contenedor, kekPw1);
        QVERIFY(!v);
        QVERIFY(esCategoria(v.error(), C::RutaInvalida));
        QVERIFY(diagnosticoLimpio(v.error()));

        const QString destino = staging(QStringLiteral("dentro/staging"));
        QVERIFY(!extraerBytes(contenedor, kekPw1, destino));
        QVERIFY(stagingVacio(destino));
        QVERIFY(!QFileInfo::exists(ruta(QStringLiteral("dentro/evil"))));
        QVERIFY(!QFileInfo::exists(ruta(QStringLiteral("evil"))));
    }

    void lectorRechazaManifiestoIncoherente_data()
    {
        QTest::addColumn<int>("caso");
        QTest::newRow("cuenta") << 0;
        QTest::newRow("sha en manifiesto") << 1;
        QTest::newRow("sha de entrada") << 2;
        QTest::newRow("sin manifiesto") << 3;
        QTest::newRow("registro tras manifiesto") << 4;
        QTest::newRow("sin metadatos") << 5;
        QTest::newRow("tipo desconocido") << 6;
        QTest::newRow("ruta distinta") << 7;
        QTest::newRow("tamano declarado mayor") << 8;
        QTest::newRow("duplicada") << 9;
    }

    void lectorRechazaManifiestoIncoherente()
    {
        QFETCH(int, caso);
        namespace f = crypto::afb1;
        const QByteArray ruta1 = QByteArrayLiteral("a/uno.zip");
        const QByteArray datos = QByteArrayLiteral("contenido sintetico XAXX010101000");
        const QByteArray sha = f::sha256(datos);
        const QByteArray meta = f::registroMetadatos(QByteArrayLiteral("m"));
        const QByteArray entrada = f::registroEntrada(0, ruta1, datos, sha);
        const f::FilaManifiestoCruda fila{0, ruta1, quint64(datos.size()), sha};
        QByteArray flujo;
        switch (caso) {
        case 0: flujo = meta + entrada + f::registroManifiesto({fila, fila}); break;
        case 1: {
            f::FilaManifiestoCruda otra = fila;
            otra.sha256 = f::sha256("otro");
            flujo = meta + entrada + f::registroManifiesto({otra});
            break;
        }
        case 2: flujo = meta + f::registroEntrada(0, ruta1, datos, f::sha256("otro")) + f::registroManifiesto({fila}); break;
        case 3: flujo = meta + entrada; break;
        case 4: flujo = meta + entrada + f::registroManifiesto({fila}) + f::registroFaltante("b"); break;
        case 5: flujo = entrada + f::registroManifiesto({fila}); break;
        case 6: flujo = meta + QByteArray(1, '\x09') + entrada; break;
        case 7: {
            f::FilaManifiestoCruda otra = fila;
            otra.rutaUtf8 = "a/dos.zip";
            flujo = meta + entrada + f::registroManifiesto({otra});
            break;
        }
        case 8: {
            QByteArray e = entrada;
            e[1 + 1 + 2 + ruta1.size() + 7] = static_cast<char>(e.at(1 + 1 + 2 + ruta1.size() + 7) + 100);
            flujo = meta + e + f::registroManifiesto({fila});
            break;
        }
        default: flujo = meta + entrada + entrada + f::registroManifiesto({fila, fila}); break;
        }
        const QByteArray contenedor = crudo(flujo);
        QVERIFY(!contenedor.isEmpty());
        auto v = verificarBytes(contenedor, kekPw1);
        QVERIFY(!v);
        QVERIFY(v.error().categoria == C::Danado || v.error().categoria == C::RutaInvalida);
        esperarRechazo(contenedor, QStringLiteral("manifiesto"));
    }

    // --- C2: limites. ---
    void limiteDeEntradasEnEscritorYLector()
    {
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekPw1, kekRec, datosNuevos());
        QVERIFY(esc);
        auto& e = *esc.valor();
        for (quint32 i = 0; i < kMaxEntradas; ++i) {
            if (!e.anotarFaltante(QStringLiteral("p/%1.zip").arg(i))) {
                QFAIL("anotarFaltante dentro del limite");
            }
        }
        auto excedida = e.anotarFaltante(QStringLiteral("p/extra.zip"));
        QVERIFY(!excedida);
        QVERIFY(esCategoria(excedida.error(), C::LimiteExcedido));
        QVERIFY(!e.agregarDatos(QStringLiteral("p/extra2.zip"), "x"));
        auto inv = e.finalizar(); // el error de limite no deja el escritor fallido
        QVERIFY(inv);
        QCOMPARE(inv.valor().entradas(), kMaxEntradas);
        QVERIFY(verificarBytes(destino.data(), kekPw1));

        // Lector: un flujo con kMaxEntradas + 1 se rechaza tipado.
        QByteArray flujo = crypto::afb1::registroMetadatos({});
        for (quint32 i = 0; i <= kMaxEntradas; ++i) {
            flujo += crypto::afb1::registroFaltante(QByteArray("q/") + QByteArray::number(i));
        }
        auto v = verificarBytes(crudo(flujo), kekPw1);
        QVERIFY(!v);
        QVERIFY(esCategoria(v.error(), C::LimiteExcedido));
    }

    void limiteDeBytesDelManifiesto()
    {
        // Rutas de ~1000 bytes: el tope de 16 MiB se alcanza antes que el de
        // entradas.
        const QString prefijo = QString(250, QLatin1Char('a')) + QLatin1Char('/') + QString(250, QLatin1Char('b'))
            + QLatin1Char('/') + QString(250, QLatin1Char('c')) + QLatin1Char('/') + QString(230, QLatin1Char('d'));
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekPw1, kekRec, datosNuevos());
        QVERIFY(esc);
        auto& e = *esc.valor();
        std::optional<ErrorRespaldo> primerError;
        quint32 aceptadas = 0;
        for (quint32 i = 0; i < 20'000 && !primerError; ++i) {
            auto r = e.anotarFaltante(prefijo + QString::number(i));
            if (r) {
                ++aceptadas;
            } else {
                primerError = r.error();
            }
        }
        QVERIFY(primerError);
        QVERIFY(esCategoria(*primerError, C::LimiteExcedido));
        QCOMPARE(primerError->diagnostico, QStringLiteral("manifiesto.limite"));
        QVERIFY(aceptadas > 15'000 && aceptadas < kMaxEntradas);
        QVERIFY(e.finalizar());
        QVERIFY(verificarBytes(destino.data(), kekPw1));

        QByteArray flujo = crypto::afb1::registroMetadatos({});
        const QByteArray prefijoUtf8 = prefijo.toUtf8();
        for (quint32 i = 0; i <= aceptadas; ++i) {
            flujo += crypto::afb1::registroFaltante(prefijoUtf8 + QByteArray::number(i));
        }
        auto v = verificarBytes(crudo(flujo), kekPw1);
        QVERIFY(!v);
        QVERIFY(esCategoria(v.error(), C::LimiteExcedido));
    }

    void limiteDeCarpetas()
    {
        // Tres carpetas nuevas por entrada: el tope de kMaxCarpetas llega
        // antes que el de entradas.
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekPw1, kekRec, datosNuevos());
        QVERIFY(esc);
        auto& e = *esc.valor();
        std::optional<ErrorRespaldo> primerError;
        quint32 aceptadas = 0;
        for (quint32 i = 0; i < kMaxEntradas && !primerError; ++i) {
            auto r = e.anotarFaltante(QStringLiteral("a%1/b/c/f.zip").arg(i));
            if (r) {
                ++aceptadas;
            } else {
                primerError = r.error();
            }
        }
        QVERIFY(primerError);
        QVERIFY(esCategoria(*primerError, C::LimiteExcedido));
        QCOMPARE(primerError->diagnostico, QStringLiteral("carpetas.limite"));
        QCOMPARE(aceptadas, kMaxCarpetas / 3);
        QVERIFY(e.finalizar());
        QVERIFY(verificarBytes(destino.data(), kekPw1));

        QByteArray flujo = crypto::afb1::registroMetadatos({});
        for (quint32 i = 0; i <= aceptadas; ++i) {
            flujo += crypto::afb1::registroFaltante(QByteArray("a") + QByteArray::number(i) + "/b/c/f.zip");
        }
        auto v = verificarBytes(crudo(flujo), kekPw1);
        QVERIFY(!v);
        QVERIFY(esCategoria(v.error(), C::LimiteExcedido));
    }

    // --- C3: fuentes. ---
    void fuenteEnlazadaNuncaSeSigue()
    {
        QVERIFY(escribir(ruta(QStringLiteral("fuera/secreto.zip")), QByteArrayLiteral("<cfdi:XAXX010101000 enlace")));
        QVERIFY(QDir().mkpath(origen()));
        QVERIFY(QFile::link(ruta(QStringLiteral("fuera/secreto.zip")), ruta(QStringLiteral("origen/enlace.zip"))));
        QVERIFY(QDir().mkpath(ruta(QStringLiteral("origen/carpeta"))));
        QCOMPARE(::mkfifo(QFile::encodeName(ruta(QStringLiteral("origen/fifo"))).constData(), 0600), 0);
        QVERIFY(QFile::link(origen(), ruta(QStringLiteral("raiz-enlace"))));

        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekPw1, kekRec, datosNuevos());
        QVERIFY(esc);
        auto& e = *esc.valor();
        struct Caso {
            QString raiz;
            QString relativa;
            C categoria;
        };
        const QList<Caso> casos = {
            {origen(), QStringLiteral("enlace.zip"), C::FuenteInvalida},
            {origen(), QStringLiteral("carpeta"), C::FuenteInvalida},
            {origen(), QStringLiteral("fifo"), C::FuenteInvalida}, // no bloquea: O_NONBLOCK
            {ruta(QStringLiteral("raiz-enlace")), QStringLiteral("enlace.zip"), C::FuenteInvalida},
            {QStringLiteral("origen"), QStringLiteral("enlace.zip"), C::FuenteInvalida},
            {origen(), QStringLiteral("../fuera/secreto.zip"), C::FuenteInvalida},
            {origen(), QStringLiteral("no-existe.zip"), C::FuenteFaltante},
        };
        for (const Caso& c : casos) {
            auto r = e.agregarArchivo(QStringLiteral("p/x.zip"), c.raiz, c.relativa);
            QVERIFY2(!r, qPrintable(c.relativa));
            QVERIFY(esCategoria(r.error(), c.categoria));
            QVERIFY(diagnosticoLimpio(r.error()));
        }
        // Ninguno dejo el escritor fallido; el llamador anota el faltante (D11).
        QVERIFY(e.anotarFaltante(QStringLiteral("p/x.zip")));
        auto inv = e.finalizar();
        QVERIFY(inv);
        QCOMPARE(inv.valor().entradas(), 1u);
        QCOMPARE(inv.valor().faltantes, 1u);
        QVERIFY(verificarBytes(destino.data(), kekPw1));
    }

    void enlaceEnDirectorioAncestro()
    {
        QVERIFY(escribir(ruta(QStringLiteral("fuera/XAXX010101000/2026-01/PAQ.zip")), QByteArrayLiteral("<cfdi:fuera")));
        QVERIFY(QDir().mkpath(ruta(QStringLiteral("origen/real"))));
        QVERIFY(QFile::link(ruta(QStringLiteral("fuera/XAXX010101000")), ruta(QStringLiteral("origen/XAXX010101000"))));
        QVERIFY(QFile::link(ruta(QStringLiteral("fuera")), ruta(QStringLiteral("origen/real/anidado"))));

        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekPw1, kekRec, datosNuevos());
        QVERIFY(esc);
        for (const QString& rel : {QStringLiteral("XAXX010101000/2026-01/PAQ.zip"),
                                   QStringLiteral("real/anidado/XAXX010101000/2026-01/PAQ.zip")}) {
            auto r = esc.valor()->agregarArchivo(QStringLiteral("paquetes/PAQ.zip"), origen(), rel);
            QVERIFY(!r);
            QVERIFY(esCategoria(r.error(), C::FuenteInvalida));
            QCOMPARE(r.error().diagnostico, QStringLiteral("fuente.enlace"));
        }
        QVERIFY(esc.valor()->finalizar());
        QVERIFY(!destino.data().contains("fuera"));
    }

    void fuenteQueCambiaDuranteLaLecturaFalla()
    {
        const QString fuente = ruta(QStringLiteral("origen/crece.zip"));
        QVERIFY(escribir(fuente, relleno(3 * 1024 * 1024, "<cfdi:XAXX010101000 ")));
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        bool modificado = false;
        auto esc = cr.crearEscritor(destino, kekPw1, kekRec, datosNuevos(), [&](quint64) {
            if (!modificado) {
                modificado = true;
                QFile f(fuente);
                if (f.open(QIODevice::Append)) {
                    f.write("mas");
                }
            }
        });
        QVERIFY(esc);
        auto a = esc.valor()->agregarArchivo(QStringLiteral("p/crece.zip"), origen(), QStringLiteral("crece.zip"));
        QVERIFY(!a);
        QVERIFY(esCategoria(a.error(), C::FuenteCambio));
        auto fin = esc.valor()->finalizar();
        QVERIFY(!fin);
        QCOMPARE(fin.error(), a.error());
        QVERIFY(!esc.valor()->agregarDatos(QStringLiteral("x"), "y"));
    }

    void fuenteBorradaDuranteLaLecturaFalla()
    {
        const QString fuente = ruta(QStringLiteral("origen/borrada.zip"));
        QVERIFY(escribir(fuente, relleno(3 * 1024 * 1024, "<cfdi:XAXX010101000 ")));
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekPw1, kekRec, datosNuevos(), [&](quint64) { QFile::remove(fuente); });
        QVERIFY(esc);
        auto a = esc.valor()->agregarArchivo(QStringLiteral("p/borrada.zip"), origen(), QStringLiteral("borrada.zip"));
        QVERIFY(!a);
        QVERIFY(esCategoria(a.error(), C::FuenteCambio));
    }

    // --- C4: staging privado. ---
    void extraerExigeStagingPrivado()
    {
        const QByteArray contenedor = respaldoCompleto(kekPw1, 4096);
        QVERIFY(escribir(staging(QStringLiteral("lleno")) + QStringLiteral("/existente.txt"), "x"));
        const QString real = staging(QStringLiteral("real"));
        QVERIFY(QFile::link(real, ruta(QStringLiteral("enlace"))));
        const QString abierto = ruta(QStringLiteral("abierto"));
        QVERIFY(QDir().mkpath(abierto));
        QVERIFY(QFile::setPermissions(abierto, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                                   | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther
                                                   | QFile::ExeOther));
        struct Caso {
            QString dir;
            const char* diagnostico;
        };
        for (const Caso& c : {Caso{ruta(QStringLiteral("lleno")), "destino.no_vacio"},
                              Caso{ruta(QStringLiteral("enlace")), "destino.abrir"},
                              Caso{QStringLiteral("relativo"), "destino.relativo"},
                              Caso{ruta(QStringLiteral("no-existe")), "destino.abrir"},
                              Caso{abierto, "destino.permisos"}}) {
            auto x = extraerBytes(contenedor, kekPw1, c.dir);
            QVERIFY(!x);
            QVERIFY(esCategoria(x.error(), C::DestinoInvalido));
            QCOMPARE(x.error().diagnostico, QString::fromLatin1(c.diagnostico));
        }
        QCOMPARE(leer(ruta(QStringLiteral("lleno/existente.txt"))), QByteArrayLiteral("x"));
        QVERIFY(stagingVacio(real));
        QVERIFY(stagingVacio(abierto));
        QVERIFY(extraerBytes(contenedor, kekPw1, real));
        // Dueno distinto de euid: no se puede fabricar sin privilegios; el
        // chequeo (st_uid != geteuid) queda cubierto por revision de codigo.
    }

    void fallaTardiaVaciaElStaging()
    {
        // Alteracion del ultimo bloque (manifiesto): las entradas ya se
        // escribieron cuando se detecta; el staging debe quedar vacio.
        QByteArray contenedor = respaldoCompleto(kekPw1);
        contenedor[contenedor.size() - 20] = static_cast<char>(contenedor.at(contenedor.size() - 20) ^ 0x01);
        const QString destino = staging(QStringLiteral("tardio"));
        auto x = extraerBytes(contenedor, kekPw1, destino);
        QVERIFY(!x);
        QVERIFY(stagingVacio(destino));
    }

    void noEsAfb1OVersionDesconocida()
    {
        QBuffer otro;
        otro.setData(QByteArray(4096, 'Z'));
        otro.open(QIODevice::ReadOnly);
        auto a = cr.leerCabecera(otro);
        QVERIFY(!a);
        QVERIFY(esCategoria(a.error(), C::FormatoNoReconocido));

        const QByteArray contenedor = respaldoCompleto(kekPw1, 4096);
        QByteArray v2 = contenedor;
        v2[4] = 2;
        QBuffer b;
        b.setData(v2);
        b.open(QIODevice::ReadOnly);
        auto v = cr.leerCabecera(b);
        QVERIFY(!v);
        QVERIFY(esCategoria(v.error(), C::VersionNoSoportada));

        QByteArray debil = contenedor;
        ponerU32(debil, 48, 8 * 1024); // 8 MiB
        QBuffer c;
        c.setData(debil);
        c.open(QIODevice::ReadOnly);
        auto k = cr.leerCabecera(c);
        QVERIFY(!k);
        QVERIFY(esCategoria(k.error(), C::ParametrosKdfInvalidos));
    }

    void cancelacion()
    {
        const QByteArray contenedor = respaldoCompleto(kekPw1);
        QBuffer o;
        o.setData(contenedor);
        o.open(QIODevice::ReadOnly);
        int llamadas = 0;
        OpcionesLectura op;
        op.cancelacion = Cancelacion([&] { return ++llamadas > 1; });
        auto v = cr.verificar(o, kekPw1, op);
        QVERIFY(!v);
        QVERIFY(esCategoria(v.error(), C::Cancelado));

        crearFuentes();
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekPw1, kekRec, datosNuevos());
        QVERIFY(esc);
        auto a = esc.valor()->agregarArchivo(kRutaZip, origen(), QStringLiteral("XAXX010101000/2026-01/PAQ_01.zip"),
                                             Cancelacion([] { return true; }));
        QVERIFY(!a);
        QVERIFY(esCategoria(a.error(), C::Cancelado));
        QVERIFY(!esc.valor()->finalizar());
    }

    void escritorFinalizadoNoAceptaMas()
    {
        QBuffer destino;
        destino.open(QIODevice::WriteOnly);
        auto esc = cr.crearEscritor(destino, kekPw1, kekRec, datosNuevos({}));
        QVERIFY(esc);
        QVERIFY(esc.valor()->finalizar());
        QVERIFY(!esc.valor()->finalizar());
        QVERIFY(!esc.valor()->agregarDatos(QStringLiteral("x"), "y"));
        auto v = verificarBytes(destino.data(), kekRec);
        QVERIFY(v);
        QCOMPARE(v.valor().entradas(), 0u);
    }

    // --- Memoria (criterio 4; omitibles). ---
    void memoriaAcotadaCon3GB()
    {
        if (qEnvironmentVariableIntValue("SATCFDI_RESPALDO_PRUEBA_3GB") != 1) {
            QSKIP("Define SATCFDI_RESPALDO_PRUEBA_3GB=1 para correr la prueba de 3 GB (requiere ~3 GB libres).");
        }
#if !defined(__APPLE__)
        QSKIP("Medicion de memoria solo implementada en macOS.");
#else
        constexpr qint64 kTres = 3LL * 1024 * 1024 * 1024;
        constexpr quint64 kLimite = 32ULL * 1024 * 1024;
        {
            QVERIFY(QDir().mkpath(origen()));
            QFile f(ruta(QStringLiteral("origen/grande.zip")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QVERIFY(f.resize(kTres)); // disperso: no ocupa disco
        }

        const quint64 enUsoAntesKdf = enUso();
        KekRespaldo kek = derivar(kContrasena1, sal1);
        QVERIFY(kek.valida());
        const Huella trasKdf = huella();
        qInfo("KDF Argon2id: pico estimado %llu MiB (huella antes del primer KDF %llu MiB, pico de por vida %llu MiB); "
              "en uso antes/despues %llu/%llu KiB",
              (trasKdf.pico - std::min(trasKdf.pico, huellaAntesDelPrimerKdf)) >> 20, huellaAntesDelPrimerKdf >> 20,
              trasKdf.pico >> 20, enUsoAntesKdf >> 10, enUso() >> 10);

        const QString archivo = ruta(QStringLiteral("grande.afbak.partial"));
        QFile salida(archivo);
        QVERIFY(salida.open(QIODevice::WriteOnly));
        const quint64 base = enUso();
        quint64 maxEscritura = base;
        auto esc = cr.crearEscritor(salida, kek, kekRec, datosNuevos(), [&](quint64 n) {
            if ((n & ((64u << 20) - 1)) < (256u << 10)) {
                maxEscritura = std::max(maxEscritura, enUso());
            }
        });
        QVERIFY(esc);
        auto escritor = std::move(esc).valor();
        QVERIFY(escritor->agregarArchivo(QStringLiteral("paquetes/grande.zip"), origen(), QStringLiteral("grande.zip")));
        QVERIFY(escritor->finalizar());
        maxEscritura = std::max(maxEscritura, enUso());
        escritor.reset();
        salida.close();
        QVERIFY(QFileInfo(archivo).size() > kTres);

        QFile entrada(archivo);
        QVERIFY(entrada.open(QIODevice::ReadOnly));
        const quint64 baseLectura = enUso();
        quint64 maxLectura = baseLectura;
        OpcionesLectura op;
        op.progreso = [&](quint64 n) {
            if ((n & ((64u << 20) - 1)) < (256u << 10)) {
                maxLectura = std::max(maxLectura, enUso());
            }
        };
        auto v = cr.verificar(entrada, kek, op);
        QVERIFY2(v, v ? "" : qPrintable(v.error().diagnostico));
        QCOMPARE(v.valor().bytesDatos, quint64(kTres));
        const quint64 extraEscritura = maxEscritura - base;
        const quint64 extraLectura = maxLectura - baseLectura;
        qInfo("Pipeline 3 GiB (malloc en uso): adicional escritura %llu KiB, lectura %llu KiB (limite %llu KiB)",
              extraEscritura >> 10, extraLectura >> 10, kLimite >> 10);
        QVERIFY(extraEscritura <= kLimite);
        QVERIFY(extraLectura <= kLimite);
#endif
    }

    void memoriaAcotadaCon100kEntradas()
    {
        if (qEnvironmentVariableIntValue("SATCFDI_RESPALDO_PRUEBA_100K") != 1) {
            QSKIP("Define SATCFDI_RESPALDO_PRUEBA_100K=1 para correr la prueba de 100 000 entradas.");
        }
#if !defined(__APPLE__)
        QSKIP("Medicion de memoria solo implementada en macOS.");
#else
        constexpr quint64 kLimite = 32ULL * 1024 * 1024;
        const QByteArray datos = relleno(64, "<cfdi:XAXX010101000 ");
        const QString archivo = ruta(QStringLiteral("muchas.afbak.partial"));
        QFile salida(archivo);
        QVERIFY(salida.open(QIODevice::WriteOnly));
        const quint64 base = enUso();
        quint64 maxEscritura = base;
        {
            auto esc = cr.crearEscritor(salida, kekPw1, kekRec, datosNuevos());
            QVERIFY(esc);
            auto& e = *esc.valor();
            for (quint32 i = 0; i < kMaxEntradas; ++i) {
                // Ruta realista: paquetes/<RFC>/<yyyy-mm>/<UUID>/<id>.zip (~95 B).
                const QString r = QStringLiteral("paquetes/XAXX010101000/2026-%1/%2/PAQ_%3.zip")
                                      .arg(1 + i % 12, 2, 10, QLatin1Char('0'))
                                      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
                                      .arg(i);
                if (!e.agregarDatos(r, datos)) {
                    QFAIL("agregarDatos");
                }
                if (i % 10000 == 0) {
                    maxEscritura = std::max(maxEscritura, enUso());
                }
            }
            maxEscritura = std::max(maxEscritura, enUso());
            QVERIFY(e.finalizar());
            maxEscritura = std::max(maxEscritura, enUso());
        }
        salida.close();

        QFile entrada(archivo);
        QVERIFY(entrada.open(QIODevice::ReadOnly));
        const quint64 baseLectura = enUso();
        quint64 maxLectura = baseLectura;
        quint32 vistas = 0;
        OpcionesLectura op;
        op.alLeerEntrada = [&](const EntradaManifiesto&) {
            if (++vistas % 10000 == 0) {
                maxLectura = std::max(maxLectura, enUso());
            }
        };
        auto v = cr.verificar(entrada, kekPw1, op);
        QVERIFY2(v, v ? "" : qPrintable(v.error().diagnostico));
        QCOMPARE(v.valor().entradas(), kMaxEntradas);
        QCOMPARE(vistas, kMaxEntradas);
        const quint64 extraEscritura = maxEscritura - base;
        const quint64 extraLectura = maxLectura - baseLectura;
        qInfo("100 000 entradas (malloc en uso): adicional escritura %llu KiB, lectura %llu KiB (limite %llu KiB); "
              "contenedor %lld KiB",
              extraEscritura >> 10, extraLectura >> 10, kLimite >> 10, QFileInfo(archivo).size() >> 10);
        QVERIFY(extraEscritura <= kLimite);
        QVERIFY(extraLectura <= kLimite);
#endif
    }
};

QTEST_GUILESS_MAIN(TestContenedorRespaldo)
#include "TestContenedorRespaldo.moc"
