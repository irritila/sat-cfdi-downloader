// T025.4: adaptadores macOS del respaldo contra el sistema real, con
// QTemporaryDir y datos sinteticos: CarpetaRespaldoMacOS (ruta canonica,
// bookmark, UUID de volumen, renombrado) y la actividad de sistema
// (aserciones IOPM del proceso). Sin bundle, permisos ni Keychain.

#include "infrastructure/os/macos/ActividadSistemaMacOS.h"
#include "infrastructure/os/macos/CarpetaRespaldoMacOS.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using satcfdi::OSIntegration;
using satcfdi::macos::CarpetaRespaldoMacOS;
using Categoria = satcfdi::respaldo::ErrorCarpetaRespaldo::Categoria;

namespace {

QString canonica(const QString& ruta)
{
    return QFileInfo(ruta).canonicalFilePath();
}

// Cuantas aserciones IOPM de este proceso se llaman `nombre`.
int asercionesDelProceso(const QString& nombre)
{
    CFDictionaryRef porProceso = nullptr;
    if (IOPMCopyAssertionsByProcess(&porProceso) != kIOReturnSuccess || porProceso == nullptr) {
        return -1;
    }
    int n = 0;
    const int pid = static_cast<int>(::getpid());
    CFNumberRef clave = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
    const auto lista = static_cast<CFArrayRef>(CFDictionaryGetValue(porProceso, clave));
    if (lista != nullptr && CFGetTypeID(lista) == CFArrayGetTypeID()) {
        for (CFIndex i = 0; i < CFArrayGetCount(lista); ++i) {
            const auto a = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(lista, i));
            const auto valor = static_cast<CFStringRef>(CFDictionaryGetValue(a, kIOPMAssertionNameKey));
            if (valor != nullptr && CFGetTypeID(valor) == CFStringGetTypeID() && QString::fromCFString(valor) == nombre) {
                ++n;
            }
        }
    }
    CFRelease(clave);
    CFRelease(porProceso);
    return n;
}

} // namespace

class TestPlataformaRespaldoMacOS : public QObject {
    Q_OBJECT

private slots:
    void referenciaDeCarpetaYResolucion()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString carpeta = tmp.filePath(QStringLiteral("Respaldo sintetico"));
        QVERIFY(QDir().mkpath(carpeta));

        const CarpetaRespaldoMacOS sistema;
        const auto ref = sistema.crearReferencia(carpeta);
        QVERIFY(ref.esExito());
        // QTemporaryDir vive bajo /var/folders -> /private/var/folders.
        QCOMPARE(ref.valor().rutaCanonica, canonica(carpeta));
        QVERIFY(ref.valor().rutaCanonica.startsWith(QStringLiteral("/private/")));
        QVERIFY(!ref.valor().bookmark.isEmpty());
        QVERIFY2(!ref.valor().uuidVolumen.isEmpty(), "el volumen de arranque (APFS) reporta UUID");

        const auto r = sistema.resolver(ref.valor().bookmark);
        QVERIFY(r.esExito());
        QCOMPARE(r.valor().rutaCanonica, ref.valor().rutaCanonica);
        QCOMPARE(r.valor().uuidVolumen, ref.valor().uuidVolumen);
        QVERIFY(!r.valor().obsoleto);
    }

    void enlaceSeCanonicaliza()
    {
        // Como ~/Dropbox -> ~/Library/CloudStorage/Dropbox.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString real = tmp.filePath(QStringLiteral("CloudStorage/Proveedor"));
        QVERIFY(QDir().mkpath(real));
        const QString enlace = tmp.filePath(QStringLiteral("Proveedor"));
        QVERIFY(QFile::link(real, enlace));

        const CarpetaRespaldoMacOS sistema;
        const auto ref = sistema.crearReferencia(enlace);
        QVERIFY(ref.esExito());
        QCOMPARE(ref.valor().rutaCanonica, canonica(real));
        const auto r = sistema.resolver(ref.valor().bookmark);
        QVERIFY(r.esExito());
        QCOMPARE(r.valor().rutaCanonica, canonica(real));
    }

    void renombrarSigueResolviendo()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString antes = tmp.filePath(QStringLiteral("Respaldo"));
        const QString despues = tmp.filePath(QStringLiteral("Respaldo renombrado"));
        QVERIFY(QDir().mkpath(antes));

        const CarpetaRespaldoMacOS sistema;
        const auto ref = sistema.crearReferencia(antes);
        QVERIFY(ref.esExito());
        QVERIFY(QDir().rename(antes, despues));

        const auto r = sistema.resolver(ref.valor().bookmark);
        QVERIFY(r.esExito());
        QCOMPARE(r.valor().rutaCanonica, canonica(despues));
        QCOMPARE(r.valor().uuidVolumen, ref.valor().uuidVolumen);
        // `obsoleto` lo decide el sistema; si lo pide, regenerar funciona.
        if (r.valor().obsoleto) {
            QVERIFY(sistema.crearReferencia(r.valor().rutaCanonica).esExito());
        }
    }

    void erroresDeCrearReferencia()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const CarpetaRespaldoMacOS sistema;
        QCOMPARE(sistema.crearReferencia(QString()).error().categoria, Categoria::RutaInvalida);
        QCOMPARE(sistema.crearReferencia(QStringLiteral("relativa/carpeta")).error().categoria, Categoria::RutaInvalida);
        QCOMPARE(sistema.crearReferencia(tmp.filePath(QStringLiteral("no-existe"))).error().categoria,
                 Categoria::NoExiste);
        const QString archivo = tmp.filePath(QStringLiteral("archivo.txt"));
        QFile f(archivo);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("sintetico");
        f.close();
        QCOMPARE(sistema.crearReferencia(archivo).error().categoria, Categoria::NoEsCarpeta);
        // Los diagnosticos no llevan rutas.
        QVERIFY(!sistema.crearReferencia(archivo).error().diagnostico.contains(QLatin1Char('/')));
    }

    void erroresDeResolver()
    {
        const CarpetaRespaldoMacOS sistema;
        QCOMPARE(sistema.resolver(QByteArray()).error().categoria, Categoria::BookmarkInvalido);
        QCOMPARE(sistema.resolver(QByteArray("no es un bookmark")).error().categoria, Categoria::BookmarkInvalido);

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString carpeta = tmp.filePath(QStringLiteral("Borrada"));
        QVERIFY(QDir().mkpath(carpeta));
        const auto ref = sistema.crearReferencia(carpeta);
        QVERIFY(ref.esExito());
        QVERIFY(QDir(carpeta).removeRecursively());
        QCOMPARE(sistema.resolver(ref.valor().bookmark).error().categoria, Categoria::NoResuelve);
    }

    void carpetaEnPapeleraSeRechaza()
    {
        // Papelera sintetica (componente ".Trash") para no tocar la real.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString carpeta = tmp.filePath(QStringLiteral("Respaldo"));
        QVERIFY(QDir().mkpath(carpeta));
        QVERIFY(QDir().mkpath(tmp.filePath(QStringLiteral(".Trash"))));
        const CarpetaRespaldoMacOS sistema;
        const auto ref = sistema.crearReferencia(carpeta);
        QVERIFY(ref.esExito());
        QVERIFY(QDir().rename(carpeta, tmp.filePath(QStringLiteral(".Trash/Respaldo"))));
        QCOMPARE(sistema.resolver(ref.valor().bookmark).error().categoria, Categoria::EnPapelera);
    }

    void actividadDeSistemaViveConElToken()
    {
        const QString motivo = satcfdi::macos::textoMotivoActividad(OSIntegration::MotivoActividad::Respaldo);
        QCOMPARE(motivo, QStringLiteral("Archivo Fiscal: respaldo en curso"));
        const int base = asercionesDelProceso(motivo);
        if (base < 0) {
            QSKIP("IOPMCopyAssertionsByProcess no disponible en este entorno");
        }
        QCOMPARE(base, 0);
        {
            auto token = satcfdi::macos::iniciarActividadSistemaMacOS(OSIntegration::MotivoActividad::Respaldo);
            QVERIFY(token);
            QTRY_COMPARE(asercionesDelProceso(motivo), 1);
        }
        QTRY_COMPARE(asercionesDelProceso(motivo), 0);
    }
};

QTEST_GUILESS_MAIN(TestPlataformaRespaldoMacOS)
#include "TestPlataformaRespaldoMacOS.moc"
