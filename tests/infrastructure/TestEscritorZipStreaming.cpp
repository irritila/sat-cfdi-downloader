// T026.1 (D12, C2): EscritorZipStreaming. Formato binario inspeccionado byte a
// byte, lectura con libzip (ZIP_CHECKCONS) y LectorZipLibzip con SHA-256 de
// cada payload, `unzip -t` / `zipinfo -v` si existen, ZIP64 (65,536 entradas
// y umbral reducido), cancelacion y fallos de escritura en datos y en el
// directorio central sin ZIP publicado, y publicacion completa con
// DestinoExportacionPosix real. Datos sinteticos.

#include "infrastructure/cfdi/zip/EscritorZipStreaming.h"
#include "infrastructure/cfdi/zip/LectorZipLibzip.h"
#include "infrastructure/storage/DestinoExportacionPosix.h"

#include "fakes/FakeDestinoExportacion.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <zip.h>
#include <zlib.h>

#include <sys/stat.h>

#include <cerrno>
#include <functional>
#include <memory>

using namespace satcfdi;
using exportacion::ErrorZipExportacion;
using CZ = ErrorZipExportacion::Codigo;
using Fase = DestinoExportacionPosix::Fase;

namespace {

QMutex g_mutexLog;
QStringList g_log;
QtMessageHandler g_anterior = nullptr;

void capturarLog(QtMsgType tipo, const QMessageLogContext& ctx, const QString& msg)
{
    {
        QMutexLocker l(&g_mutexLog);
        g_log.append(msg);
    }
    if (g_anterior) {
        g_anterior(tipo, ctx, msg);
    }
}

struct Entrada {
    QString nombre;
    QByteArray datos;
};

QByteArray sha(QByteArrayView b)
{
    return QCryptographicHash::hash(b, QCryptographicHash::Sha256);
}

QByteArray aleatorio(qsizetype n, quint32 semilla)
{
    QRandomGenerator g(semilla);
    QByteArray b(n, Qt::Uninitialized);
    for (qsizetype i = 0; i < n; ++i) {
        b[i] = static_cast<char>(g.bounded(256));
    }
    return b;
}

QByteArray xmlSintetico(int i)
{
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<cfdi:Comprobante Version=\"4.0\" "
                          "xmlns:cfdi=\"http://www.sat.gob.mx/cfd/4\"><cfdi:Emisor Rfc=\"XAXX010101000\"/>"
                          "<cfdi:Receptor Rfc=\"XEXX010101000\"/><n>%1</n></cfdi:Comprobante>\n")
        .arg(i)
        .toUtf8()
        .repeated(1 + i % 7);
}

QString uuidSintetico(int i)
{
    return QStringLiteral("00000000-0000-4000-8000-%1").arg(i, 12, 10, QLatin1Char('0'));
}

QDateTime fechaExportacion()
{
    // Segundos pares en hora local: la fecha DOS es exacta.
    return QDateTime(QDate(2026, 10, 24), QTime(13, 45, 30)).toUTC();
}

quint16 u16(const QByteArray& b, qsizetype o)
{
    return static_cast<quint16>(static_cast<quint8>(b[o]) | (static_cast<quint8>(b[o + 1]) << 8));
}
quint32 u32(const QByteArray& b, qsizetype o)
{
    return static_cast<quint32>(u16(b, o)) | (static_cast<quint32>(u16(b, o + 2)) << 16);
}
quint64 u64(const QByteArray& b, qsizetype o)
{
    return static_cast<quint64>(u32(b, o)) | (static_cast<quint64>(u32(b, o + 4)) << 32);
}

QByteArray inflarRaw(const QByteArray& comprimido)
{
    z_stream zs{};
    if (::inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        return {};
    }
    QByteArray salida;
    QByteArray buf(65536, Qt::Uninitialized);
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(comprimido.constData()));
    zs.avail_in = static_cast<uInt>(comprimido.size());
    int ret = Z_OK;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf.data());
        zs.avail_out = static_cast<uInt>(buf.size());
        ret = ::inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            ::inflateEnd(&zs);
            return QByteArray("ERROR");
        }
        salida.append(buf.constData(), buf.size() - static_cast<qsizetype>(zs.avail_out));
    } while (ret != Z_STREAM_END);
    const bool sobra = zs.avail_in != 0;
    ::inflateEnd(&zs);
    return sobra ? QByteArray("SOBRA") : salida;
}

// Directorio central parseado.
struct Central {
    QByteArray nombre;
    quint16 hecho = 0, necesita = 0, flags = 0, metodo = 0, hora = 0, fecha = 0;
    quint32 crc = 0;
    quint64 comprimido = 0, descomprimido = 0, desplazamiento = 0;
    quint16 extra = 0, comentario = 0, disco = 0, internos = 0;
    quint32 externos = 0;
};

struct Zip {
    QList<Central> entradas;
    bool zip64 = false;
    quint64 inicioCentral = 0;
    quint64 tamanoCentral = 0;
};

// Inspeccion estricta: devuelve un texto de error vacio si todo cuadra.
QString inspeccionar(const QByteArray& b, const QList<Entrada>& esperadas, Zip& zip, quint16 horaDos,
                     quint16 fechaDos)
{
    const qsizetype n = b.size();
    if (n < 22 || u32(b, n - 22) != 0x06054b50) {
        return QStringLiteral("eocd ausente o con comentario");
    }
    const qsizetype eocd = n - 22;
    if (u16(b, eocd + 4) != 0 || u16(b, eocd + 6) != 0 || u16(b, eocd + 20) != 0) {
        return QStringLiteral("eocd disco/comentario");
    }
    quint64 total = u16(b, eocd + 10);
    zip.tamanoCentral = u32(b, eocd + 12);
    zip.inicioCentral = u32(b, eocd + 16);
    if (u16(b, eocd + 8) != u16(b, eocd + 10)) {
        return QStringLiteral("eocd conteos");
    }
    qsizetype finCentral = eocd;
    if (eocd >= 20 && u32(b, eocd - 20) == 0x07064b50) {
        zip.zip64 = true;
        const qsizetype loc = eocd - 20;
        const auto e64 = static_cast<qsizetype>(u64(b, loc + 8));
        if (u32(b, loc + 4) != 0 || u32(b, loc + 16) != 1 || e64 != loc - 56 || u32(b, e64) != 0x06064b50
            || u64(b, e64 + 4) != 44 || u16(b, e64 + 14) != 45 || u64(b, e64 + 24) != u64(b, e64 + 32)) {
            return QStringLiteral("eocd64/localizador");
        }
        const quint64 t64 = u64(b, e64 + 32);
        if ((total != 0xFFFF && total != t64) || (zip.tamanoCentral != 0xFFFFFFFF && zip.tamanoCentral != u64(b, e64 + 40))
            || (zip.inicioCentral != 0xFFFFFFFF && zip.inicioCentral != u64(b, e64 + 48))) {
            return QStringLiteral("eocd inconsistente con eocd64");
        }
        total = t64;
        zip.tamanoCentral = u64(b, e64 + 40);
        zip.inicioCentral = u64(b, e64 + 48);
        finCentral = e64;
    }
    if (static_cast<qsizetype>(zip.inicioCentral + zip.tamanoCentral) != finCentral) {
        return QStringLiteral("directorio central no contiguo al fin");
    }
    if (total != static_cast<quint64>(esperadas.size())) {
        return QStringLiteral("total de entradas");
    }
    auto o = static_cast<qsizetype>(zip.inicioCentral);
    quint64 siguienteLocal = 0;
    for (int i = 0; i < esperadas.size(); ++i) {
        if (u32(b, o) != 0x02014b50) {
            return QStringLiteral("firma central %1").arg(i);
        }
        Central c;
        c.hecho = u16(b, o + 4);
        c.necesita = u16(b, o + 6);
        c.flags = u16(b, o + 8);
        c.metodo = u16(b, o + 10);
        c.hora = u16(b, o + 12);
        c.fecha = u16(b, o + 14);
        c.crc = u32(b, o + 16);
        c.comprimido = u32(b, o + 20);
        c.descomprimido = u32(b, o + 24);
        const quint16 ln = u16(b, o + 28);
        c.extra = u16(b, o + 30);
        c.comentario = u16(b, o + 32);
        c.disco = u16(b, o + 34);
        c.internos = u16(b, o + 36);
        c.externos = u32(b, o + 38);
        c.desplazamiento = u32(b, o + 42);
        c.nombre = b.mid(o + 46, ln);
        // Extra: solo ZIP64 (0x0001) con los campos desbordados.
        qsizetype x = o + 46 + ln;
        if (c.extra > 0) {
            if (u16(b, x) != 0x0001 || u16(b, x + 2) != c.extra - 4) {
                return QStringLiteral("extra central no zip64 %1").arg(i);
            }
            qsizetype k = x + 4;
            int usados = 0;
            if (c.descomprimido == 0xFFFFFFFF) {
                c.descomprimido = u64(b, k), k += 8, ++usados;
            }
            if (c.comprimido == 0xFFFFFFFF) {
                c.comprimido = u64(b, k), k += 8, ++usados;
            }
            if (c.desplazamiento == 0xFFFFFFFF) {
                c.desplazamiento = u64(b, k), k += 8, ++usados;
            }
            if (4 + 8 * usados != c.extra || c.necesita != 45) {
                return QStringLiteral("campos zip64 central %1").arg(i);
            }
        }
        const Entrada& e = esperadas.at(i);
        if (c.nombre != e.nombre.toLatin1() || c.hecho != ((3 << 8) | c.necesita) || (c.necesita != 20 && c.necesita != 45)
            || c.flags != 0x0008 || c.metodo != 8 || c.hora != horaDos || c.fecha != fechaDos || c.comentario != 0
            || c.disco != 0 || c.internos != 0 || c.externos != 0x81800000U
            || c.descomprimido != static_cast<quint64>(e.datos.size())
            || c.crc != static_cast<quint32>(::crc32_z(0, reinterpret_cast<const Bytef*>(e.datos.constData()),
                                                       static_cast<z_size_t>(e.datos.size())))
            || c.desplazamiento != siguienteLocal) {
            return QStringLiteral("central %1").arg(i);
        }
        // Encabezado local.
        const auto l = static_cast<qsizetype>(c.desplazamiento);
        const quint16 lext = u16(b, l + 28);
        const bool local64 = lext == 20;
        if (u32(b, l) != 0x04034b50 || u16(b, l + 4) != c.necesita || u16(b, l + 6) != 0x0008 || u16(b, l + 8) != 8
            || u16(b, l + 10) != horaDos || u16(b, l + 12) != fechaDos || u32(b, l + 14) != 0
            || u16(b, l + 26) != ln || b.mid(l + 30, ln) != c.nombre || (lext != 0 && lext != 20)) {
            return QStringLiteral("local %1").arg(i);
        }
        if (local64
            && (u32(b, l + 18) != 0xFFFFFFFF || u32(b, l + 22) != 0xFFFFFFFF || u16(b, l + 30 + ln) != 0x0001
                || u16(b, l + 32 + ln) != 16 || u64(b, l + 34 + ln) != 0 || u64(b, l + 42 + ln) != 0)) {
            return QStringLiteral("local zip64 %1").arg(i);
        }
        if (!local64 && (u32(b, l + 18) != 0 || u32(b, l + 22) != 0)) {
            return QStringLiteral("local tamanos %1").arg(i);
        }
        const qsizetype datos = l + 30 + ln + lext;
        const QByteArray comprimido = b.mid(datos, static_cast<qsizetype>(c.comprimido));
        if (inflarRaw(comprimido) != e.datos) {
            return QStringLiteral("datos %1").arg(i);
        }
        // Descriptor de datos.
        const qsizetype dd = datos + static_cast<qsizetype>(c.comprimido);
        qsizetype finDd = 0;
        if (u32(b, dd) != 0x08074b50 || u32(b, dd + 4) != c.crc) {
            return QStringLiteral("descriptor %1").arg(i);
        }
        if (local64) {
            if (u64(b, dd + 8) != c.comprimido || u64(b, dd + 16) != c.descomprimido) {
                return QStringLiteral("descriptor64 %1").arg(i);
            }
            finDd = dd + 24;
        } else {
            if (u32(b, dd + 8) != c.comprimido || u32(b, dd + 12) != c.descomprimido) {
                return QStringLiteral("descriptor32 %1").arg(i);
            }
            finDd = dd + 16;
        }
        siguienteLocal = static_cast<quint64>(finDd);
        zip.entradas.append(c);
        o += 46 + ln + c.extra;
    }
    if (siguienteLocal != zip.inicioCentral) {
        return QStringLiteral("bytes entre datos y directorio central");
    }
    return {};
}

// Escribe el ZIP en un FakeDestinoExportacion y devuelve sus bytes publicados.
QByteArray zipEnMemoria(const QList<Entrada>& entradas, EscritorZipStreaming::Opciones o = {},
                        fakes::FakeDestinoExportacion* destino = nullptr)
{
    fakes::FakeDestinoExportacion propio;
    fakes::FakeDestinoExportacion& f = destino ? *destino : propio;
    const QString carpeta = QStringLiteral("/Users/usuario/Exportaciones");
    auto r = f.reservar(carpeta, QStringLiteral("x.zip"));
    if (!r) {
        return {};
    }
    auto p = std::move(r).valor();
    EscritorZipStreaming w(o);
    if (!w.abrir(*p, fechaExportacion())) {
        return {};
    }
    for (const Entrada& e : entradas) {
        if (!w.agregar(e.nombre, e.datos)) {
            return {};
        }
    }
    if (!w.cerrar() || !p->publicar()) {
        return {};
    }
    return f.archivo(carpeta, QStringLiteral("x.zip")).value_or(QByteArray());
}

QList<Entrada> entradasMixtas()
{
    QList<Entrada> r;
    for (int i = 0; i < 3; ++i) {
        r.append({QStringLiteral("emitidas/2026-0%1/%2.xml").arg(i + 1).arg(uuidSintetico(i)), xmlSintetico(i)});
    }
    r.append({QStringLiteral("recibidas/2026-01/%1.pdf").arg(uuidSintetico(9)), aleatorio(300 * 1024, 7)});
    r.append({QStringLiteral("recibidas/2026-01/sin-uuid-1.xml"), QByteArray()});
    r.append({QStringLiteral("omitidas.txt"), QByteArray("ya no disponible\n")});
    return r;
}

struct LibzipCerrar {
    zip_t* z;
    ~LibzipCerrar()
    {
        if (z) {
            zip_discard(z);
        }
    }
};

// Lee con libzip (ZIP_CHECKCONS: compara encabezados locales y centrales).
QString verificarConLibzip(const QString& ruta, const QList<Entrada>& esperadas, qint64 mtimeEsperado)
{
    int err = 0;
    zip_t* z = zip_open(QFile::encodeName(ruta).constData(), ZIP_RDONLY | ZIP_CHECKCONS, &err);
    if (!z) {
        return QStringLiteral("zip_open %1").arg(err);
    }
    LibzipCerrar cierre{z};
    if (zip_get_num_entries(z, 0) != esperadas.size()) {
        return QStringLiteral("num entradas");
    }
    int largo = -1;
    const char* comentario = zip_get_archive_comment(z, &largo, 0);
    if (comentario != nullptr && largo != 0) {
        return QStringLiteral("comentario del archivo");
    }
    for (int i = 0; i < esperadas.size(); ++i) {
        const auto idx = static_cast<zip_uint64_t>(i);
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(z, idx, 0, &st) != 0 || QString::fromLatin1(st.name) != esperadas.at(i).nombre
            || st.comp_method != ZIP_CM_DEFLATE || st.size != static_cast<zip_uint64_t>(esperadas.at(i).datos.size())
            || static_cast<qint64>(st.mtime) != mtimeEsperado || st.encryption_method != ZIP_EM_NONE) {
            return QStringLiteral("stat %1").arg(i);
        }
        zip_uint8_t opsys = 0;
        zip_uint32_t attr = 0;
        if (zip_file_get_external_attributes(z, idx, 0, &opsys, &attr) != 0 || opsys != ZIP_OPSYS_UNIX
            || attr != 0x81800000U) {
            return QStringLiteral("atributos %1").arg(i);
        }
        zip_uint32_t lc = 0;
        if (zip_file_get_comment(z, idx, &lc, 0) != nullptr && lc != 0) {
            return QStringLiteral("comentario %1").arg(i);
        }
        if (zip_file_extra_fields_count(z, idx, ZIP_FL_CENTRAL) != 0
            || zip_file_extra_fields_count(z, idx, ZIP_FL_LOCAL) != 0) {
            return QStringLiteral("extras %1").arg(i);
        }
        zip_file_t* f = zip_fopen_index(z, idx, 0);
        if (!f) {
            return QStringLiteral("fopen %1").arg(i);
        }
        QByteArray datos;
        char buf[65536];
        zip_int64_t n = 0;
        while ((n = zip_fread(f, buf, sizeof(buf))) > 0) {
            datos.append(buf, static_cast<qsizetype>(n));
        }
        zip_fclose(f);
        if (n < 0 || sha(datos) != sha(esperadas.at(i).datos)) {
            return QStringLiteral("sha %1").arg(i);
        }
    }
    return {};
}

QString herramienta(const QString& nombre)
{
    return QStandardPaths::findExecutable(nombre);
}

struct Salida {
    int codigo = -1;
    QString texto;
};

Salida ejecutar(const QString& programa, const QStringList& argumentos)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    // Salida comparable sin depender del idioma del sistema.
    QProcessEnvironment entorno = QProcessEnvironment::systemEnvironment();
    entorno.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    entorno.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    p.setProcessEnvironment(entorno);
    p.start(programa, argumentos);
    if (!p.waitForFinished(120000)) {
        p.kill();
        return {};
    }
    return {p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1, QString::fromLocal8Bit(p.readAll())};
}

QStringList entradasDir(const QString& carpeta)
{
    return QDir(carpeta).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
}

// Publicacion completa con el destino POSIX real.
struct Publicacion {
    QTemporaryDir tmp;
    QString datos;
    QString carpeta;
    std::unique_ptr<DestinoExportacionPosix> destino;

    bool preparar(std::function<int(Fase)> falla = {})
    {
        datos = tmp.filePath(QStringLiteral("datos"));
        carpeta = tmp.filePath(QStringLiteral("Exportaciones"));
        if (!tmp.isValid() || !QDir().mkpath(datos) || !QDir().mkpath(carpeta)) {
            return false;
        }
        DestinoExportacionPosix::Opciones o;
        o.directorioDatos = datos;
        o.inyectarFalla = std::move(falla);
        destino = std::make_unique<DestinoExportacionPosix>(std::move(o));
        return true;
    }
    QString ruta(const QString& nombre) const { return QDir(carpeta).filePath(nombre); }
    bool vacio() const
    {
        return entradasDir(carpeta).isEmpty() && entradasDir(DestinoExportacionPosix::carpetaParciales(datos)).isEmpty();
    }
};

} // namespace

class TestEscritorZipStreaming : public QObject {
    Q_OBJECT

private:
    quint16 m_horaDos = 0;
    quint16 m_fechaDos = 0;

private slots:
    void initTestCase()
    {
        QLoggingCategory::setFilterRules(QStringLiteral("satcfdi.*=true"));
        g_anterior = qInstallMessageHandler(capturarLog);
        const auto [h, f] = EscritorZipStreaming::fechaHoraDos(fechaExportacion());
        m_horaDos = h;
        m_fechaDos = f;
    }
    void cleanupTestCase() { qInstallMessageHandler(g_anterior); }

    void fechaDos()
    {
        const QDateTime impar = QDateTime(QDate(2026, 10, 24), QTime(13, 45, 31)).toUTC();
        const auto [h, f] = EscritorZipStreaming::fechaHoraDos(impar);
        QCOMPARE(h, static_cast<quint16>((13 << 11) | (45 << 5) | 15)); // 31 s -> 30 s (granularidad 2 s)
        QCOMPARE(f, static_cast<quint16>((46 << 9) | (10 << 5) | 24));
        QCOMPARE(EscritorZipStreaming::fechaHoraDos(fechaExportacion()), std::make_pair(h, f));
        const auto antes = EscritorZipStreaming::fechaHoraDos(QDateTime(QDate(1970, 1, 2), QTime(0, 0), QTimeZone::UTC));
        QCOMPARE(antes, std::make_pair(quint16{0}, static_cast<quint16>((1 << 5) | 1)));
        const auto despues = EscritorZipStreaming::fechaHoraDos(QDateTime(QDate(2200, 1, 1), QTime(0, 0), QTimeZone::UTC));
        QCOMPARE(despues.second, static_cast<quint16>((127 << 9) | (12 << 5) | 31));
    }

    void nombresDeEntrada()
    {
        for (const QString& valido : {QStringLiteral("emitidas/2026-01/%1.xml").arg(uuidSintetico(1)),
                                      QStringLiteral("omitidas.txt"), QStringLiteral("aZ09/c-d_e.f")}) {
            QVERIFY2(EscritorZipStreaming::esNombreEntradaValido(valido), qPrintable(valido));
        }
        for (const QString& invalido :
             {QString(), QStringLiteral("/abs.xml"), QStringLiteral("a/../b.xml"), QStringLiteral("../b.xml"),
              QStringLiteral("a/./b.xml"), QStringLiteral("a//b.xml"), QStringLiteral("a/"), QStringLiteral("a\\b.xml"),
              QStringLiteral("con\x01" "control.xml"), QStringLiteral("tab\t.xml"), QStringLiteral("ñ.xml"),
              QStringLiteral("._x.xml"), QStringLiteral("__MACOSX/x.xml"), QString(1025, QLatin1Char('a')),
              QStringLiteral("con espacio.xml"), QStringLiteral("c:x.xml"), QStringLiteral("a*.xml"),
              QStringLiteral("a?.xml"), QStringLiteral("a<b>.xml"), QStringLiteral("a|b.xml"),
              QStringLiteral("a\"b.xml"), QStringLiteral("a'b.xml"), QStringLiteral("a$b.xml")}) {
            QVERIFY2(!EscritorZipStreaming::esNombreEntradaValido(invalido), qPrintable(invalido));
        }
    }

    void estadosYErroresDeUso()
    {
        fakes::FakeDestinoExportacion f;
        auto p = std::move(f.reservar(QStringLiteral("/c"), QStringLiteral("x.zip"))).valor();
        EscritorZipStreaming w;
        QCOMPARE(w.agregar(QStringLiteral("a.xml"), "x").error().codigo, CZ::Estado);
        QCOMPARE(w.cerrar().error().codigo, CZ::Estado);
        QVERIFY(w.abrir(*p, fechaExportacion()));
        QCOMPARE(w.abrir(*p, fechaExportacion()).error().codigo, CZ::Estado);
        QCOMPARE(w.agregar(QStringLiteral("../a.xml"), "x").error().codigo, CZ::NombreInvalido);
        QVERIFY(w.agregar(QStringLiteral("a.xml"), "x")); // NombreInvalido no consume el escritor
        QCOMPARE(w.agregar(QStringLiteral("a.xml"), "y").error().codigo, CZ::NombreDuplicado);
        QVERIFY(w.cerrar());
        QCOMPARE(w.agregar(QStringLiteral("b.xml"), "x").error().codigo, CZ::Estado);

        // abrir() exige un parcial vacio en la posicion 0.
        auto q = std::move(f.reservar(QStringLiteral("/c"), QStringLiteral("y.zip"))).valor();
        QCOMPARE(q->dispositivo().write("basura"), 6);
        EscritorZipStreaming w2;
        QCOMPARE(w2.abrir(*q, fechaExportacion()).error().codigo, CZ::Estado);

        EscritorZipStreaming w3;
        auto r = std::move(f.reservar(QStringLiteral("/c"), QStringLiteral("z.zip"))).valor();
        QVERIFY(w3.abrir(*r, fechaExportacion()));
        w3.abortar();
        QCOMPARE(w3.agregar(QStringLiteral("a.xml"), "x").error().codigo, CZ::Estado);
        QCOMPARE(w3.cerrar().error().codigo, CZ::Estado);
    }

    // Inspeccion binaria del caso normal (sin ZIP64).
    void formatoBinario()
    {
        const QList<Entrada> entradas = entradasMixtas();
        const QByteArray b = zipEnMemoria(entradas);
        QVERIFY(!b.isEmpty());
        Zip zip;
        const QString error = inspeccionar(b, entradas, zip, m_horaDos, m_fechaDos);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!zip.zip64);
        QVERIFY(!b.contains(QByteArray("PK\x06\x06", 4)));
        QVERIFY(!b.contains(QByteArray("PK\x06\x07", 4)));
        for (const Central& c : zip.entradas) {
            QCOMPARE(c.extra, quint16{0});
            QCOMPARE(c.necesita, quint16{20});
            QCOMPARE(c.hecho, quint16{0x0314});
        }
        QVERIFY(!b.contains("__MACOSX"));
        QVERIFY(!b.contains("/._"));
        // El XML comprime de verdad.
        QVERIFY(zip.entradas.at(2).comprimido < zip.entradas.at(2).descomprimido);
        // Vacio: deflate de 0 bytes.
        QCOMPARE(zip.entradas.at(4).descomprimido, quint64{0});
    }

    // Publicacion completa con DestinoExportacionPosix y oraculos externos.
    void publicacionCompletaConOraculos()
    {
        Publicacion pub;
        QVERIFY(pub.preparar());
        const QList<Entrada> entradas = entradasMixtas();
        auto r = pub.destino->reservar(pub.carpeta, QStringLiteral("Facturas.zip"));
        QVERIFY(r);
        auto parcial = std::move(r).valor();
        EscritorZipStreaming w;
        QVERIFY(w.abrir(*parcial, fechaExportacion()));
        for (const Entrada& e : entradas) {
            QVERIFY(w.agregar(e.nombre, e.datos));
        }
        auto c = w.cerrar();
        QVERIFY(c);
        QCOMPARE(c.valor().entradas, static_cast<int>(entradas.size()));
        QVERIFY(!c.valor().zip64);
        auto p = parcial->publicar();
        QVERIFY(p);
        QCOMPARE(p.valor().tamanoBytes, c.valor().bytes);
        const QString ruta = pub.ruta(QStringLiteral("Facturas.zip"));
        struct stat st {};
        QCOMPARE(::lstat(QFile::encodeName(ruta).constData(), &st), 0);
        QCOMPARE(static_cast<unsigned>(st.st_mode), static_cast<unsigned>(S_IFREG | 0600));

        const QString libzip = verificarConLibzip(ruta, entradas, fechaExportacion().toSecsSinceEpoch());
        QVERIFY2(libzip.isEmpty(), qPrintable(libzip));

        // LectorZipLibzip (ZIP de solo XML): mismo SHA-256 por posicion.
        QList<Entrada> xmls;
        for (int i = 0; i < 4; ++i) {
            xmls.append({QStringLiteral("emitidas/2026-01/%1.xml").arg(uuidSintetico(i)), xmlSintetico(i)});
        }
        const QByteArray zipXml = zipEnMemoria(xmls);
        const QString rutaXml = pub.tmp.filePath(QStringLiteral("xml.zip"));
        {
            QFile f(rutaXml);
            QVERIFY(f.open(QIODevice::WriteOnly) && f.write(zipXml) == zipXml.size());
        }
        LectorZipLibzip lector;
        QList<QByteArray> leidos;
        auto rec = lector.recorrer(rutaXml, [&](const EntradaXmlCfdi& e) {
            if (e.indice == leidos.size()) {
                leidos.append(sha(e.contenido));
            }
            return true;
        });
        QVERIFY(rec);
        QCOMPARE(rec.valor().xmlEntregados, 4);
        QVERIFY(rec.valor().omitidas.isEmpty());
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(leidos.at(i), sha(xmls.at(i).datos));
            auto una = lector.leerEntrada(rutaXml, i);
            QVERIFY(una && una.valor());
            QCOMPARE(sha(*una.valor()), sha(xmls.at(i).datos));
        }

        // Herramientas del sistema (si existen).
        const QString unzip = herramienta(QStringLiteral("unzip"));
        const QString zipinfo = herramienta(QStringLiteral("zipinfo"));
        if (unzip.isEmpty() || zipinfo.isEmpty()) {
            QSKIP("unzip/zipinfo no disponibles");
        }
        const Salida t = ejecutar(unzip, {QStringLiteral("-t"), ruta});
        QVERIFY2(t.codigo == 0 && t.texto.contains(QLatin1String("No errors detected")), qPrintable(t.texto));
        const Salida v = ejecutar(zipinfo, {QStringLiteral("-v"), ruta});
        const auto veces = [&](const char* patron) {
            return static_cast<int>(v.texto.count(QRegularExpression(QLatin1String(patron))));
        };
        const int n = static_cast<int>(entradas.size());
        QVERIFY(v.texto.contains(QLatin1String("There is no zipfile comment.")));
        QCOMPARE(veces("operating system of origin:\\s+Unix\\n"), n);
        QCOMPARE(veces("compression method:\\s+deflated\\n"), n);
        QCOMPARE(veces("file security status:\\s+not encrypted\\n"), n);
        QCOMPARE(veces("extended local header:\\s+yes\\n"), n);
        QCOMPARE(veces("file last modified on \\(DOS date/time\\):\\s+2026 Oct 24 13:45:30\\n"), n);
        QCOMPARE(veces("length of extra field:\\s+0 bytes\\n"), n);
        QCOMPARE(veces("length of file comment:\\s+0 characters\\n"), n);
        QCOMPARE(veces("Unix file attributes \\(100600 octal\\):\\s+-rw-------\\n"), n);
        QCOMPARE(veces("There is no file comment\\."), n);
        // zipinfo anota el descriptor de datos (16 bytes) de la entrada previa.
        QCOMPARE(veces("There are an extra 16 bytes preceding this file\\."), n - 1);
    }

    // ZIP64 por numero de entradas: 65,536 entradas vacias.
    void zip64PorEntradas()
    {
        Publicacion pub;
        QVERIFY(pub.preparar());
        QList<Entrada> entradas;
        entradas.reserve(65536);
        for (int i = 0; i < 65536; ++i) {
            entradas.append({QStringLiteral("e/%1.xml").arg(i, 5, 10, QLatin1Char('0')), QByteArray()});
        }
        auto parcial = std::move(pub.destino->reservar(pub.carpeta, QStringLiteral("muchas.zip"))).valor();
        EscritorZipStreaming w;
        QVERIFY(w.abrir(*parcial, fechaExportacion()));
        for (const Entrada& e : entradas) {
            QVERIFY(w.agregar(e.nombre, e.datos));
        }
        auto c = w.cerrar();
        QVERIFY(c);
        QVERIFY(c.valor().zip64);
        QVERIFY(parcial->publicar());
        const QString ruta = pub.ruta(QStringLiteral("muchas.zip"));
        QFile f(ruta);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray b = f.readAll();
        Zip zip;
        const QString error = inspeccionar(b, entradas, zip, m_horaDos, m_fechaDos);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(zip.zip64);
        QCOMPARE(u16(b, b.size() - 22 + 10), quint16{0xFFFF});
        // Las entradas en si no necesitan ZIP64.
        for (const Central& e : std::as_const(zip.entradas)) {
            QCOMPARE(e.extra, quint16{0});
        }
        int err = 0;
        zip_t* z = zip_open(QFile::encodeName(ruta).constData(), ZIP_RDONLY | ZIP_CHECKCONS, &err);
        QVERIFY2(z, qPrintable(QString::number(err)));
        QCOMPARE(zip_get_num_entries(z, 0), zip_int64_t{65536});
        QCOMPARE(QString::fromLatin1(zip_get_name(z, 65535, 0)), entradas.last().nombre);
        zip_discard(z);
        if (const QString unzip = herramienta(QStringLiteral("unzip")); !unzip.isEmpty()) {
            const Salida t = ejecutar(unzip, {QStringLiteral("-tq"), ruta});
            QVERIFY2(t.codigo == 0 && t.texto.contains(QLatin1String("No errors detected")), qPrintable(t.texto.right(2000)));
        }
    }

    // ZIP64 por tamanos y desplazamientos con un umbral reducido (los mismos
    // campos que con > 4 GiB, sin escribirlos).
    void zip64PorTamanosYDesplazamientos()
    {
        Publicacion pub;
        QVERIFY(pub.preparar());
        const QList<Entrada> entradas{
            {QStringLiteral("a/chica.xml"), QByteArray("chica")},                // sin ZIP64
            {QStringLiteral("a/grande.pdf"), aleatorio(5000, 3)},              // tamanos ZIP64 (local + descriptor)
            {QStringLiteral("a/desplazada.xml"), xmlSintetico(2)},               // solo desplazamiento ZIP64
        };
        EscritorZipStreaming::Opciones o;
        o.umbralZip64 = 2000;
        auto parcial = std::move(pub.destino->reservar(pub.carpeta, QStringLiteral("z64.zip"))).valor();
        EscritorZipStreaming w(o);
        QVERIFY(w.abrir(*parcial, fechaExportacion()));
        for (const Entrada& e : entradas) {
            QVERIFY(w.agregar(e.nombre, e.datos));
        }
        QVERIFY(w.cerrar().valor().zip64);
        QVERIFY(parcial->publicar());
        const QString ruta = pub.ruta(QStringLiteral("z64.zip"));
        QFile f(ruta);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray b = f.readAll();
        Zip zip;
        const QString error = inspeccionar(b, entradas, zip, m_horaDos, m_fechaDos);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(zip.zip64);
        QCOMPARE(zip.entradas.at(0).extra, quint16{0});
        QCOMPARE(zip.entradas.at(0).necesita, quint16{20});
        QCOMPARE(zip.entradas.at(1).extra, quint16{20}); // descomprimido + comprimido
        QCOMPARE(zip.entradas.at(2).extra, quint16{12}); // desplazamiento
        QCOMPARE(zip.entradas.at(2).necesita, quint16{45});
        const QString libzip = verificarConLibzip(ruta, entradas, fechaExportacion().toSecsSinceEpoch());
        QVERIFY2(libzip.isEmpty(), qPrintable(libzip));
        if (const QString unzip = herramienta(QStringLiteral("unzip")); !unzip.isEmpty()) {
            const Salida t = ejecutar(unzip, {QStringLiteral("-t"), ruta});
            QVERIFY2(t.codigo == 0 && t.texto.contains(QLatin1String("No errors detected")), qPrintable(t.texto));
        }
    }

    // Cancelacion durante los datos de una entrada y durante el directorio
    // central: error Cancelado, el llamador descarta y no queda nada.
    void cancelacion_data()
    {
        QTest::addColumn<bool>("enCentral");
        QTest::newRow("datos") << false;
        QTest::newRow("directorio_central") << true;
    }
    void cancelacion()
    {
        QFETCH(bool, enCentral);
        Publicacion pub;
        QVERIFY(pub.preparar());
        auto parcial = std::move(pub.destino->reservar(pub.carpeta, QStringLiteral("c.zip"))).valor();
        bool cerrando = false;
        int llamadas = 0;
        EscritorZipStreaming w;
        QVERIFY(w.abrir(*parcial, fechaExportacion(), [&] {
            ++llamadas;
            if (enCentral) {
                return !cerrando || llamadas < 3; // pasa la primera comprobacion de cerrar()
            }
            return llamadas < 3; // entrada + 1 trozo
        }));
        if (enCentral) {
            for (int i = 0; i < 600; ++i) {
                QVERIFY(w.agregar(QStringLiteral("e/%1.xml").arg(i), xmlSintetico(i)));
            }
            cerrando = true;
            llamadas = 0;
            auto c = w.cerrar();
            QVERIFY(!c);
            QCOMPARE(c.error().codigo, CZ::Cancelado);
        } else {
            const QByteArray grande = aleatorio(2 * 1024 * 1024, 11);
            auto a = w.agregar(QStringLiteral("grande.pdf"), grande);
            QVERIFY(!a);
            QCOMPARE(a.error().codigo, CZ::Cancelado);
            QCOMPARE(llamadas, 3);
        }
        QCOMPARE(w.agregar(QStringLiteral("otra.xml"), "x").error().codigo, CZ::Estado);
        parcial->descartar();
        QVERIFY(pub.vacio());
    }

    // Fallo de escritura (disco lleno) durante datos y durante el directorio
    // central: Escritura con el codigo del destino y errno; nada publicado.
    void falloDeEscritura_data()
    {
        QTest::addColumn<bool>("enCentral");
        QTest::newRow("datos") << false;
        QTest::newRow("directorio_central") << true;
    }
    void falloDeEscritura()
    {
        QFETCH(bool, enCentral);
        bool fallar = false;
        int escrituras = 0;
        Publicacion pub;
        QVERIFY(pub.preparar([&](Fase f) {
            if (f != Fase::Escribir) {
                return 0;
            }
            ++escrituras;
            return (fallar || (!enCentral && escrituras > 4)) ? ENOSPC : 0;
        }));
        auto parcial = std::move(pub.destino->reservar(pub.carpeta, QStringLiteral("f.zip"))).valor();
        EscritorZipStreaming w;
        QVERIFY(w.abrir(*parcial, fechaExportacion()));
        if (enCentral) {
            QVERIFY(w.agregar(QStringLiteral("grande.pdf"), aleatorio(200 * 1024, 5)));
            for (int i = 0; i < 20; ++i) {
                QVERIFY(w.agregar(QStringLiteral("e/%1.xml").arg(i), xmlSintetico(i)));
            }
            QVERIFY(escrituras > 0); // ya hubo escrituras de datos
            fallar = true;
            auto c = w.cerrar();
            QVERIFY(!c);
            QCOMPARE(c.error().codigo, CZ::Escritura);
            QCOMPARE(c.error().destino, std::optional(exportacion::ErrorDestinoExportacion::Codigo::EspacioInsuficiente));
            QCOMPARE(c.error().errnoSistema, ENOSPC);
        } else {
            auto a = w.agregar(QStringLiteral("grande.pdf"), aleatorio(2 * 1024 * 1024, 13));
            QVERIFY(!a);
            QCOMPARE(a.error().codigo, CZ::Escritura);
            QCOMPARE(a.error().destino, std::optional(exportacion::ErrorDestinoExportacion::Codigo::EspacioInsuficiente));
            QCOMPARE(a.error().errnoSistema, ENOSPC);
        }
        QVERIFY(!parcial->publicar()); // el parcial con error no se publica
        QVERIFY(pub.vacio());
    }

    // Memoria acotada: un payload de 8 MiB sale en escrituras de a lo mas
    // 64 KiB (no se arma el ZIP ni la entrada comprimida en memoria).
    void escriturasAcotadas()
    {
        fakes::FakeDestinoExportacion f;
        qint64 maximo = 0;
        int escrituras = 0;
        f.alEscribir = [&](qint64 n) {
            maximo = std::max(maximo, n);
            ++escrituras;
        };
        const QByteArray grande = aleatorio(8 * 1024 * 1024, 17);
        const QByteArray b = zipEnMemoria({{QStringLiteral("grande.pdf"), grande}}, {}, &f);
        QVERIFY(!b.isEmpty());
        QVERIFY(maximo <= 64 * 1024);
        QVERIFY(escrituras >= (8 * 1024 * 1024) / (64 * 1024));
        Zip zip;
        const QString error = inspeccionar(b, {{QStringLiteral("grande.pdf"), grande}}, zip, m_horaDos, m_fechaDos);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    // Calidad: la fabrica crea un escritor nuevo e independiente por ZIP.
    void fabricaCreaEscritoresNuevos()
    {
        const exportacion::FabricaEscritorZip fabrica = EscritorZipStreaming::fabrica();
        fakes::FakeDestinoExportacion f;
        const QString carpeta = QStringLiteral("/c");
        for (int i = 0; i < 2; ++i) {
            auto w = fabrica();
            QVERIFY(w);
            auto p = std::move(f.reservar(carpeta, QStringLiteral("z%1.zip").arg(i))).valor();
            QVERIFY(w->abrir(*p, fechaExportacion()));
            QVERIFY(w->agregar(QStringLiteral("a.xml"), xmlSintetico(i)));
            QVERIFY(w->cerrar());
            QCOMPARE(w->agregar(QStringLiteral("b.xml"), "x").error().codigo, CZ::Estado); // terminal
            QVERIFY(p->publicar());
            Zip zip;
            const QString error = inspeccionar(*f.archivo(carpeta, QStringLiteral("z%1.zip").arg(i)),
                                               {{QStringLiteral("a.xml"), xmlSintetico(i)}}, zip, m_horaDos, m_fechaDos);
            QVERIFY2(error.isEmpty(), qPrintable(error));
        }
        QVERIFY(fabrica().get() != fabrica().get());
    }

    // D20: los logs del escritor no llevan nombres de entrada ni datos.
    void logsSinNombres()
    {
        {
            QMutexLocker l(&g_mutexLog);
            g_log.clear();
        }
        const QString nombre = QStringLiteral("XAXX010101000/%1-hostil.xml").arg(uuidSintetico(5));
        QVERIFY(!zipEnMemoria({{nombre, xmlSintetico(1)}}).isEmpty());
        fakes::FakeDestinoExportacion f;
        f.falla = fakes::FakeDestinoExportacion::Falla::DiscoLleno;
        f.limiteEscritura = 10;
        (void)zipEnMemoria({{nombre, aleatorio(200 * 1024, 1)}}, {}, &f);
        QMutexLocker l(&g_mutexLog);
        QVERIFY(g_log.size() >= 2);
        for (const QString& linea : std::as_const(g_log)) {
            for (const QString& sonda : {QStringLiteral("XAXX"), QStringLiteral("hostil"), uuidSintetico(5),
                                         QStringLiteral("usuario"), QStringLiteral(".xml"), QStringLiteral("/")}) {
                QVERIFY2(!linea.contains(sonda), qPrintable(sonda));
            }
        }
    }
};

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestEscritorZipStreaming prueba;
    return QTest::qExec(&prueba, argc, argv);
}

#include "TestEscritorZipStreaming.moc"
