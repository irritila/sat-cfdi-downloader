#include "TestPackageStorageFs.h"

#include "fakes/FakePackageStorage.h"

#include "infrastructure/storage/FilesystemPackageStorage.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <cerrno>
#include <functional>
#include <map>
#include <memory>
#include <sys/stat.h>

using namespace satcfdi;
using fakes::FuenteZipEnMemoria;

namespace {

const QString kUuid = QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e");
const QByteArray kCentinela = QByteArrayLiteral("CENTINELA-ZIP-7f3a");

// FileOps real con fallos programables por operacion y por llamada (1-based;
// 0 = todas las llamadas) y un gancho antes de promover.
class FileOpsProgramable final : public PosixFileOps {
public:
    enum class Op { Tipo, CrearDirectorio, AbrirExclusivo, Escribir, SincronizarArchivo, Cerrar, Promover,
                    SincronizarDirectorio, EliminarArchivo, Listar };

    void fallar(Op op, int errnoSimulado, int llamada = 0) { m_fallos[op] = {llamada, errnoSimulado}; }
    int llamadas(Op op) const { return m_llamadas.count(op) ? m_llamadas.at(op) : 0; }
    int totalLlamadas() const
    {
        int t = 0;
        for (const auto& [op, n] : m_llamadas) {
            t += n;
        }
        return t;
    }
    QList<QByteArray> abiertos;
    std::function<void()> antesDePromover;

    int tipo(const QByteArray& r, TipoEntrada& t) override
    {
        if (int e = inyectar(Op::Tipo)) {
            return e;
        }
        return PosixFileOps::tipo(r, t);
    }
    int crearDirectorio(const QByteArray& r, unsigned m) override
    {
        if (int e = inyectar(Op::CrearDirectorio)) {
            return e;
        }
        return PosixFileOps::crearDirectorio(r, m);
    }
    int abrirExclusivo(const QByteArray& r, unsigned m, int& fd) override
    {
        if (int e = inyectar(Op::AbrirExclusivo)) {
            return e;
        }
        abiertos.append(r);
        return PosixFileOps::abrirExclusivo(r, m, fd);
    }
    int escribir(int fd, const char* d, std::size_t n) override
    {
        if (int e = inyectar(Op::Escribir)) {
            return e;
        }
        return PosixFileOps::escribir(fd, d, n);
    }
    int sincronizarArchivo(int fd) override
    {
        if (int e = inyectar(Op::SincronizarArchivo)) {
            return e;
        }
        return PosixFileOps::sincronizarArchivo(fd);
    }
    int cerrar(int fd) override
    {
        const int real = PosixFileOps::cerrar(fd); // nunca filtra el descriptor
        if (int e = inyectar(Op::Cerrar)) {
            return e;
        }
        return real;
    }
    int promoverSinReemplazo(const QByteArray& o, const QByteArray& d) override
    {
        if (antesDePromover) {
            antesDePromover();
        }
        if (int e = inyectar(Op::Promover)) {
            return e;
        }
        return PosixFileOps::promoverSinReemplazo(o, d);
    }
    int sincronizarDirectorio(const QByteArray& d) override
    {
        if (int e = inyectar(Op::SincronizarDirectorio)) {
            return e;
        }
        return PosixFileOps::sincronizarDirectorio(d);
    }
    int eliminarArchivo(const QByteArray& r) override
    {
        if (int e = inyectar(Op::EliminarArchivo)) {
            return e;
        }
        return PosixFileOps::eliminarArchivo(r);
    }
    int listar(const QByteArray& d, QList<EntradaDirectorio>& s) override
    {
        if (int e = inyectar(Op::Listar)) {
            return e;
        }
        return PosixFileOps::listar(d, s);
    }

private:
    int inyectar(Op op)
    {
        const int n = ++m_llamadas[op];
        const auto it = m_fallos.find(op);
        if (it != m_fallos.end() && (it->second.first == 0 || it->second.first == n)) {
            return it->second.second;
        }
        return 0;
    }
    std::map<Op, std::pair<int, int>> m_fallos;
    std::map<Op, int> m_llamadas;
};
using Op = FileOpsProgramable::Op;

UbicacionPaquete ubicacion(const QString& id = QStringLiteral("0F8FAD5B_01"))
{
    return UbicacionPaquete{QStringLiteral("AAA010101AAA"), QStringLiteral("2026-03-15T00:00:00"), kUuid, id};
}

QList<QByteArray> chunks(int n)
{
    QList<QByteArray> l;
    for (int i = 0; i < n; ++i) {
        l.append(QByteArray(1000 + i, char('a' + i)) + kCentinela);
    }
    return l;
}

QByteArray unir(const QList<QByteArray>& l)
{
    QByteArray r;
    for (const QByteArray& c : l) {
        r += c;
    }
    return r;
}

struct Entorno {
    QTemporaryDir dir;
    QString raiz;
    FileOpsProgramable* ops = nullptr; // propiedad del storage
    std::unique_ptr<FilesystemPackageStorage> storage;

    Entorno()
    {
        raiz = dir.filePath(QStringLiteral("paquetes"));
        auto o = std::make_unique<FileOpsProgramable>();
        ops = o.get();
        storage = std::make_unique<FilesystemPackageStorage>(raiz, std::move(o));
    }
    QString abs(const QString& rel) const { return raiz + QLatin1Char('/') + rel; }
    // Todos los archivos (incluidos ocultos) relativos a la raiz.
    QStringList archivos() const
    {
        QStringList r;
        QDirIterator it(raiz, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            r.append(QDir(raiz).relativeFilePath(it.next()));
        }
        r.sort();
        return r;
    }
    bool escribir(const QString& rel, const QByteArray& bytes) const
    {
        QDir().mkpath(QFileInfo(abs(rel)).absolutePath());
        QFile f(abs(rel));
        return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
    }
    QByteArray leer(const QString& rel) const
    {
        QFile f(abs(rel));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
};

unsigned modo(const QString& ruta)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(ruta).constData(), &st) == 0 ? unsigned(st.st_mode & 07777) : 0u;
}

QString relativa()
{
    return PackageStorage::derivarRutaRelativa(ubicacion()).valor();
}

} // namespace

void TestPackageStorageFs::initTestCase()
{
    m_carpetaUsuarioExistia = QDir::home().exists(QStringLiteral("SAT-CFDI-Downloader"));
}

void TestPackageStorageFs::cleanupTestCase()
{
    // Ninguna prueba crea la carpeta productiva del usuario (D8).
    QCOMPARE(QDir::home().exists(QStringLiteral("SAT-CFDI-Downloader")), m_carpetaUsuarioExistia);
}

void TestPackageStorageFs::guardadoExitosoPorChunks()
{
    Entorno e;
    QVERIFY(e.dir.isValid());
    const QList<QByteArray> datos = chunks(5);
    FuenteZipEnMemoria fuente(datos);
    const auto r = e.storage->guardarAtomico(ubicacion(), fuente, Cancelacion());
    QVERIFY2(r, r ? "" : qPrintable(r.error().diagnostico));
    QCOMPARE(r.valor().rutaRelativa, relativa());
    QVERIFY(!r.valor().advertenciaDurabilidad);
    QCOMPARE(e.ops->llamadas(Op::Escribir), 5);
    // Bytes exactos, sin temporal residual, permisos 0600 / 0700.
    QCOMPARE(e.leer(relativa()), unir(datos));
    QCOMPARE(e.archivos(), QStringList{relativa()});
    QCOMPARE(modo(e.abs(relativa())), 0600u);
    QCOMPARE(modo(e.raiz), 0700u);
    for (const QString& d : {QStringLiteral("AAA010101AAA"), QStringLiteral("AAA010101AAA/2026-03"),
                             QStringLiteral("AAA010101AAA/2026-03/") + kUuid}) {
        QCOMPARE(modo(e.abs(d)), 0700u);
    }
    // El temporal estuvo en la carpeta final, con el patron de D6.
    QCOMPARE(e.ops->abiertos.size(), 1);
    const QFileInfo temporal(QFile::decodeName(e.ops->abiertos.first()));
    QCOMPARE(temporal.absolutePath(), QFileInfo(e.abs(relativa())).absolutePath());
    QCOMPARE(rutapaquete::finalDeTemporal(temporal.fileName()), std::optional<QString>(QFileInfo(relativa()).fileName()));
    QCOMPARE(e.storage->existeArchivoFinal(relativa()).valor(), true);
}

void TestPackageStorageFs::fallosAntesDePromover_data()
{
    QTest::addColumn<int>("op");
    QTest::addColumn<int>("errnoSimulado");
    QTest::addColumn<int>("llamada");
    QTest::addColumn<int>("esperado");
    const auto fila = [](const char* n, Op op, int e, int llamada, ErrorAlmacenamiento esperado) {
        QTest::newRow(n) << int(op) << e << llamada << int(esperado);
    };
    fila("escritura_eio_chunk3", Op::Escribir, EIO, 3, ErrorAlmacenamiento::Escritura);
    fila("escritura_enospc_chunk2", Op::Escribir, ENOSPC, 2, ErrorAlmacenamiento::SinEspacio);
    fila("escritura_edquot", Op::Escribir, EDQUOT, 1, ErrorAlmacenamiento::SinEspacio);
    fila("temporal_enospc", Op::AbrirExclusivo, ENOSPC, 0, ErrorAlmacenamiento::SinEspacio);
    fila("temporal_eacces", Op::AbrirExclusivo, EACCES, 0, ErrorAlmacenamiento::Permiso);
    fila("directorio_eio", Op::CrearDirectorio, EIO, 2, ErrorAlmacenamiento::Escritura);
    fila("directorio_erofs", Op::CrearDirectorio, EROFS, 1, ErrorAlmacenamiento::Permiso);
    fila("cerrar_eio", Op::Cerrar, EIO, 1, ErrorAlmacenamiento::Escritura);
}

void TestPackageStorageFs::fallosAntesDePromover()
{
    QFETCH(int, op);
    QFETCH(int, errnoSimulado);
    QFETCH(int, llamada);
    QFETCH(int, esperado);
    Entorno e;
    e.ops->fallar(Op(op), errnoSimulado, llamada);
    FuenteZipEnMemoria fuente(chunks(4));
    const auto r = e.storage->guardarAtomico(ubicacion(), fuente, Cancelacion());
    QVERIFY(!r);
    QCOMPARE(r.error().origen, ErrorGuardarZip::Origen::Almacenamiento);
    QCOMPARE(int(r.error().almacenamiento), esperado);
    QVERIFY(e.archivos().isEmpty()); // ni final ni temporal
    QCOMPARE(e.ops->llamadas(Op::Promover), 0);
}

void TestPackageStorageFs::cancelacionEnChunkK()
{
    Entorno e;
    std::atomic<bool> cancelar{false};
    FuenteZipEnMemoria fuente(chunks(5));
    fuente.alEntregar = [&](int k) {
        if (k == 3) {
            cancelar = true;
        }
    };
    const auto r = e.storage->guardarAtomico(ubicacion(), fuente, Cancelacion([&] { return cancelar.load(); }));
    QVERIFY(!r);
    QCOMPARE(r.error().almacenamiento, ErrorAlmacenamiento::Cancelada);
    QCOMPARE(fuente.entregados(), 3);
    QVERIFY(e.archivos().isEmpty());
    // Cancelada antes de empezar: no toca el filesystem.
    Entorno e2;
    FuenteZipEnMemoria f2(chunks(1));
    QCOMPARE(e2.storage->guardarAtomico(ubicacion(), f2, Cancelacion([] { return true; })).error().almacenamiento,
             ErrorAlmacenamiento::Cancelada);
    QVERIFY(!QFileInfo::exists(e2.raiz));
}

void TestPackageStorageFs::fuenteFallidaLimpia()
{
    Entorno e;
    FuenteZipEnMemoria fuente(chunks(4));
    fuente.fallarEnChunk = 3;
    const auto r = e.storage->guardarAtomico(ubicacion(), fuente, Cancelacion());
    QVERIFY(!r);
    QCOMPARE(r.error().origen, ErrorGuardarZip::Origen::Fuente);
    QCOMPARE(r.error().diagnostico, QStringLiteral("fuente interrumpida"));
    QVERIFY(e.archivos().isEmpty());
}

void TestPackageStorageFs::permisoDenegado()
{
    // Raiz sin escritura (0500): no se puede crear la estructura.
    {
        Entorno e;
        QVERIFY(QDir().mkpath(e.raiz));
        QVERIFY(::chmod(QFile::encodeName(e.raiz).constData(), 0500) == 0);
        const auto restaurar = qScopeGuard([&] { ::chmod(QFile::encodeName(e.raiz).constData(), 0700); });
        FuenteZipEnMemoria fuente(chunks(2));
        const auto r = e.storage->guardarAtomico(ubicacion(), fuente, Cancelacion());
        QVERIFY(!r);
        QCOMPARE(r.error().almacenamiento, ErrorAlmacenamiento::Permiso);
        QVERIFY(!QFileInfo::exists(e.abs(relativa())));
    }
    // Carpeta de la solicitud sin escritura: no se puede crear el temporal.
    {
        Entorno e;
        const QString carpeta = e.abs(QStringLiteral("AAA010101AAA/2026-03/") + kUuid);
        QVERIFY(QDir().mkpath(carpeta));
        QVERIFY(::chmod(QFile::encodeName(carpeta).constData(), 0500) == 0);
        const auto restaurar = qScopeGuard([&] { ::chmod(QFile::encodeName(carpeta).constData(), 0700); });
        FuenteZipEnMemoria fuente(chunks(2));
        const auto r = e.storage->guardarAtomico(ubicacion(), fuente, Cancelacion());
        QVERIFY(!r);
        QCOMPARE(r.error().almacenamiento, ErrorAlmacenamiento::Permiso);
        QCOMPARE(QDir(carpeta).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList());
    }
}

void TestPackageStorageFs::durabilidadYPromocion()
{
    {
        Entorno e;
        e.ops->fallar(Op::SincronizarArchivo, EIO);
        FuenteZipEnMemoria fuente(chunks(3));
        const auto r = e.storage->guardarAtomico(ubicacion(), fuente, Cancelacion());
        QVERIFY(!r);
        QCOMPARE(r.error().almacenamiento, ErrorAlmacenamiento::Durabilidad);
        QCOMPARE(e.ops->llamadas(Op::Promover), 0); // D7: no promueve
        QVERIFY(e.archivos().isEmpty());
    }
    {
        Entorno e;
        e.ops->fallar(Op::Promover, EXDEV);
        FuenteZipEnMemoria fuente(chunks(3));
        const auto r = e.storage->guardarAtomico(ubicacion(), fuente, Cancelacion());
        QVERIFY(!r);
        QCOMPARE(r.error().almacenamiento, ErrorAlmacenamiento::Promocion);
        QVERIFY(e.archivos().isEmpty());
    }
}

void TestPackageStorageFs::fsyncDirectorioTrasPromoverEsAdvertencia()
{
    Entorno e;
    e.ops->fallar(Op::SincronizarDirectorio, EIO);
    const QList<QByteArray> datos = chunks(2);
    FuenteZipEnMemoria fuente(datos);
    const auto r = e.storage->guardarAtomico(ubicacion(), fuente, Cancelacion());
    QVERIFY(r);
    QVERIFY(r.valor().advertenciaDurabilidad);
    QCOMPARE(e.leer(relativa()), unir(datos));
    QCOMPARE(e.archivos(), QStringList{relativa()});
}

void TestPackageStorageFs::colisionConCentinela()
{
    // Final preexistente: rechazo previo, sin consumir la fuente.
    {
        Entorno e;
        QVERIFY(e.escribir(relativa(), kCentinela));
        FuenteZipEnMemoria fuente(chunks(2));
        const auto r = e.storage->guardarAtomico(ubicacion(), fuente, Cancelacion());
        QVERIFY(!r);
        QCOMPARE(r.error().almacenamiento, ErrorAlmacenamiento::ColisionDestino);
        QCOMPARE(fuente.entregados(), 0);
        QCOMPARE(e.leer(relativa()), kCentinela);
        QCOMPARE(e.archivos(), QStringList{relativa()});
    }
    // Final que aparece durante la escritura: RENAME_EXCL no lo reemplaza.
    {
        Entorno e;
        e.ops->antesDePromover = [&] { e.escribir(relativa(), kCentinela); };
        FuenteZipEnMemoria fuente(chunks(2));
        const auto r = e.storage->guardarAtomico(ubicacion(), fuente, Cancelacion());
        QVERIFY(!r);
        QCOMPARE(r.error().almacenamiento, ErrorAlmacenamiento::ColisionDestino);
        QCOMPARE(e.leer(relativa()), kCentinela);
        QCOMPARE(e.archivos(), QStringList{relativa()}); // temporal limpiado
    }
}

void TestPackageStorageFs::entradaInvalidaNoTocaFilesystem()
{
    Entorno e;
    UbicacionPaquete mala = ubicacion();
    mala.solicitudId = QStringLiteral("../../fuera");
    FuenteZipEnMemoria fuente(chunks(1));
    const auto r = e.storage->guardarAtomico(mala, fuente, Cancelacion());
    QVERIFY(!r);
    QCOMPARE(r.error().almacenamiento, ErrorAlmacenamiento::EntradaInvalida);
    QCOMPARE(e.storage->existeArchivoFinal(QStringLiteral("../../etc/passwd")).error(),
             ErrorAlmacenamiento::EntradaInvalida);
    QCOMPARE(e.storage->existeArchivoFinal(e.abs(relativa())).error(), ErrorAlmacenamiento::EntradaInvalida);
    HallazgoFilesystem falso;
    falso.tipo = TipoHallazgo::Temporal;
    falso.rutaRelativa = QStringLiteral("../x/.a.zip.0123456789abcdef.part");
    falso.archivoFinal = QStringLiteral("a.zip");
    QCOMPARE(e.storage->eliminarTemporal(falso).error(), ErrorAlmacenamiento::EntradaInvalida);
    QCOMPARE(e.ops->totalLlamadas(), 0);
    QCOMPARE(fuente.entregados(), 0);
    QVERIFY(!QFileInfo::exists(e.raiz));
    // Raiz relativa: LecturaRaiz sin E/S.
    auto ops = std::make_unique<FileOpsProgramable>();
    FileOpsProgramable* sonda = ops.get();
    FilesystemPackageStorage relativaRaiz(QStringLiteral("paquetes"), std::move(ops));
    QCOMPARE(relativaRaiz.guardarAtomico(ubicacion(), fuente, Cancelacion()).error().almacenamiento,
             ErrorAlmacenamiento::LecturaRaiz);
    QCOMPARE(relativaRaiz.escanearRecuperacion().error(), ErrorAlmacenamiento::LecturaRaiz);
    QCOMPARE(sonda->totalLlamadas(), 0);
}

void TestPackageStorageFs::existenciaDeFinales()
{
    Entorno e;
    QCOMPARE(e.storage->existeArchivoFinal(relativa()).valor(), false); // raiz inexistente
    QVERIFY(e.escribir(relativa(), kCentinela));
    QCOMPARE(e.storage->existeArchivoFinal(relativa()).valor(), true);
    const QString otra = PackageStorage::derivarRutaRelativa(ubicacion(QStringLiteral("otro"))).valor();
    QCOMPARE(e.storage->existeArchivoFinal(otra).valor(), false);
    // Error de lectura distinto de "no existe".
    const QString periodo = e.abs(QStringLiteral("AAA010101AAA/2026-03"));
    QVERIFY(::chmod(QFile::encodeName(periodo).constData(), 0000) == 0);
    const auto restaurar = qScopeGuard([&] { ::chmod(QFile::encodeName(periodo).constData(), 0700); });
    const auto r = e.storage->existeArchivoFinal(relativa());
    QVERIFY(!r);
    QCOMPARE(r.error(), ErrorAlmacenamiento::LecturaRaiz);
}

void TestPackageStorageFs::escaneoArbolMixto()
{
    Entorno e;
    QVERIFY(e.storage->escanearRecuperacion().valor().isEmpty()); // raiz inexistente
    const QString propia = QStringLiteral("AAA010101AAA/2026-03/") + kUuid;
    const QString finalPropio = propia + QStringLiteral("/p1.zip");
    const QString temporalPropio = propia + QStringLiteral("/.p2.zip.0123456789abcdef.part");
    const QStringList ajenos = {
        QStringLiteral("notas.txt"),
        QStringLiteral("AAA010101AAA/suelto.zip"),
        QStringLiteral("AAA010101AAA/2026-03/no-es-uuid/p.zip"),
        QStringLiteral("AAA010101AAA/2026-13/") + kUuid + QStringLiteral("/p.zip"),
        // (APFS no distingue mayusculas: se usan nombres realmente distintos.)
        QStringLiteral("RFC-INVALIDO/2026-03/") + kUuid + QStringLiteral("/p.zip"),
        QStringLiteral("AAA010101AAA/2026-03/{") + kUuid + QStringLiteral("}/p.zip"),
        propia + QStringLiteral("/.p3.zip.0123456789ABCDEF.part"),
        propia + QStringLiteral("/.p3.zip.0123.part"),
        propia + QStringLiteral("/p4.zip.part"),
        propia + QStringLiteral("/.DS_Store"),
        propia + QStringLiteral("/LEEME.txt"),
        propia + QStringLiteral("/sub/p5.zip"),
    };
    QVERIFY(e.escribir(finalPropio, kCentinela));
    QVERIFY(e.escribir(temporalPropio, QByteArrayLiteral("parcial")));
    for (const QString& a : ajenos) {
        QVERIFY(e.escribir(a, QByteArrayLiteral("ajeno")));
    }
    // Enlace con nombre de final: no es propio (no se siguen enlaces).
    QVERIFY(QFile::link(e.abs(finalPropio), e.abs(propia + QStringLiteral("/enlace.zip"))));
    const QStringList antes = e.archivos();

    const auto r = e.storage->escanearRecuperacion();
    QVERIFY(r);
    QCOMPARE(r.valor().size(), 2);
    const HallazgoFilesystem t = r.valor().at(0);
    QCOMPARE(t.tipo, TipoHallazgo::Temporal);
    QCOMPARE(t.rutaRelativa, temporalPropio);
    QCOMPARE(t.solicitudId.texto(), kUuid);
    QCOMPARE(t.archivoFinal, QStringLiteral("p2.zip"));
    const HallazgoFilesystem f = r.valor().at(1);
    QCOMPARE(f.tipo, TipoHallazgo::Final);
    QCOMPARE(f.rutaRelativa, finalPropio);
    QCOMPARE(f.solicitudId.texto(), kUuid);
    QCOMPARE(f.archivoFinal, QStringLiteral("p1.zip"));
    QCOMPARE(e.archivos(), antes); // el escaneo no borra nada

    // Carpeta propia ilegible: LecturaRaiz (no se omite en silencio).
    // Llamadas de listar por escaneo: raiz, RFC, periodo y solicitud (4.a).
    e.ops->fallar(Op::Listar, EACCES, e.ops->llamadas(Op::Listar) + 4);
    const auto ilegible = e.storage->escanearRecuperacion();
    QVERIFY(!ilegible);
    QCOMPARE(ilegible.error(), ErrorAlmacenamiento::LecturaRaiz);
}

void TestPackageStorageFs::eliminarSoloTemporalesPropios()
{
    Entorno e;
    const QString propia = QStringLiteral("AAA010101AAA/2026-03/") + kUuid;
    const QString ajeno = propia + QStringLiteral("/.p9.zip.0123456789ABCDEF.part");
    QVERIFY(e.escribir(propia + QStringLiteral("/p1.zip"), kCentinela));
    QVERIFY(e.escribir(propia + QStringLiteral("/.p2.zip.0123456789abcdef.part"), QByteArrayLiteral("x")));
    QVERIFY(e.escribir(ajeno, QByteArrayLiteral("x")));
    const auto h = e.storage->escanearRecuperacion().valor();
    QCOMPARE(h.size(), 2);
    const HallazgoFilesystem temporal = h.at(0);
    const HallazgoFilesystem final = h.at(1);

    QCOMPARE(e.storage->eliminarTemporal(final).error(), ErrorAlmacenamiento::EntradaInvalida);
    HallazgoFilesystem disfrazado = final; // un final con tipo Temporal
    disfrazado.tipo = TipoHallazgo::Temporal;
    QCOMPARE(e.storage->eliminarTemporal(disfrazado).error(), ErrorAlmacenamiento::EntradaInvalida);
    HallazgoFilesystem forjado = temporal;
    forjado.rutaRelativa = ajeno;
    QCOMPARE(e.storage->eliminarTemporal(forjado).error(), ErrorAlmacenamiento::EntradaInvalida);
    HallazgoFilesystem incoherente = temporal;
    incoherente.archivoFinal = QStringLiteral("p1.zip");
    QCOMPARE(e.storage->eliminarTemporal(incoherente).error(), ErrorAlmacenamiento::EntradaInvalida);

    QVERIFY(e.storage->eliminarTemporal(temporal));
    QVERIFY(e.storage->eliminarTemporal(temporal)); // idempotente
    QCOMPARE(e.archivos(), (QStringList{ajeno, propia + QStringLiteral("/p1.zip")}));
    QCOMPARE(e.leer(propia + QStringLiteral("/p1.zip")), kCentinela);
}

void TestPackageStorageFs::diagnosticosSinBytesNiRutasAbsolutas()
{
    QList<std::pair<Op, int>> fallos = {{Op::Escribir, ENOSPC}, {Op::SincronizarArchivo, EIO},
                                        {Op::Promover, EXDEV}, {Op::AbrirExclusivo, EACCES}};
    for (const auto& [op, err] : fallos) {
        Entorno e;
        e.ops->fallar(op, err);
        FuenteZipEnMemoria fuente(chunks(2));
        const auto r = e.storage->guardarAtomico(ubicacion(), fuente, Cancelacion());
        QVERIFY(!r);
        QVERIFY(!r.error().diagnostico.isEmpty());
        QVERIFY(!r.error().diagnostico.contains(QString::fromLatin1(kCentinela)));
        QVERIFY2(!r.error().diagnostico.contains(e.dir.path()), qPrintable(r.error().diagnostico));
    }
}

void TestPackageStorageFs::raizEfectiva()
{
    QCOMPARE(FilesystemPackageStorage::resolverRaiz(QStringLiteral("/datos/app"), QStringLiteral("/Users/x")),
             QStringLiteral("/datos/app/paquetes"));
    QCOMPARE(FilesystemPackageStorage::resolverRaiz(std::nullopt, QStringLiteral("/Users/x")),
             QStringLiteral("/Users/x/SAT-CFDI-Downloader/paquetes"));
    QCOMPARE(FilesystemPackageStorage::resolverRaiz(QString(), QStringLiteral("/Users/x")),
             QStringLiteral("/Users/x/SAT-CFDI-Downloader/paquetes"));
    // Con data-dir real: la raiz efectiva se crea solo bajo ese directorio.
    QTemporaryDir dataDir;
    FilesystemPackageStorage storage(FilesystemPackageStorage::resolverRaiz(dataDir.path(), QDir::homePath()));
    FuenteZipEnMemoria fuente(chunks(1));
    QVERIFY(storage.guardarAtomico(ubicacion(), fuente, Cancelacion()));
    QVERIFY(QFileInfo::exists(dataDir.filePath(QStringLiteral("paquetes/")) + relativa()));
}
