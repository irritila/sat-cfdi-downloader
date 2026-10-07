#include "TestPackageStorageRevelable.h"

#include "infrastructure/storage/FilesystemPackageStorage.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>
#include <sys/time.h>

using namespace satcfdi;
using Estado = RutaRevelable::Estado;

namespace {

const QString kUuid = QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e");
const QString kCarpeta = QStringLiteral("AAA010101AAA/2026-03/") + kUuid;
const QString kRel = kCarpeta + QStringLiteral("/0F8FAD5B_01.zip");
const QByteArray kCentinela = QByteArrayLiteral("PK\x03\x04-CENTINELA-T0091");

struct Entorno {
    QTemporaryDir dir;
    QString raiz = dir.filePath(QStringLiteral("paquetes"));
    FilesystemPackageStorage storage{raiz};

    QString abs(const QString& rel) const { return raiz + QLatin1Char('/') + rel; }
    bool escribir(const QString& rel, const QByteArray& bytes = kCentinela) const
    {
        QDir().mkpath(QFileInfo(abs(rel)).absolutePath());
        QFile f(abs(rel));
        return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
    }
    QStringList todo() const
    {
        QStringList r;
        QDirIterator it(dir.path(), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            r.append(it.next());
        }
        r.sort();
        return r;
    }
};

bool esInvalida(const Resultado<RutaRevelable, ErrorAlmacenamiento>& r)
{
    return !r && r.error() == ErrorAlmacenamiento::EntradaInvalida;
}

bool noEncontrada(const Resultado<RutaRevelable, ErrorAlmacenamiento>& r)
{
    return r && r.valor().estado == Estado::NoEncontrada && r.valor().rutaAbsoluta.isEmpty();
}

} // namespace

void TestPackageStorageRevelable::archivoPresenteSinAbrirElZip()
{
    Entorno e;
    QVERIFY(e.escribir(kRel));
    // atime anterior a mtime: con la politica de APFS (actualiza atime si es
    // anterior a mtime), cualquier lectura del ZIP lo moveria. Asi la
    // comparacion es confiable aunque el volumen no use strictatime.
    const QByteArray nativa = QFile::encodeName(e.abs(kRel));
    timeval tiempos[2] = {{1700000000, 0}, {1700086400, 0}}; // atime, mtime
    QVERIFY(::utimes(nativa.constData(), tiempos) == 0);
    struct stat antes {};
    QVERIFY(::lstat(nativa.constData(), &antes) == 0);

    const auto r = e.storage.resolverArchivoRevelable(kRel);
    QVERIFY(r);
    QCOMPARE(r.valor().estado, Estado::Disponible);
    QCOMPARE(r.valor().rutaAbsoluta, e.abs(kRel));
    QVERIFY(QFileInfo(r.valor().rutaAbsoluta).isAbsolute());

    struct stat despues {};
    QVERIFY(::lstat(nativa.constData(), &despues) == 0);
    QCOMPARE(despues.st_atimespec.tv_sec, antes.st_atimespec.tv_sec);
    QCOMPARE(despues.st_mtimespec.tv_sec, antes.st_mtimespec.tv_sec);
    QFile f(e.abs(kRel));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), kCentinela);
}

void TestPackageStorageRevelable::archivoAusente()
{
    Entorno e;
    QVERIFY(e.escribir(kCarpeta + QStringLiteral("/otro.zip")));
    const QStringList antes = e.todo();
    QVERIFY(noEncontrada(e.storage.resolverArchivoRevelable(kRel)));
    // Carpeta intermedia ausente: tampoco se crea.
    const QString otraSolicitud =
        QStringLiteral("AAA010101AAA/2026-04/2a1b6c7d-0000-4000-8000-000000000001/p.zip");
    QVERIFY(noEncontrada(e.storage.resolverArchivoRevelable(otraSolicitud)));
    QCOMPARE(e.todo(), antes);
    // Destino de tipo inesperado (directorio con nombre de ZIP).
    QVERIFY(QDir().mkpath(e.abs(kRel)));
    QVERIFY(esInvalida(e.storage.resolverArchivoRevelable(kRel)));
}

void TestPackageStorageRevelable::raizAusenteNoSeCrea()
{
    Entorno e;
    QVERIFY(!QFileInfo::exists(e.raiz));
    QVERIFY(noEncontrada(e.storage.resolverArchivoRevelable(kRel)));
    QVERIFY(noEncontrada(e.storage.resolverCarpetaSolicitudRevelable(kRel)));
    QVERIFY(noEncontrada(e.storage.resolverRaizRevelable()));
    QVERIFY(!QFileInfo::exists(e.raiz));
    QVERIFY(e.todo().isEmpty());
}

void TestPackageStorageRevelable::rutasInvalidasSeRechazan()
{
    Entorno e;
    QVERIFY(e.escribir(kRel));
    QVERIFY(e.escribir(QStringLiteral("fuera.zip")));
    for (const QString& mala : {QStringLiteral("../fuera.zip"), QStringLiteral("AAA010101AAA/2026-03/../fuera.zip"),
                                kCarpeta + QStringLiteral("/../0F8FAD5B_01.zip"),
                                QStringLiteral("AAA010101AAA/2026-03/") + kUuid + QStringLiteral("/../../../fuera.zip"),
                                e.abs(kRel), QStringLiteral("/etc/passwd"), QStringLiteral("fuera.zip"),
                                QString(), kCarpeta, kCarpeta + QStringLiteral("/")}) {
        QVERIFY2(esInvalida(e.storage.resolverArchivoRevelable(mala)), qPrintable(mala));
        QVERIFY2(esInvalida(e.storage.resolverCarpetaSolicitudRevelable(mala)), qPrintable(mala));
    }
    // Raiz relativa: LecturaRaiz, sin E/S.
    FilesystemPackageStorage relativa(QStringLiteral("paquetes"));
    const auto r = relativa.resolverRaizRevelable();
    QVERIFY(!r);
    QCOMPARE(r.error(), ErrorAlmacenamiento::LecturaRaiz);
}

void TestPackageStorageRevelable::symlinksSeRechazan()
{
    // Directorio externo con la misma estructura, enlazado dentro de la raiz.
    {
        Entorno e;
        QTemporaryDir externo;
        const QString extRfc = externo.filePath(QStringLiteral("AAA010101AAA"));
        QDir().mkpath(extRfc + QStringLiteral("/2026-03/") + kUuid);
        QFile f(extRfc + QStringLiteral("/2026-03/") + kUuid + QStringLiteral("/0F8FAD5B_01.zip"));
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(kCentinela) > 0);
        f.close();
        QVERIFY(QDir().mkpath(e.raiz));
        QVERIFY(QFile::link(extRfc, e.abs(QStringLiteral("AAA010101AAA"))));
        QVERIFY(QFileInfo::exists(e.abs(kRel))); // existe siguiendo el enlace
        QVERIFY(esInvalida(e.storage.resolverArchivoRevelable(kRel)));
        QVERIFY(esInvalida(e.storage.resolverCarpetaSolicitudRevelable(kRel)));
    }
    // Carpeta de la solicitud como enlace.
    {
        Entorno e;
        QTemporaryDir externo;
        QVERIFY(QDir().mkpath(e.abs(QStringLiteral("AAA010101AAA/2026-03"))));
        QVERIFY(QFile::link(externo.path(), e.abs(kCarpeta)));
        QVERIFY(esInvalida(e.storage.resolverCarpetaSolicitudRevelable(kRel)));
    }
    // Archivo final como enlace a un archivo externo.
    {
        Entorno e;
        QTemporaryDir externo;
        const QString destino = externo.filePath(QStringLiteral("secreto.zip"));
        QFile f(destino);
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(kCentinela) > 0);
        f.close();
        QVERIFY(QDir().mkpath(e.abs(kCarpeta)));
        QVERIFY(QFile::link(destino, e.abs(kRel)));
        QVERIFY(esInvalida(e.storage.resolverArchivoRevelable(kRel)));
        // La carpeta (real) si es revelable.
        const auto c = e.storage.resolverCarpetaSolicitudRevelable(kRel);
        QVERIFY(c && c.valor().estado == Estado::Disponible);
    }
}

void TestPackageStorageRevelable::carpetaDeLaSolicitud()
{
    Entorno e;
    QVERIFY(e.escribir(kRel));
    const auto c = e.storage.resolverCarpetaSolicitudRevelable(kRel);
    QVERIFY(c);
    QCOMPARE(c.valor().estado, Estado::Disponible);
    QCOMPARE(c.valor().rutaAbsoluta, e.abs(kCarpeta));
    // El ZIP no necesita existir: basta la carpeta.
    QVERIFY(QFile::remove(e.abs(kRel)));
    QCOMPARE(e.storage.resolverCarpetaSolicitudRevelable(kRel).valor().rutaAbsoluta, e.abs(kCarpeta));
    // Carpeta borrada: NoEncontrada y no se recrea.
    QVERIFY(QDir(e.abs(kCarpeta)).removeRecursively());
    QVERIFY(noEncontrada(e.storage.resolverCarpetaSolicitudRevelable(kRel)));
    QVERIFY(!QFileInfo::exists(e.abs(kCarpeta)));
    // La carpeta como archivo regular: tipo inesperado.
    QVERIFY(e.escribir(kCarpeta, "x"));
    QVERIFY(esInvalida(e.storage.resolverCarpetaSolicitudRevelable(kRel)));
}

void TestPackageStorageRevelable::raizRevelable()
{
    {
        Entorno e;
        QVERIFY(QDir().mkpath(e.raiz));
        const auto r = e.storage.resolverRaizRevelable();
        QVERIFY(r);
        QCOMPARE(r.valor().estado, Estado::Disponible);
        QCOMPARE(r.valor().rutaAbsoluta, QDir::cleanPath(e.raiz));
    }
    // Raiz que es un archivo: inaccesible como carpeta.
    {
        Entorno e;
        QFile f(e.raiz);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        const auto r = e.storage.resolverRaizRevelable();
        QVERIFY(!r);
        QCOMPARE(r.error(), ErrorAlmacenamiento::LecturaRaiz);
    }
    // Raiz que es un enlace a un directorio (como /var en macOS): se acepta.
    {
        Entorno e;
        QTemporaryDir real;
        QVERIFY(QFile::link(real.path(), e.raiz));
        const auto r = e.storage.resolverRaizRevelable();
        QVERIFY(r && r.valor().estado == Estado::Disponible);
    }
}
