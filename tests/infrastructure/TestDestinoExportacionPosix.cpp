// T026.1 (D12, D20): DestinoExportacionPosix sobre QTemporaryDir (fallas
// inyectadas por fase, colision tardia, permisos, xattr, limpieza local y de
// otro volumen simulado, logs sin rutas ni nombres) y la limpieza al arrancar
// desde el AppCompositionRoot real. Datos sinteticos.

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"
#include "infrastructure/storage/DestinoExportacionPosix.h"

#include "fakes/FakeDestinoExportacion.h"
#include "fakes/FakeOperacionesSat.h"
#include "fakes/FakeSecretStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutex>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <sys/stat.h>
#include <sys/acl.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <cerrno>
#include <functional>
#include <memory>

using namespace satcfdi;
using exportacion::ArchivoParcialExportacion;
using exportacion::ErrorDestinoExportacion;
using C = ErrorDestinoExportacion::Codigo;
using Fase = DestinoExportacionPosix::Fase;

Q_DECLARE_METATYPE(DestinoExportacionPosix::Fase)

namespace {

const QString kParcialFijo = QStringLiteral(".archivo-fiscal-exportacion-0123456789abcdef.partial");

QMutex g_mutexLog;
QStringList g_log;
QtMessageHandler g_anterior = nullptr;

void capturarLog(QtMsgType tipo, const QMessageLogContext& ctx, const QString& msg)
{
    {
        QMutexLocker l(&g_mutexLog);
        g_log.append(QString::fromLatin1(ctx.category ? ctx.category : "") + QLatin1Char(' ') + msg);
    }
    if (g_anterior) {
        g_anterior(tipo, ctx, msg);
    }
}

QStringList logCapturado()
{
    QMutexLocker l(&g_mutexLog);
    return g_log;
}

QStringList entradas(const QString& carpeta)
{
    QStringList r = QDir(carpeta).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    r.sort();
    return r;
}

bool escribir(const QString& ruta, const QByteArray& datos)
{
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    return f.open(QIODevice::WriteOnly) && f.write(datos) == datos.size();
}

QByteArray leer(const QString& ruta)
{
    QFile f(ruta);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

unsigned modo(const QString& ruta)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(ruta).constData(), &st) == 0 ? static_cast<unsigned>(st.st_mode) : 0;
}

ssize_t tamanoXattr(const QString& ruta)
{
    return ::listxattr(QFile::encodeName(ruta).constData(), nullptr, 0, XATTR_NOFOLLOW);
}

QStringList atributos(const QString& ruta)
{
    const QByteArray nativa = QFile::encodeName(ruta);
    const ssize_t n = ::listxattr(nativa.constData(), nullptr, 0, XATTR_NOFOLLOW);
    if (n <= 0) {
        return {};
    }
    QByteArray nombres(static_cast<qsizetype>(n), '\0');
    const ssize_t m = ::listxattr(nativa.constData(), nombres.data(), static_cast<size_t>(n), XATTR_NOFOLLOW);
    QStringList r;
    for (const QByteArray& a : nombres.left(m > 0 ? static_cast<qsizetype>(m) : 0).split('\0')) {
        if (!a.isEmpty()) {
            r.append(QString::fromLatin1(a));
        }
    }
    return r;
}

// D12: el ZIP no lleva atributos extendidos. Excepcion: macOS 13+ aplica
// com.apple.provenance a todo archivo creado por un proceso con procedencia
// (lanzado desde una terminal o una app descargada) y no permite retirarlo
// (removexattr "tiene exito" y el atributo sigue); no lleva datos del ZIP.
bool sinAtributosPropios(const QString& ruta)
{
    QStringList a = atributos(ruta);
    a.removeAll(QStringLiteral("com.apple.provenance"));
    return a.isEmpty();
}

using Salida_ = int;

int ejecutarChmod(const QStringList& argumentos)
{
    QProcess p;
    p.start(QStringLiteral("/bin/chmod"), argumentos);
    if (!p.waitForFinished(30000)) {
        return -1;
    }
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

bool tieneAcl(const QString& ruta)
{
    acl_t acl = ::acl_get_link_np(QFile::encodeName(ruta).constData(), ACL_TYPE_EXTENDED);
    if (acl == nullptr) {
        return false;
    }
    acl_entry_t entrada = nullptr;
    const bool alguna = ::acl_get_entry(acl, ACL_FIRST_ENTRY, &entrada) == 0;
    ::acl_free(acl);
    return alguna;
}

struct Entorno {
    QTemporaryDir tmp;
    QString datos;
    QString destino;

    bool preparar(const QString& subDestino = QStringLiteral("destino"))
    {
        if (!tmp.isValid()) {
            return false;
        }
        datos = tmp.filePath(QStringLiteral("datos"));
        destino = tmp.filePath(subDestino);
        return QDir().mkpath(datos) && QDir().mkpath(destino);
    }
    QString privada() const { return DestinoExportacionPosix::carpetaParciales(datos); }
    QString registro() const { return DestinoExportacionPosix::rutaRegistro(datos); }

    std::unique_ptr<DestinoExportacionPosix> crear(std::function<int(Fase)> falla = {}, bool otroVolumen = false) const
    {
        DestinoExportacionPosix::Opciones o;
        o.directorioDatos = datos;
        o.inyectarFalla = std::move(falla);
        o.simularOtroVolumen = otroVolumen;
        return std::make_unique<DestinoExportacionPosix>(std::move(o));
    }
};

// Escribe un "ZIP" con una cabecera reescrita por seek (como un escritor ZIP).
bool escribirContenido(QIODevice& d, const QByteArray& cuerpo)
{
    if (d.write("XXXX") != 4 || d.write(cuerpo) != cuerpo.size() || !d.seek(0) || d.write("PK\x03\x04", 4) != 4
        || !d.seek(4 + cuerpo.size())) {
        return false;
    }
    return d.write("FIN") == 3;
}

QByteArray esperado(const QByteArray& cuerpo)
{
    return QByteArray("PK\x03\x04", 4) + cuerpo + "FIN";
}

} // namespace

class TestDestinoExportacionPosix : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QLoggingCategory::setFilterRules(QStringLiteral("satcfdi.*=true"));
        g_anterior = qInstallMessageHandler(capturarLog);
    }
    void cleanupTestCase() { qInstallMessageHandler(g_anterior); }

    void patronDeNombres()
    {
        using exportacion::nombres::esParcialExportacion;
        QVERIFY(esParcialExportacion(kParcialFijo));
        QVERIFY(!esParcialExportacion(QStringLiteral(".archivo-fiscal-exportacion-0123456789ABCDEF.partial")));
        QVERIFY(!esParcialExportacion(QStringLiteral(".archivo-fiscal-exportacion-0123456789abcde.partial")));
        QVERIFY(!esParcialExportacion(QStringLiteral(".archivo-fiscal-exportacion-0123456789abcdef0.partial")));
        QVERIFY(!esParcialExportacion(QStringLiteral(".archivo-fiscal-exportacion-0123456789abcdef.partial.x")));
        QVERIFY(!esParcialExportacion(QStringLiteral(".archivo-fiscal-0123456789abcdef.partial")));
    }

    // Mismo volumen: el .partial vive en la carpeta privada 0700; durante la
    // escritura no hay otros archivos; el ZIP final es 0600 sin xattr.
    void publicaEnElMismoVolumen()
    {
        Entorno e;
        QVERIFY(e.preparar());
        QVERIFY(escribir(QDir(e.destino).filePath(QStringLiteral("previo.zip")), "previo"));
        QStringList durante;
        QStringList duranteDestino;
        auto destino = e.crear([&](Fase f) {
            if (f == Fase::Renombrar) {
                durante = entradas(e.privada());
                duranteDestino = entradas(e.destino);
            }
            return 0;
        });
        auto r = destino->reservar(e.destino, QStringLiteral("Facturas 2026-01.zip"));
        QVERIFY(r);
        auto parcial = std::move(r).valor();
        QVERIFY(!parcial->enCarpetaDestino());
        QCOMPARE(modo(e.privada()) & 07777, 0700u);
        QCOMPARE(modo(QFileInfo(e.privada()).absolutePath()) & 07777, 0700u);
        const QStringList privadas = entradas(e.privada());
        QCOMPARE(privadas.size(), 1);
        QVERIFY(exportacion::nombres::esParcialExportacion(privadas.first()));
        QCOMPARE(modo(QDir(e.privada()).filePath(privadas.first())), static_cast<unsigned>(S_IFREG | 0600));
        QCOMPARE(entradas(e.destino), QStringList{QStringLiteral("previo.zip")});

        const QByteArray cuerpo(70000, 'z');
        QVERIFY(!parcial->dispositivo().isSequential());
        QVERIFY(escribirContenido(parcial->dispositivo(), cuerpo));
        QVERIFY(!parcial->errorEscritura());
        QCOMPARE(parcial->dispositivo().size(), static_cast<qint64>(esperado(cuerpo).size()));
        QVERIFY(parcial->dispositivo().seek(0));
        QCOMPARE(parcial->dispositivo().read(4), QByteArray("PK\x03\x04", 4));
        QVERIFY(parcial->sincronizar());

        auto p = parcial->publicar();
        QVERIFY(p);
        QVERIFY(!p.valor().advertenciaDurabilidad);
        QCOMPARE(p.valor().tamanoBytes, static_cast<quint64>(esperado(cuerpo).size()));
        QCOMPARE(durante, privadas);
        QCOMPARE(duranteDestino, QStringList{QStringLiteral("previo.zip")});

        const QString rutaFinal = QDir(e.destino).filePath(QStringLiteral("Facturas 2026-01.zip"));
        QCOMPARE(leer(rutaFinal), esperado(cuerpo));
        QCOMPARE(modo(rutaFinal), static_cast<unsigned>(S_IFREG | 0600));
        QVERIFY2(sinAtributosPropios(rutaFinal), qPrintable(atributos(rutaFinal).join(QLatin1Char(','))));
        QCOMPARE(entradas(e.privada()), QStringList());
        QCOMPARE(entradas(e.destino), (QStringList{QStringLiteral("Facturas 2026-01.zip"), QStringLiteral("previo.zip")}));
        QCOMPARE(leer(QDir(e.destino).filePath(QStringLiteral("previo.zip"))), QByteArray("previo"));
        QVERIFY(!QFileInfo::exists(e.registro()));
        // Terminal: publicar de nuevo o descartar no tocan el ZIP.
        QCOMPARE(parcial->publicar().error().codigo, C::Estado);
        parcial->descartar();
        parcial.reset();
        QCOMPARE(leer(rutaFinal), esperado(cuerpo));
    }

    // Un atributo extendido puesto en el .partial no llega al ZIP final.
    void retiraAtributosExtendidos()
    {
        Entorno e;
        QVERIFY(e.preparar());
        auto destino = e.crear();
        auto r = destino->reservar(e.destino, QStringLiteral("a.zip"));
        QVERIFY(r);
        auto parcial = std::move(r).valor();
        const QString ruta = QDir(e.privada()).filePath(entradas(e.privada()).first());
        QCOMPARE(::setxattr(QFile::encodeName(ruta).constData(), "com.example.sintetico", "x", 1, 0, 0), 0);
        QVERIFY(tamanoXattr(ruta) > 0);
        QCOMPARE(parcial->dispositivo().write("datos"), 5);
        QVERIFY(parcial->publicar());
        const QString zip = QDir(e.destino).filePath(QStringLiteral("a.zip"));
        QVERIFY2(sinAtributosPropios(zip), qPrintable(atributos(zip).join(QLatin1Char(','))));
        QVERIFY(!atributos(zip).contains(QStringLiteral("com.example.sintetico")));
    }

    void fallas_data()
    {
        QTest::addColumn<Fase>("fase");
        QTest::addColumn<int>("codigoErrno");
        QTest::addColumn<int>("codigo");
        QTest::addColumn<bool>("otroVolumen");
        const struct {
            const char* nombre;
            Fase fase;
            int e;
            C c;
        } casos[] = {
            {"carpeta_privada", Fase::CrearCarpetaPrivada, EIO, C::Creacion},
            {"crear", Fase::Crear, EIO, C::Creacion},
            {"crear_sin_permiso", Fase::Crear, EACCES, C::SinPermiso},
            {"escribir", Fase::Escribir, EIO, C::Escritura},
            {"disco_lleno", Fase::Escribir, ENOSPC, C::EspacioInsuficiente},
            {"fullfsync", Fase::Sincronizar, EIO, C::Sincronizacion},
            {"cerrar", Fase::Cerrar, EIO, C::Cierre},
            {"rename", Fase::Renombrar, EIO, C::Publicacion},
            {"rename_excl_no_soportado", Fase::Renombrar, ENOTSUP, C::ExclusivoNoSoportado},
            {"rename_disco_lleno", Fase::Renombrar, ENOSPC, C::EspacioInsuficiente},
        };
        for (const auto& c : casos) {
            QTest::addRow("%s_local", c.nombre) << c.fase << c.e << static_cast<int>(c.c) << false;
            QTest::addRow("%s_otro_volumen", c.nombre) << c.fase << c.e << static_cast<int>(c.c) << true;
        }
    }

    // Toda falla antes del rename: sin ZIP final, sin .partial, el archivo
    // previo con ese nombre... no existe (se reserva solo si no existe) y uno
    // ajeno en la carpeta queda intacto.
    void fallas()
    {
        QFETCH(Fase, fase);
        QFETCH(int, codigoErrno);
        QFETCH(int, codigo);
        QFETCH(bool, otroVolumen);
        Entorno e;
        QVERIFY(e.preparar());
        QVERIFY(escribir(QDir(e.destino).filePath(QStringLiteral("ajeno.zip")), "ajeno"));
        auto destino = e.crear([&](Fase f) { return f == fase ? codigoErrno : 0; }, otroVolumen);
        auto r = destino->reservar(e.destino, QStringLiteral("final.zip"));
        if (fase == Fase::CrearCarpetaPrivada || fase == Fase::Crear) {
            QVERIFY(!r);
            QCOMPARE(static_cast<int>(r.error().codigo), codigo);
            QCOMPARE(r.error().errnoSistema, codigoErrno);
        } else {
            QVERIFY(r);
            auto parcial = std::move(r).valor();
            QCOMPARE(parcial->enCarpetaDestino(), otroVolumen);
            const bool escrito = parcial->dispositivo().write("contenido") == 9;
            QCOMPARE(escrito, fase != Fase::Escribir);
            if (fase == Fase::Escribir) {
                QVERIFY(parcial->errorEscritura());
                QCOMPARE(static_cast<int>(parcial->errorEscritura()->codigo), codigo);
            }
            auto p = parcial->publicar();
            QVERIFY(!p);
            QCOMPARE(static_cast<int>(p.error().codigo), codigo);
            QCOMPARE(p.error().errnoSistema, codigoErrno);
        }
        QCOMPARE(entradas(e.destino), QStringList{QStringLiteral("ajeno.zip")});
        QCOMPARE(leer(QDir(e.destino).filePath(QStringLiteral("ajeno.zip"))), QByteArray("ajeno"));
        QCOMPARE(entradas(e.privada()), QStringList());
        QVERIFY(!QFileInfo::exists(e.registro())); // otro volumen: retirado del registro
    }

    // Cancelar en cada fase previa al rename (descartar o destruir).
    void cancelarAntesDelRename()
    {
        for (const bool otroVolumen : {false, true}) {
            for (int punto = 0; punto < 4; ++punto) {
                Entorno e;
                QVERIFY(e.preparar());
                auto destino = e.crear({}, otroVolumen);
                auto r = destino->reservar(e.destino, QStringLiteral("final.zip"));
                QVERIFY(r);
                auto parcial = std::move(r).valor();
                if (punto >= 1) {
                    QCOMPARE(parcial->dispositivo().write("abc"), 3);
                }
                if (punto >= 2) {
                    QVERIFY(parcial->sincronizar());
                }
                if (punto == 3) {
                    QVERIFY(parcial->truncar(1));
                    QCOMPARE(parcial->dispositivo().size(), qint64(1));
                }
                if (punto % 2 == 0) {
                    parcial->descartar();
                    parcial->descartar(); // idempotente
                }
                parcial.reset();
                QCOMPARE(entradas(e.destino), QStringList());
                QCOMPARE(entradas(e.privada()), QStringList());
                QVERIFY(!QFileInfo::exists(e.registro()));
            }
        }
    }

    // Solo falla el fsync de una carpeta tras el rename: publicado con advertencia.
    void fsyncDeCarpetaTrasRename_data()
    {
        QTest::addColumn<Fase>("fase");
        QTest::addColumn<bool>("otroVolumen");
        QTest::newRow("destino_local") << Fase::SincronizarCarpetaDestino << false;
        QTest::newRow("privada_local") << Fase::SincronizarCarpetaPrivada << false;
        QTest::newRow("destino_otro_volumen") << Fase::SincronizarCarpetaDestino << true;
    }
    void fsyncDeCarpetaTrasRename()
    {
        QFETCH(Fase, fase);
        QFETCH(bool, otroVolumen);
        Entorno e;
        QVERIFY(e.preparar());
        auto destino = e.crear([&](Fase f) { return f == fase ? EIO : 0; }, otroVolumen);
        auto r = destino->reservar(e.destino, QStringLiteral("final.zip"));
        QVERIFY(r);
        auto parcial = std::move(r).valor();
        QCOMPARE(parcial->dispositivo().write("contenido"), 9);
        auto p = parcial->publicar();
        QVERIFY(p);
        QVERIFY(p.valor().advertenciaDurabilidad);
        QCOMPARE(p.valor().errnoDurabilidad, EIO);
        parcial.reset(); // no despublica
        QCOMPARE(entradas(e.destino), QStringList{QStringLiteral("final.zip")});
        QCOMPARE(leer(QDir(e.destino).filePath(QStringLiteral("final.zip"))), QByteArray("contenido"));
        QCOMPARE(entradas(e.privada()), QStringList());
    }

    // Nombre ya existente al reservar (archivo o enlace): no se crea nada.
    void destinoExistenteAlReservar()
    {
        Entorno e;
        QVERIFY(e.preparar());
        QVERIFY(escribir(QDir(e.destino).filePath(QStringLiteral("existe.zip")), "previo"));
        QCOMPARE(::symlink("/nonexistent-sintetico", QFile::encodeName(QDir(e.destino).filePath(QStringLiteral("enlace.zip"))).constData()), 0);
        auto destino = e.crear();
        for (const QString& n : {QStringLiteral("existe.zip"), QStringLiteral("enlace.zip")}) {
            auto r = destino->reservar(e.destino, n);
            QVERIFY(!r);
            QCOMPARE(r.error().codigo, C::DestinoExistente);
        }
        QCOMPARE(leer(QDir(e.destino).filePath(QStringLiteral("existe.zip"))), QByteArray("previo"));
        QVERIFY(!QFileInfo::exists(e.privada()));
    }

    // El nombre aparece entre reservar y el rename: no se reemplaza; el
    // .partial sigue y se publica con otro nombre.
    void colisionTardia()
    {
        for (const bool otroVolumen : {false, true}) {
            Entorno e;
            QVERIFY(e.preparar());
            const QString previo = QDir(e.destino).filePath(QStringLiteral("final.zip"));
            bool sembrado = false;
            auto destino = e.crear(
                [&](Fase f) {
                    if (f == Fase::Renombrar && !sembrado) {
                        sembrado = escribir(previo, "del usuario");
                    }
                    return 0;
                },
                otroVolumen);
            auto r = destino->reservar(e.destino, QStringLiteral("final.zip"));
            QVERIFY(r);
            auto parcial = std::move(r).valor();
            QCOMPARE(parcial->dispositivo().write("nuevo"), 5);
            auto p = parcial->publicar();
            QVERIFY(!p);
            QCOMPARE(p.error().codigo, C::DestinoExistente);
            QCOMPARE(leer(previo), QByteArray("del usuario"));
            QCOMPARE(entradas(otroVolumen ? e.destino : e.privada()).size(), otroVolumen ? 2 : 1);
            QCOMPARE(parcial->publicarComo(QStringLiteral("a/b.zip")).error().codigo, C::NombreInvalido);
            auto q = parcial->publicarComo(QStringLiteral("final (2).zip"));
            QVERIFY(q);
            QCOMPARE(leer(QDir(e.destino).filePath(QStringLiteral("final (2).zip"))), QByteArray("nuevo"));
            QCOMPARE(leer(previo), QByteArray("del usuario"));
            QCOMPARE(entradas(e.destino), (QStringList{QStringLiteral("final (2).zip"), QStringLiteral("final.zip")}));
            QCOMPARE(entradas(e.privada()), QStringList());
            QVERIFY(!QFileInfo::exists(e.registro()));
        }
    }

    void nombresYCarpetasInvalidos()
    {
        Entorno e;
        QVERIFY(e.preparar());
        auto destino = e.crear();
        for (const QString& n : {QString(), QStringLiteral("a/b.zip"), QStringLiteral(".oculto.zip"),
                                 QStringLiteral("con\ncontrol.zip"), QString(252, QLatin1Char('n')) + QStringLiteral(".zip")}) {
            auto r = destino->reservar(e.destino, n);
            QVERIFY(!r);
            QCOMPARE(r.error().codigo, C::NombreInvalido);
        }
        auto r = destino->reservar(e.tmp.filePath(QStringLiteral("no-existe")), QStringLiteral("a.zip"));
        QCOMPARE(r.error().codigo, C::CarpetaNoDisponible);
        QVERIFY(escribir(e.tmp.filePath(QStringLiteral("archivo")), "x"));
        r = destino->reservar(e.tmp.filePath(QStringLiteral("archivo")), QStringLiteral("a.zip"));
        QCOMPARE(r.error().codigo, C::NoEsCarpeta);
        r = destino->reservar(QStringLiteral("relativa"), QStringLiteral("a.zip"));
        QCOMPARE(r.error().codigo, C::CarpetaNoDisponible);
        QVERIFY(!QFileInfo::exists(e.privada()));
        QCOMPARE(entradas(e.destino), QStringList());
    }

    // Otro volumen: el registro (0600) existe con el .partial ANTES de
    // crearlo; publicar lo retira.
    void otroVolumenRegistraAntesDeCrear()
    {
        Entorno e;
        QVERIFY(e.preparar());
        QString parcialRegistrado;
        bool parcialYaExistia = true;
        auto destino = e.crear(
            [&](Fase f) {
                if (f == Fase::Crear) {
                    const auto doc = QJsonDocument::fromJson(leer(e.registro())).object();
                    const auto d = doc.value(QStringLiteral("destinos")).toArray();
                    if (d.size() == 1) {
                        parcialRegistrado = d.at(0).toObject().value(QStringLiteral("parciales")).toArray().at(0).toString();
                    }
                    parcialYaExistia = QFileInfo::exists(QDir(e.destino).filePath(parcialRegistrado));
                }
                return 0;
            },
            true);
        auto r = destino->reservar(e.destino, QStringLiteral("final.zip"));
        QVERIFY(r);
        QVERIFY(exportacion::nombres::esParcialExportacion(parcialRegistrado));
        QVERIFY(!parcialYaExistia);
        QCOMPARE(entradas(e.destino), QStringList{parcialRegistrado});
        QCOMPARE(modo(e.registro()), static_cast<unsigned>(S_IFREG | 0600));
        QCOMPARE(modo(QFileInfo(e.registro()).absolutePath()) & 07777, 0700u);
        QCOMPARE(entradas(e.privada()), QStringList());
        auto parcial = std::move(r).valor();
        QVERIFY(parcial->enCarpetaDestino());
        QCOMPARE(parcial->dispositivo().write("x"), 1);
        QVERIFY(parcial->publicar());
        QCOMPARE(entradas(e.destino), QStringList{QStringLiteral("final.zip")});
        QCOMPARE(modo(QDir(e.destino).filePath(QStringLiteral("final.zip"))), static_cast<unsigned>(S_IFREG | 0600));
        QVERIFY(!QFileInfo::exists(e.registro()));
    }

    void otroVolumenFallaElRegistro()
    {
        Entorno e;
        QVERIFY(e.preparar());
        auto destino = e.crear([](Fase f) { return f == Fase::Registrar ? EIO : 0; }, true);
        auto r = destino->reservar(e.destino, QStringLiteral("final.zip"));
        QVERIFY(!r);
        QCOMPARE(r.error().codigo, C::Registro);
        QCOMPARE(entradas(e.destino), QStringList());
        QVERIFY(!QFileInfo::exists(e.registro()));
    }

    // Limpieza local: solo archivos regulares con el patron exacto; enlaces,
    // ajenos, directorios y nombres fuera de patron permanecen; un .partial
    // vivo de este proceso no se toca.
    void limpiezaLocal()
    {
        Entorno e;
        QVERIFY(e.preparar());
        auto destino = e.crear();
        auto vivo = destino->reservar(e.destino, QStringLiteral("vivo.zip"));
        QVERIFY(vivo);
        const QString nombreVivo = entradas(e.privada()).first();
        const QString objetivo = e.tmp.filePath(QStringLiteral("objetivo.txt"));
        QVERIFY(escribir(objetivo, "objetivo"));
        QVERIFY(escribir(QDir(e.privada()).filePath(kParcialFijo), "huerfano"));
        const QString enlace = QStringLiteral(".archivo-fiscal-exportacion-fedcba9876543210.partial");
        QCOMPARE(::symlink(QFile::encodeName(objetivo).constData(), QFile::encodeName(QDir(e.privada()).filePath(enlace)).constData()), 0);
        // Fuera de patron (hex en mayusculas); distinto de los demas aun en APFS
        // sin distincion de mayusculas.
        const QString mayusculas = QStringLiteral(".archivo-fiscal-exportacion-AAAAAAAAAAAAAAAB.partial");
        QVERIFY(escribir(QDir(e.privada()).filePath(mayusculas), "x"));
        QVERIFY(escribir(QDir(e.privada()).filePath(QStringLiteral("notas.txt")), "x"));
        const QString directorio = QStringLiteral(".archivo-fiscal-exportacion-00000000000000aa.partial");
        QVERIFY(QDir().mkpath(QDir(e.privada()).filePath(directorio)));

        auto l = destino->limpiarParcialesPropios();
        QVERIFY(l);
        QCOMPARE(l.valor().borrados, 1);
        QCOMPARE(l.valor().conservados, 2); // enlace y directorio
        QStringList quedan{nombreVivo, enlace, mayusculas, QStringLiteral("notas.txt"), directorio};
        quedan.sort();
        QCOMPARE(entradas(e.privada()), quedan);
        QCOMPARE(leer(objetivo), QByteArray("objetivo"));
        // El vivo se sigue publicando.
        auto p = std::move(vivo).valor();
        QCOMPARE(p->dispositivo().write("v"), 1);
        QVERIFY(p->publicar());

        // Sin carpeta privada: no crea nada.
        Entorno vacio;
        QVERIFY(vacio.preparar());
        auto l2 = vacio.crear()->limpiarParcialesPropios();
        QVERIFY(l2);
        QCOMPARE(l2.valor().borrados, 0);
        QVERIFY(!QFileInfo::exists(QFileInfo(vacio.privada()).absolutePath()));
    }

    // Terminacion simulada con un .partial en otro volumen (el objeto nunca
    // se destruye): el siguiente "arranque" lo borra por el registro; un
    // .partial ajeno con el mismo patron y no registrado permanece.
    void limpiezaOtroVolumenTrasTerminacion()
    {
        Entorno e;
        QVERIFY(e.preparar());
        auto proceso = e.crear({}, true);
        auto r = proceso->reservar(e.destino, QStringLiteral("final.zip"));
        QVERIFY(r);
        ArchivoParcialExportacion* abandonado = std::move(r).valor().release(); // "muere" el proceso
        QCOMPARE(abandonado->dispositivo().write("a medias"), 8);
        const QString nombre = entradas(e.destino).first();
        const QString ajeno = QStringLiteral(".archivo-fiscal-exportacion-aaaaaaaaaaaaaaaa.partial");
        QVERIFY(escribir(QDir(e.destino).filePath(ajeno), "otra instalacion"));
        // El mismo proceso no lo toca mientras vive.
        QCOMPARE(proceso->limpiarParcialesPropios().valor().borrados, 0);
        QVERIFY(QFileInfo::exists(QDir(e.destino).filePath(nombre)));

        auto siguiente = e.crear();
        auto l = siguiente->limpiarParcialesPropios();
        QVERIFY(l);
        QCOMPARE(l.valor().borrados, 1);
        QCOMPARE(entradas(e.destino), QStringList{ajeno});
        QVERIFY(!QFileInfo::exists(e.registro()));
        delete abandonado; // ya no existe: descartar() no encuentra nada que borrar
        QCOMPARE(entradas(e.destino), QStringList{ajeno});
    }

    // Carpeta registrada con otra identidad (reemplazada) o no disponible:
    // nada se toca y la entrada se conserva; un enlace registrado no se sigue.
    void limpiezaOtroVolumenIdentidad()
    {
        Entorno e;
        QVERIFY(e.preparar());
        const QString otra = e.tmp.filePath(QStringLiteral("reemplazada"));
        QVERIFY(QDir().mkpath(otra));
        QVERIFY(escribir(QDir(otra).filePath(kParcialFijo), "no tocar"));
        QVERIFY(escribir(QDir(e.destino).filePath(kParcialFijo), "huerfano"));
        const QString enlace = QStringLiteral(".archivo-fiscal-exportacion-fedcba9876543210.partial");
        const QString objetivo = e.tmp.filePath(QStringLiteral("objetivo.txt"));
        QVERIFY(escribir(objetivo, "objetivo"));
        QCOMPARE(::symlink(QFile::encodeName(objetivo).constData(), QFile::encodeName(QDir(e.destino).filePath(enlace)).constData()), 0);
        struct stat st {};
        QCOMPARE(::stat(QFile::encodeName(e.destino).constData(), &st), 0);
        const auto entrada = [](const QString& carpeta, quint64 dev, quint64 ino, const QStringList& parciales) {
            QJsonObject o;
            o.insert(QStringLiteral("carpeta"), carpeta);
            o.insert(QStringLiteral("dispositivo"), QString::number(dev));
            o.insert(QStringLiteral("inodo"), QString::number(ino));
            o.insert(QStringLiteral("parciales"), QJsonArray::fromStringList(parciales));
            o.insert(QStringLiteral("actualizado"), QString::number(QDateTime::currentSecsSinceEpoch()));
            return o;
        };
        const auto dev = static_cast<quint64>(static_cast<unsigned>(st.st_dev));
        const auto ino = static_cast<quint64>(st.st_ino);
        QJsonArray destinos;
        destinos.append(entrada(e.destino, dev, ino, {kParcialFijo, enlace}));
        destinos.append(entrada(otra, dev, ino, {kParcialFijo}));            // identidad de otra carpeta
        destinos.append(entrada(e.tmp.filePath(QStringLiteral("desmontada")), 1, 2, {kParcialFijo})); // no disponible
        QJsonObject raiz;
        raiz.insert(QStringLiteral("formato"), 1);
        raiz.insert(QStringLiteral("destinos"), destinos);
        QVERIFY(escribir(e.registro(), QJsonDocument(raiz).toJson()));

        auto l = e.crear()->limpiarParcialesPropios();
        QVERIFY(l);
        QCOMPARE(l.valor().borrados, 1);
        QCOMPARE(l.valor().conservados, 3); // enlace + 2 de carpetas no verificables
        QCOMPARE(entradas(e.destino), QStringList{enlace});
        QCOMPARE(leer(objetivo), QByteArray("objetivo"));
        QCOMPARE(leer(QDir(otra).filePath(kParcialFijo)), QByteArray("no tocar"));
        const auto quedan = QJsonDocument::fromJson(leer(e.registro())).object().value(QStringLiteral("destinos")).toArray();
        QCOMPARE(quedan.size(), 2);
        for (const auto& v : quedan) {
            QVERIFY(v.toObject().value(QStringLiteral("carpeta")).toString() != e.destino);
        }
    }

    // D20: con datos sonda (RFC, /Users/usuario, nombre hostil) los logs solo
    // tienen fases, conteos y codigos.
    void logsSinRutasNiNombres()
    {
        {
            QMutexLocker l(&g_mutexLog);
            g_log.clear();
        }
        Entorno e;
        QVERIFY(e.preparar(QStringLiteral("Users/usuario/XAXX010101000")));
        const QString hostil = QStringLiteral("XAXX010101000 $(rm -rf ~) 'hostil' \\ \"x\" %s %n.zip");
        for (const Fase fase : {Fase::Crear, Fase::Escribir, Fase::Sincronizar, Fase::Cerrar, Fase::Renombrar,
                                Fase::SincronizarCarpetaDestino, Fase::Registrar}) {
            for (const bool otro : {false, true}) {
                auto destino = e.crear([&](Fase f) { return f == fase ? ENOSPC : 0; }, otro);
                auto r = destino->reservar(e.destino, hostil);
                if (r) {
                    auto p = std::move(r).valor();
                    (void)p->dispositivo().write("contenido");
                    if (auto pub = p->publicar(); pub) {
                        QVERIFY(QFile::remove(QDir(e.destino).filePath(hostil)));
                    }
                }
            }
        }
        QVERIFY(escribir(QDir(e.destino).filePath(hostil), "previo"));
        auto destino = e.crear();
        QVERIFY(!destino->reservar(e.destino, hostil));
        QVERIFY(escribir(QDir(e.privada()).filePath(kParcialFijo), "x"));
        QVERIFY(destino->limpiarParcialesPropios());

        const QStringList log = logCapturado();
        QVERIFY(log.size() >= 8);
        bool hayErrno = false;
        for (const QString& linea : log) {
            for (const QString& sonda : {QStringLiteral("usuario"), QStringLiteral("XAXX010101000"),
                                         QStringLiteral("hostil"), QStringLiteral("archivo-fiscal"),
                                         QStringLiteral(".partial"), QStringLiteral(".zip"), e.tmp.path(),
                                         QStringLiteral("/")}) {
                QVERIFY2(!linea.contains(sonda), qPrintable(QStringLiteral("sonda en log: ") + sonda));
            }
            hayErrno = hayErrno || linea.contains(QStringLiteral("errno: 28"));
        }
        QVERIFY(hayErrno);
    }


    // Seguridad 1: si el borrado del .partial falla al descartar (p. ej. disco
    // desconectado), sigue registrado; la limpieza lo reintenta y solo lo
    // retira del registro cuando lo borra.
    void descartarConBorradoFallidoConservaElRegistro()
    {
        Entorno e;
        QVERIFY(e.preparar());
        bool fallarBorrado = true;
        auto destino = e.crear([&](Fase f) { return (f == Fase::Borrar && fallarBorrado) ? EIO : 0; }, true);
        auto r = destino->reservar(e.destino, QStringLiteral("final.zip"));
        QVERIFY(r);
        auto parcial = std::move(r).valor();
        const QString nombre = entradas(e.destino).first();
        parcial->descartar();
        QCOMPARE(entradas(e.destino), QStringList{nombre});
        QVERIFY(leer(e.registro()).contains(nombre.toLatin1()));
        // Limpieza con el borrado aun fallando: conservado y registrado.
        auto l1 = destino->limpiarParcialesPropios();
        QVERIFY(l1);
        QCOMPARE(l1.valor().borrados, 0);
        QCOMPARE(l1.valor().conservados, 1);
        QVERIFY(leer(e.registro()).contains(nombre.toLatin1()));
        // Volumen "de vuelta": la limpieza lo borra y lo retira.
        fallarBorrado = false;
        auto l2 = destino->limpiarParcialesPropios();
        QVERIFY(l2);
        QCOMPARE(l2.valor().borrados, 1);
        QCOMPARE(entradas(e.destino), QStringList());
        QVERIFY(!QFileInfo::exists(e.registro()));
    }

    // Seguridad 2: un registro ilegible se aparta (0600) en vez de perderse.
    void registroIlegibleSeAparta()
    {
        {
            QMutexLocker l(&g_mutexLog);
            g_log.clear();
        }
        Entorno e;
        QVERIFY(e.preparar());
        QVERIFY(escribir(e.registro(), "{basura"));
        auto destino = e.crear({}, true);
        auto r = destino->reservar(e.destino, QStringLiteral("final.zip"));
        QVERIFY(r);
        const QString apartado1 = QDir(QFileInfo(e.registro()).absolutePath()).filePath(QStringLiteral("parciales-destinos.ilegible-1.json"));
        QCOMPARE(leer(apartado1), QByteArray("{basura"));
        QCOMPARE(modo(apartado1), static_cast<unsigned>(S_IFREG | 0600));
        const auto doc = QJsonDocument::fromJson(leer(e.registro())).object();
        QCOMPARE(doc.value(QStringLiteral("destinos")).toArray().size(), 1);
        auto parcial = std::move(r).valor();
        QCOMPARE(parcial->dispositivo().write("x"), 1);
        QVERIFY(parcial->publicar());
        // La limpieza tambien aparta (otro numero) e ignora los apartados.
        QVERIFY(escribir(e.registro(), "otra basura"));
        auto l = destino->limpiarParcialesPropios();
        QVERIFY(l);
        const QString apartado2 = QDir(QFileInfo(e.registro()).absolutePath()).filePath(QStringLiteral("parciales-destinos.ilegible-2.json"));
        QCOMPARE(leer(apartado2), QByteArray("otra basura"));
        QCOMPARE(leer(apartado1), QByteArray("{basura"));
        QVERIFY(!QFileInfo::exists(e.registro()));
        QMutexLocker lg(&g_mutexLog);
        bool hayCodigo = false;
        for (const QString& linea : std::as_const(g_log)) {
            QVERIFY2(!linea.contains(QLatin1Char('/')) && !linea.contains(QLatin1String("ilegible-")), qPrintable(linea));
            hayCodigo = hayCodigo || linea.contains(QLatin1String("codigo: registro_ilegible"));
        }
        QVERIFY(hayCodigo);
    }

    // Seguridad 3: una limpieza entre el registro y la creacion del .partial
    // no lo trata como huerfano (ya esta activo).
    void activoAntesDeCrear()
    {
        Entorno e;
        QVERIFY(e.preparar());
        DestinoExportacionPosix* yo = nullptr;
        int borradosEnMedio = -1;
        auto destino = e.crear(
            [&](Fase f) {
                if (f == Fase::Crear && yo) {
                    borradosEnMedio = yo->limpiarParcialesPropios().valor().borrados;
                }
                return 0;
            },
            true);
        yo = destino.get();
        auto r = destino->reservar(e.destino, QStringLiteral("final.zip"));
        QVERIFY(r);
        QCOMPARE(borradosEnMedio, 0);
        const QString nombre = entradas(e.destino).first();
        QVERIFY(leer(e.registro()).contains(nombre.toLatin1())); // sigue registrado
        yo = nullptr;
        std::move(r).valor()->descartar();
        QCOMPARE(entradas(e.destino), QStringList());
        QVERIFY(!QFileInfo::exists(e.registro()));
    }

    // Seguridad 4: entradas de carpetas no disponibles caducan a los 90 dias.
    void caducidadDelRegistro()
    {
        Entorno e;
        QVERIFY(e.preparar());
        const QDateTime ahora = QDateTime(QDate(2026, 10, 24), QTime(12, 0), QTimeZone::UTC);
        const auto entrada = [](const QString& carpeta, const QDateTime& cuando) {
            QJsonObject o;
            o.insert(QStringLiteral("carpeta"), carpeta);
            o.insert(QStringLiteral("dispositivo"), QStringLiteral("1"));
            o.insert(QStringLiteral("inodo"), QStringLiteral("2"));
            o.insert(QStringLiteral("parciales"), QJsonArray{kParcialFijo});
            o.insert(QStringLiteral("actualizado"), QString::number(cuando.toSecsSinceEpoch()));
            return o;
        };
        QJsonObject raiz;
        raiz.insert(QStringLiteral("formato"), 1);
        raiz.insert(QStringLiteral("destinos"),
                    QJsonArray{entrada(QStringLiteral("/Volumes/USB-sintetico/vieja"), ahora.addDays(-91)),
                               entrada(QStringLiteral("/Volumes/USB-sintetico/reciente"), ahora.addDays(-89))});
        QVERIFY(escribir(e.registro(), QJsonDocument(raiz).toJson()));
        DestinoExportacionPosix::Opciones o;
        o.directorioDatos = e.datos;
        o.reloj = [ahora] { return ahora; };
        DestinoExportacionPosix destino(o);
        auto l = destino.limpiarParcialesPropios();
        QVERIFY(l);
        QCOMPARE(l.valor().conservados, 1);
        const auto quedan = QJsonDocument::fromJson(leer(e.registro())).object().value(QStringLiteral("destinos")).toArray();
        QCOMPARE(quedan.size(), 1);
        QCOMPARE(quedan.at(0).toObject().value(QStringLiteral("carpeta")).toString(), QStringLiteral("/Volumes/USB-sintetico/reciente"));
    }

    // Seguridad 5: una ACL heredable de la carpeta destino no llega al ZIP.
    void retiraAclHeredada()
    {
        Entorno e;
        QVERIFY(e.preparar());
        const Salida_ r = ejecutarChmod({QStringLiteral("+a"), QStringLiteral("everyone allow read,file_inherit"), e.destino});
        if (r != 0) {
            QSKIP("chmod +a no disponible en este volumen");
        }
        bool heredo = false;
        auto destino = e.crear(
            [&](Fase f) {
                if (f == Fase::Sincronizar) {
                    for (const QString& n : entradas(e.destino)) {
                        heredo = heredo || tieneAcl(QDir(e.destino).filePath(n));
                    }
                }
                return 0;
            },
            true); // otro volumen: el .partial se crea en la carpeta destino
        auto res = destino->reservar(e.destino, QStringLiteral("final.zip"));
        QVERIFY(res);
        auto parcial = std::move(res).valor();
        QCOMPARE(parcial->dispositivo().write("x"), 1);
        QVERIFY(parcial->publicar());
        QVERIFY(heredo); // la prueba es significativa
        QVERIFY(!tieneAcl(QDir(e.destino).filePath(QStringLiteral("final.zip"))));
        QCOMPARE(modo(QDir(e.destino).filePath(QStringLiteral("final.zip"))), static_cast<unsigned>(S_IFREG | 0600));
    }

    // Seguridad 6: no se exporta al directorio de datos ni dentro de el
    // (tampoco a traves de un enlace).
    void rechazaCarpetaBajoDatos()
    {
        Entorno e;
        QVERIFY(e.preparar());
        const QString sub = QDir(e.datos).filePath(QStringLiteral("sub/mas"));
        QVERIFY(QDir().mkpath(sub));
        const QString enlace = e.tmp.filePath(QStringLiteral("enlace-a-datos"));
        QCOMPARE(::symlink(QFile::encodeName(sub).constData(), QFile::encodeName(enlace).constData()), 0);
        auto destino = e.crear();
        for (const QString& carpeta : {e.datos, sub, enlace, QDir(e.destino).filePath(QStringLiteral("../datos"))}) {
            auto r = destino->reservar(carpeta, QStringLiteral("final.zip"));
            QVERIFY2(!r, qPrintable(carpeta));
            QCOMPARE(r.error().codigo, C::CarpetaNoPermitida);
        }
        QVERIFY(!QFileInfo::exists(QFileInfo(e.privada()).absolutePath()));
        QCOMPARE(entradas(sub), QStringList());
        QVERIFY(destino->reservar(e.destino, QStringLiteral("final.zip"))); // carpeta hermana: permitida
    }

    // El fake respeta el contrato basico (lo usaran las pruebas de T026.2).
    void fakeRespetaElContrato()
    {
        fakes::FakeDestinoExportacion f;
        const QString carpeta = QStringLiteral("/Users/usuario/Exportaciones");
        f.ponerArchivo(carpeta, QStringLiteral("existe.zip"), "previo");
        QCOMPARE(f.reservar(carpeta, QStringLiteral("existe.zip")).error().codigo, C::DestinoExistente);
        {
            auto r = f.reservar(carpeta, QStringLiteral("a.zip"));
            QVERIFY(r);
            auto p = std::move(r).valor();
            QVERIFY(p->dispositivo().write("1234") == 4 && p->dispositivo().seek(0) && p->dispositivo().write("X") == 1);
            f.antesDeRenombrar = [&] { f.ponerArchivo(carpeta, QStringLiteral("a.zip"), "tarde"); };
            QCOMPARE(p->publicar().error().codigo, C::DestinoExistente);
            f.antesDeRenombrar = {};
            QVERIFY(p->publicarComo(QStringLiteral("b.zip")));
        }
        QCOMPARE(*f.archivo(carpeta, QStringLiteral("b.zip")), QByteArray("X234"));
        QCOMPARE(*f.archivo(carpeta, QStringLiteral("a.zip")), QByteArray("tarde"));
        f.falla = fakes::FakeDestinoExportacion::Falla::DiscoLleno;
        f.limiteEscritura = 2;
        {
            auto p = std::move(f.reservar(carpeta, QStringLiteral("c.zip"))).valor();
            QCOMPARE(p->dispositivo().write("abc"), -1);
            QCOMPARE(p->publicar().error().codigo, C::EspacioInsuficiente);
        }
        f.falla = fakes::FakeDestinoExportacion::Falla::SincronizarCarpeta;
        {
            auto p = std::move(f.reservar(carpeta, QStringLiteral("d.zip"))).valor();
            QVERIFY(p->publicar().valor().advertenciaDurabilidad);
        }
        QVERIFY(!f.archivo(carpeta, QStringLiteral("c.zip")));
        QCOMPARE(f.parcialesVivos(), 0);
        QCOMPARE(f.descartes(), 1);
    }

    // El root real lanza la limpieza en iniciarMonitoreo() y conserva su future.
    void rootLimpiaAlArrancar()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString datos = tmp.filePath(QStringLiteral("datos"));
        const QString privada = DestinoExportacionPosix::carpetaParciales(datos);
        QVERIFY(escribir(QDir(privada).filePath(kParcialFijo), "huerfano"));
        QVERIFY(escribir(QDir(privada).filePath(QStringLiteral("ajeno.txt")), "ajeno"));
        const QString objetivo = tmp.filePath(QStringLiteral("objetivo.txt"));
        QVERIFY(escribir(objetivo, "objetivo"));
        const QString enlace = QStringLiteral(".archivo-fiscal-exportacion-fedcba9876543210.partial");
        QCOMPARE(::symlink(QFile::encodeName(objetivo).constData(), QFile::encodeName(QDir(privada).filePath(enlace)).constData()), 0);

        AppBootstrapper::Opciones opciones;
        opciones.directorioDatos = datos;
        const auto arranque = AppBootstrapper(opciones).preparar();
        QVERIFY(arranque);
        fakes::FakeSecretStore secretos;
        fakes::FakeOperacionesSat sat;
        OpcionesMonitoreo m;
        m.operacionesSat = &sat;
        AppCompositionRoot root(arranque.valor().rutaBase, secretos, m);
        QVERIFY(QFileInfo::exists(QDir(privada).filePath(kParcialFijo))); // construir no limpia
        root.iniciarMonitoreo();
        auto limpieza = root.limpiezaExportacionInicial();
        QVERIFY(QTest::qWaitFor([&] { return limpieza.isFinished(); }, 30000));
        QCOMPARE(limpieza.result(), 1);
        QStringList quedan{QStringLiteral("ajeno.txt"), enlace};
        quedan.sort();
        QCOMPARE(entradas(privada), quedan);
        QCOMPARE(leer(objetivo), QByteArray("objetivo"));
        // El destino del root funciona.
        const QString carpeta = tmp.filePath(QStringLiteral("exportaciones"));
        QVERIFY(QDir().mkpath(carpeta));
        auto r = root.destinoExportacion().reservar(carpeta, QStringLiteral("a.zip"));
        QVERIFY(r);
        auto p = std::move(r).valor();
        auto escritor = root.fabricaEscritorZip()();
        QVERIFY(escritor);
        QVERIFY(escritor->abrir(*p, QDateTime::currentDateTimeUtc()));
        QVERIFY(escritor->agregar(QStringLiteral("omitidas.txt"), "z\n"));
        QVERIFY(escritor->cerrar());
        QVERIFY(p->publicar());
        QCOMPARE(entradas(carpeta), QStringList{QStringLiteral("a.zip")});
    }
};

int main(int argc, char* argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    TestDestinoExportacionPosix prueba;
    return QTest::qExec(&prueba, argc, argv);
}

#include "TestDestinoExportacionPosix.moc"
