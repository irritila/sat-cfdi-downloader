// T025.2: captura consistente para el respaldo. SQLite real, OperacionExecutor
// real (SAT falso, sin red) y contenedor AFB1 de T025.1, sobre QTemporaryDir
// con datos sinteticos (XAXX010101000, /Users/usuario).

#include "SqlitePruebasComun.h"

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"
#include "app_core/FuenteRespaldoLocal.h"
#include "application/logging/RegexLogSanitizer.h"
#include "application/operaciones/OperacionExecutor.h"
#include "application/respaldo/AlimentadorRespaldo.h"
#include "infrastructure/crypto/ContenedorRespaldoOpenSsl.h"
#include "infrastructure/persistence/sqlite/SqliteCopiaRespaldo.h"
#include "ports/repositories/OperacionesSolicitudRepository.h"

#include "fakes/FakeOperacionesSat.h"
#include "fakes/FakeProgramador.h"
#include "fakes/FakeSecretStore.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QThread>
#include <QTimeZone>

#include <sys/stat.h>

#include <atomic>
#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;
using namespace satcfdi::respaldo;

namespace {

const QString kRfc = QStringLiteral("XAXX010101000");
const QUuid kInstalacion = QUuid(QStringLiteral("{6f1c2a7e-3b9d-4c5e-8f10-2a3b4c5d6e7f}"));
const QString kContrasena = QStringLiteral("contrasena-sintetica-1");

int modo(const QString& ruta)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(ruta).constData(), &st) == 0 ? static_cast<int>(st.st_mode & 07777) : -1;
}

QByteArray sha(const QString& ruta)
{
    QFile f(ruta);
    return f.open(QIODevice::ReadOnly) ? QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256) : QByteArray();
}

bool escribir(const QString& ruta, const QByteArray& datos)
{
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(datos) == datos.size();
}

QStringList archivosBajo(const QString& raiz)
{
    QStringList r;
    QDirIterator it(raiz, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        r.append(QDir(raiz).relativeFilePath(it.next()));
    }
    r.sort();
    return r;
}

// Abre `ruta` con una conexion propia y devuelve integrity_check y un escalar.
QString integridad(const QString& ruta, const QString& consulta = {}, QVariant* valor = nullptr)
{
    const QString nombre = QStringLiteral("prueba-") + uuid::generarCanonico();
    QString r;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), nombre);
        db.setDatabaseName(ruta);
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("PRAGMA integrity_check")) && q.next()) {
                r = q.value(0).toString();
            }
            if (!consulta.isEmpty() && valor && q.exec(consulta) && q.next()) {
                *valor = q.value(0);
            }
            q = QSqlQuery();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(nombre);
    return r;
}

// CopiaBaseDatos que registra en que hilo y cuando corrio (para comprobar que
// va en el ejecutor y detras de la operacion en curso).
class CopiaObservada final : public CopiaBaseDatos {
public:
    explicit CopiaObservada(CopiaBaseDatos& real) : m_real(real) {}
    Resultado<Exito, ErrorCaptura> copiarA(const QString& destino) override
    {
        hilo.store(QThread::currentThread());
        inicioMs.store(reloj.elapsed());
        return m_real.copiarA(destino);
    }
    QElapsedTimer reloj;
    std::atomic<QThread*> hilo{nullptr};
    std::atomic<qint64> inicioMs{-1};

private:
    CopiaBaseDatos& m_real;
};

class LectorQueFalla final : public LectorCopiaBaseDatos {
public:
    Resultado<ContenidoCopia, ErrorCaptura> leer(const QString&) override
    {
        return Resultado<ContenidoCopia, ErrorCaptura>::fallo(
            ErrorCaptura::de(ErrorCaptura::Categoria::Integridad, QStringLiteral("prueba")));
    }
};

// Base real + ejecutor real + fuente de captura.
struct Entorno {
    QTemporaryDir dir;
    QString datos;
    QString raizPaquetes;
    std::unique_ptr<SqlitePersistencia> p;
    std::unique_ptr<SqliteCopiaRespaldo> copia;
    std::shared_ptr<LectorCopiaBaseDatos> lector;
    std::unique_ptr<CopiaObservada> observada;
    RegexLogSanitizer sanitizer;
    fakes::FakeOperacionesSat sat;
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 21), QTime(12, 0), QTimeZone::UTC)};
    std::unique_ptr<fakes::FakeProgramador> programador;
    std::unique_ptr<OperacionExecutor> ejecutor;
    std::unique_ptr<FuenteRespaldoLocal> fuente;

    bool preparar(std::shared_ptr<LectorCopiaBaseDatos> lectorInyectado = nullptr)
    {
        datos = dir.filePath(QStringLiteral("datos"));
        raizPaquetes = QDir(datos).filePath(QStringLiteral("paquetes"));
        if (!dir.isValid() || !QDir().mkpath(raizPaquetes)) {
            return false;
        }
        const QString base = QDir(datos).filePath(QStringLiteral("satcfdi.sqlite3"));
        if (!inicializarBaseSqlite(base)) {
            return false;
        }
        p = std::make_unique<SqlitePersistencia>(base);
        copia = std::make_unique<SqliteCopiaRespaldo>(*p);
        observada = std::make_unique<CopiaObservada>(*copia);
        observada->reloj.start();
        programador = std::make_unique<fakes::FakeProgramador>(reloj);
        SqlitePersistencia* persistencia = p.get();
        PuertosEjecutor puertos{p->solicitudes(), p->logs(), p->operaciones(), p->unidadDeTrabajo(), sanitizer,
                                sat, [persistencia] { persistencia->cerrarConexionDelHiloActual(); },
                                &p->paquetes(), nullptr, observada.get()};
        ejecutor = std::make_unique<OperacionExecutor>(std::move(puertos), reloj.funcion(), *programador);
        FuenteRespaldoLocal::Opciones o;
        o.directorioDatos = datos;
        o.raizPaquetes = raizPaquetes;
        o.rutaPreferencias = QDir(datos).filePath(QStringLiteral("preferencias.ini"));
        o.versionApp = QStringLiteral("0.1.0");
        o.reloj = reloj.funcion();
        lector = lectorInyectado ? std::move(lectorInyectado) : std::make_shared<SqliteLectorCopiaRespaldo>();
        fuente = std::make_unique<FuenteRespaldoLocal>(*ejecutor, lector, std::move(o));
        return true;
    }

    ~Entorno()
    {
        fuente.reset();
        if (ejecutor) {
            auto fin = ejecutor->detener(std::chrono::milliseconds(0));
            (void)QTest::qWaitFor([&] { return fin.isFinished(); }, 30000);
            ejecutor.reset();
        }
        if (p) {
            p->cerrarConexionDelHiloActual();
        }
    }

    SqliteConnectionProvider& prov() { return p->proveedor(); }

    QString perfil()
    {
        const QString id = uuid::generarCanonico();
        return ejecutarSql(prov(), QStringLiteral("INSERT INTO perfil_sat (id, rfc, nombre, activo, creado_en, "
                                                  "actualizado_en) VALUES (%1, %2, 'Contribuyente sintetico', 1, "
                                                  "%3, %3)")
                                       .arg(citar(id), citar(kRfc), citar(kAhora)))
                   .isEmpty()
            ? id
            : QString();
    }

    QString solicitud(const QString& perfilId, bool eliminada = false)
    {
        const QString id = uuid::generarCanonico();
        const QString e = ejecutarSql(
            prov(),
            QStringLiteral(
                "INSERT INTO solicitud_masiva (id, perfil_sat_id, id_solicitud_sat, tipo_cfdi, operacion_sat, "
                "rfc_solicitante, rfc_emisor, fecha_inicial_sat, fecha_final_sat, dedup_key, estado_local, "
                "cod_estatus_solicitud, estado_solicitud_sat, creada_en, envio_iniciado_en, enviada_en, "
                "ultima_verificacion_en, eliminado_en) VALUES (%1, %2, %3, 'emitidos', "
                "'SolicitaDescargaEmitidos', %4, %4, '2026-01-01T00:00:00', '2026-01-31T23:59:59', %5, "
                "'Enviada', '5000', 'Terminada', %6, %6, %6, %6, %7)")
                .arg(citar(id), citar(perfilId), citar(id.toUpper()), citar(kRfc),
                     citar(DedupKey::calcularV1(id.toUtf8()).texto()), citar(kAhora),
                     eliminada ? citar(kAhora) : QStringLiteral("NULL")));
        return e.isEmpty() ? id : QString();
    }

    // Paquete en `estado`; Descargado lleva ruta_local `ruta` y, si `contenido`
    // no es nulo, el ZIP sintetico en disco.
    QString paquete(const QString& solicitudId, const QString& estado, const QString& ruta = {},
                    const QByteArray& contenido = {})
    {
        const QString id = uuid::generarCanonico();
        QString e = insertarPaqueteFixture(prov(), id, solicitudId, QStringLiteral("PAQ_") + id.left(8), estado);
        if (e.isEmpty() && !ruta.isEmpty()) {
            e = ejecutarSql(prov(), QStringLiteral("UPDATE paquete_solicitud SET ruta_local = %1 WHERE id = %2")
                                        .arg(citar(ruta), citar(id)));
        }
        if (e.isEmpty() && !contenido.isNull() && !escribir(QDir(raizPaquetes).filePath(ruta), contenido)) {
            e = QStringLiteral("zip");
        }
        return e.isEmpty() ? id : QString();
    }

    Resultado<CapturaRespaldo, ErrorCaptura> capturar()
    {
        auto f = fuente->capturar();
        f.waitForFinished();
        if (f.isCanceled() || f.resultCount() == 0) {
            return Resultado<CapturaRespaldo, ErrorCaptura>::fallo(
                ErrorCaptura::de(ErrorCaptura::Categoria::Interno, QStringLiteral("cancelado")));
        }
        return f.result();
    }

    QString raizStaging() const { return QDir(datos).filePath(QStringLiteral("respaldo-tmp")); }
};

QByteArray zipSintetico(int n)
{
    QByteArray b("PK\x03\x04");
    for (int i = 0; i < 2000; ++i) {
        b += QStringLiteral("<cfdi:Comprobante Rfc=\"%1\" Folio=\"%2-%3\"/>").arg(kRfc).arg(n).arg(i).toUtf8();
    }
    return b;
}

QString rutaZip(const QString& solicitud, int n)
{
    return QStringLiteral("%1/2026-01/%2/PAQ_%3.zip").arg(kRfc, solicitud).arg(n);
}

} // namespace

class TestCapturaRespaldo : public QObject {
    Q_OBJECT

private:
    crypto::ContenedorRespaldoOpenSsl cr;
    KekRespaldo kekPw;
    KekRespaldo kekRec;

    // Captura -> contenedor (archivo real) -> extraer a staging privado.
    struct Ida {
        std::optional<ErrorRespaldo> error;
        ResumenAlimentacion resumen;
        InventarioRespaldo inventario;
        QString archivo;
        QString extraido;
        QList<EntradaManifiesto> entradas;
    };

    Ida respaldar(Entorno& e, const CapturaRespaldo& captura, const QString& nombre = QStringLiteral("r"))
    {
        Ida ida;
        ida.archivo = e.dir.filePath(nombre + QStringLiteral(".afbak.partial"));
        {
            QFile f(ida.archivo);
            if (!f.open(QIODevice::WriteOnly)) {
                ida.error = ErrorRespaldo::de(ErrorRespaldo::Categoria::Escritura);
                return ida;
            }
            auto esc = cr.crearEscritor(f, kekPw, kekRec,
                                        datosNuevoRespaldo(captura, kInstalacion, e.reloj.ahora()));
            if (!esc) {
                ida.error = esc.error();
                return ida;
            }
            auto r = alimentarEscritor(*esc.valor(), captura);
            if (!r) {
                ida.error = r.error();
                return ida;
            }
            ida.resumen = r.valor();
            auto fin = esc.valor()->finalizar();
            if (!fin) {
                ida.error = fin.error();
                return ida;
            }
        }
        ida.extraido = e.dir.filePath(nombre + QStringLiteral("-extraido"));
        QDir().mkpath(ida.extraido);
        QFile::setPermissions(ida.extraido, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QFile g(ida.archivo);
        if (!g.open(QIODevice::ReadOnly)) {
            ida.error = ErrorRespaldo::de(ErrorRespaldo::Categoria::Lectura);
            return ida;
        }
        OpcionesLectura op;
        op.alLeerEntrada = [&ida](const EntradaManifiesto& m) { ida.entradas.append(m); };
        auto x = cr.extraer(g, kekPw, ida.extraido, op);
        if (!x) {
            ida.error = x.error();
            return ida;
        }
        ida.inventario = x.valor();
        return ida;
    }

private slots:
    void initTestCase()
    {
        auto sal = cr.generarSal();
        QVERIFY(sal);
        auto k = cr.derivarKekContrasena(BufferSecreto::desdeTexto(kContrasena),
                                         ParametrosKdf::predeterminados(sal.valor()));
        QVERIFY(k);
        kekPw = std::move(k).valor();
        auto clave = cr.generarClaveRecuperacion();
        QVERIFY(clave);
        auto r = cr.derivarKekRecuperacion(clave.valor());
        QVERIFY(r);
        kekRec = std::move(r).valor();
    }

    // --- Ida y vuelta (criterio 1). ---
    void idaYVueltaConTresZip()
    {
        Entorno e;
        QVERIFY(e.preparar());
        const QString perfil = e.perfil();
        const QString s = e.solicitud(perfil);
        QVERIFY(!perfil.isEmpty() && !s.isEmpty());
        QStringList rutas;
        for (int i = 1; i <= 3; ++i) {
            rutas.append(rutaZip(s, i));
            QVERIFY(!e.paquete(s, QStringLiteral("Descargado"), rutas.last(), zipSintetico(i)).isEmpty());
        }
        // Descargando, Disponible y Descargado de una solicitud eliminada: fuera.
        QVERIFY(!e.paquete(s, QStringLiteral("Descargando")).isEmpty());
        QVERIFY(!e.paquete(s, QStringLiteral("Disponible")).isEmpty());
        const QString eliminada = e.solicitud(perfil, true);
        QVERIFY(!e.paquete(eliminada, QStringLiteral("Descargado"), rutaZip(eliminada, 9), zipSintetico(9)).isEmpty());
        QVERIFY(escribir(QDir(e.datos).filePath(QStringLiteral("preferencias.ini")),
                         QByteArrayLiteral("[facturas]\nultimoPerfil=") + perfil.toUtf8() + "\n"));

        auto c = e.capturar();
        QVERIFY2(c, c ? "" : qPrintable(c.error().diagnostico));
        const CapturaRespaldo& captura = c.valor();
        QCOMPARE(captura.paquetes(), 3);
        QCOMPARE(captura.faltantesAlCongelar(), 0);
        QCOMPARE(captura.paquetesOmitidos, 0);
        QCOMPARE(captura.metadatos.versionEsquema, 7);
        QCOMPARE(captura.metadatos.versionApp, QStringLiteral("0.1.0"));
        QCOMPARE(captura.entradas.at(0).rutaEnRespaldo, kRutaRespaldoBase);
        QCOMPARE(captura.entradas.at(1).rutaEnRespaldo, kRutaRespaldoPreferencias);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(captura.entradas.at(2 + i).rutaEnRespaldo, kPrefijoRespaldoPaquetes + rutas.at(i));
            QCOMPARE(captura.entradas.at(2 + i).raiz, e.raizPaquetes);
        }
        // Staging privado: carpeta 0700, archivos 0600, solo copia + ini.
        QCOMPARE(modo(e.raizStaging()), 0700);
        QCOMPARE(modo(captura.staging), 0700);
        QCOMPARE(modo(QDir(captura.staging).filePath(kNombreCopiaBase)), 0600);
        QCOMPARE(modo(QDir(captura.staging).filePath(kNombrePreferencias)), 0600);
        QCOMPARE(archivosBajo(captura.staging), (QStringList{kNombrePreferencias, kNombreCopiaBase}));
        // La copia es verificable sin el contenedor.
        QCOMPARE(integridad(QDir(captura.staging).filePath(kNombreCopiaBase)), QStringLiteral("ok"));
        QVERIFY(MetadatosCaptura::desdeBytes(captura.metadatos.serializar()) == captura.metadatos);

        const QByteArray shaCopia = sha(QDir(captura.staging).filePath(kNombreCopiaBase));
        const Ida ida = respaldar(e, captura);
        QVERIFY2(!ida.error, ida.error ? qPrintable(ida.error->diagnostico) : "");
        QVERIFY(!ida.resumen.conAdvertencias());
        QCOMPARE(ida.inventario.presentes, 5u);
        QCOMPARE(sha(QDir(ida.extraido).filePath(kRutaRespaldoBase)), shaCopia);
        QCOMPARE(integridad(QDir(ida.extraido).filePath(kRutaRespaldoBase)), QStringLiteral("ok"));
        for (const QString& r : rutas) {
            QCOMPARE(sha(QDir(ida.extraido).filePath(kPrefijoRespaldoPaquetes + r)),
                     sha(QDir(e.raizPaquetes).filePath(r)));
        }
        // Metadatos dentro del contenedor.
        QCOMPARE(MetadatosCaptura::desdeBytes(ida.inventario.metadatos)->versionEsquema, 7);

        QVERIFY(e.fuente->limpiar(captura));
        QVERIFY(!QFileInfo::exists(captura.staging));
        QVERIFY(QDir(e.raizStaging()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden));
    }

    void idaYVueltaBaseVacia()
    {
        Entorno e;
        QVERIFY(e.preparar());
        auto c = e.capturar();
        QVERIFY(c);
        QCOMPARE(c.valor().entradas.size(), qsizetype(1)); // solo la base, sin preferencias
        const Ida ida = respaldar(e, c.valor());
        QVERIFY2(!ida.error, ida.error ? qPrintable(ida.error->diagnostico) : "");
        QVariant perfiles;
        QCOMPARE(integridad(QDir(ida.extraido).filePath(kRutaRespaldoBase), QStringLiteral("SELECT COUNT(*) FROM perfil_sat"),
                            &perfiles),
                 QStringLiteral("ok"));
        QCOMPARE(perfiles.toInt(), 0);
        QVERIFY(e.fuente->limpiar(c.valor()));
    }

    // --- D10: ejecutor y concurrencia (criterio 2). ---
    void capturaVaDetrasDeLaOperacionEnCursoYEnElEjecutor()
    {
        Entorno e;
        QVERIFY(e.preparar());
        std::atomic<qint64> finLectura{-1};
        auto bloqueo = e.ejecutor->leer([&](OperacionesSolicitudRepository&) {
            QThread::msleep(300);
            finLectura.store(e.observada->reloj.elapsed());
        });
        auto c = e.capturar();
        bloqueo.waitForFinished();
        QVERIFY(c);
        QVERIFY(e.observada->hilo.load() != nullptr);
        QVERIFY(e.observada->hilo.load() != QThread::currentThread());
        QVERIFY(finLectura.load() >= 0);
        QVERIFY(e.observada->inicioMs.load() >= finLectura.load());
        QVERIFY(e.fuente->limpiar(c.valor()));
    }

    void escriturasConcurrentesYPausaMenorAUnSegundo()
    {
        Entorno e;
        QVERIFY(e.preparar());
        // Base sintetica: 1 perfil, 200 solicitudes, 1000 paquetes.
        const QString perfil = e.perfil();
        QVERIFY(!perfil.isEmpty());
        QVERIFY(ejecutarSql(e.prov(), QStringLiteral("BEGIN")).isEmpty());
        for (int i = 0; i < 200; ++i) {
            const QString s = e.solicitud(perfil);
            QVERIFY(!s.isEmpty());
            for (int j = 0; j < 5; ++j) {
                QVERIFY(!e.paquete(s, QStringLiteral("Descargado"), rutaZip(s, j)).isEmpty());
            }
        }
        QVERIFY(ejecutarSql(e.prov(), QStringLiteral("COMMIT")).isEmpty());
        const int antes = escalar(e.prov(), QStringLiteral("SELECT COUNT(*) FROM solicitud_masiva")).toInt();

        // Escritor concurrente (otra conexion, otro hilo): solicitud + 5
        // paquetes por transaccion, durante toda la captura.
        std::atomic<bool> parar{false};
        std::atomic<int> confirmadas{0};
        QStringList errores;
        std::unique_ptr<QThread> hilo(QThread::create([&] {
            while (!parar.load()) {
                if (!ejecutarSql(e.prov(), QStringLiteral("BEGIN IMMEDIATE")).isEmpty()) {
                    errores.append(QStringLiteral("begin"));
                    continue;
                }
                const QString s = e.solicitud(perfil);
                bool ok = !s.isEmpty();
                for (int j = 0; ok && j < 5; ++j) {
                    ok = !e.paquete(s, QStringLiteral("Descargado"), rutaZip(s, j)).isEmpty();
                }
                if (!ok || !ejecutarSql(e.prov(), QStringLiteral("COMMIT")).isEmpty()) {
                    errores.append(QStringLiteral("tx"));
                    ejecutarSql(e.prov(), QStringLiteral("ROLLBACK"));
                    continue;
                }
                confirmadas.fetch_add(1);
            }
            e.p->cerrarConexionDelHiloActual();
        }));
        hilo->start();
        while (confirmadas.load() < 3) {
            QThread::msleep(1);
        }
        auto c = e.capturar();
        // El ejecutor reanuda: una lectura encolada despues termina enseguida.
        QElapsedTimer t;
        t.start();
        e.ejecutor->leer([](OperacionesSolicitudRepository&) {}).waitForFinished();
        const qint64 reanudacion = t.elapsed();
        parar.store(true);
        hilo->wait();
        QVERIFY2(errores.isEmpty(), qPrintable(errores.join(QLatin1Char(','))));
        QVERIFY2(c, c ? "" : qPrintable(c.error().diagnostico));
        const CapturaRespaldo& captura = c.valor();
        qInfo("pausa del ejecutor %lld ms; reanudacion %lld ms; escrituras concurrentes %d",
              captura.pausaEjecutorMs, reanudacion, confirmadas.load());
        QVERIFY(captura.pausaEjecutorMs < 1000);
        QVERIFY(reanudacion < 1000);

        // Copia consistente: cada solicitud copiada tiene sus 5 paquetes y la
        // lista congelada coincide con la copia.
        const QString copia = QDir(captura.staging).filePath(kNombreCopiaBase);
        QVariant solicitudes;
        QCOMPARE(integridad(copia, QStringLiteral("SELECT COUNT(*) FROM solicitud_masiva"), &solicitudes),
                 QStringLiteral("ok"));
        QVariant incompletas;
        integridad(copia,
                   QStringLiteral("SELECT COUNT(*) FROM (SELECT s.id FROM solicitud_masiva s LEFT JOIN "
                                  "paquete_solicitud p ON p.solicitud_masiva_id = s.id GROUP BY s.id "
                                  "HAVING COUNT(p.id) <> 5)"),
                   &incompletas);
        QCOMPARE(incompletas.toInt(), 0);
        QVERIFY(solicitudes.toInt() >= antes + 3);
        QCOMPARE(captura.paquetes(), solicitudes.toInt() * 5);
        QVERIFY(e.fuente->limpiar(captura));
    }

    // --- D11 (criterio 2). ---
    void faltanteAlCongelarTerminaConAdvertencia()
    {
        Entorno e;
        QVERIFY(e.preparar());
        const QString s = e.solicitud(e.perfil());
        QVERIFY(!e.paquete(s, QStringLiteral("Descargado"), rutaZip(s, 1), zipSintetico(1)).isEmpty());
        QVERIFY(!e.paquete(s, QStringLiteral("Descargado"), rutaZip(s, 2)).isEmpty()); // sin archivo
        auto c = e.capturar();
        QVERIFY(c);
        QCOMPARE(c.valor().faltantesAlCongelar(), 1);
        const Ida ida = respaldar(e, c.valor());
        QVERIFY2(!ida.error, ida.error ? qPrintable(ida.error->diagnostico) : "");
        QVERIFY(ida.resumen.conAdvertencias());
        QCOMPARE(ida.resumen.faltantes, 1);
        QCOMPARE(ida.inventario.faltantes, 1u);
        bool anotado = false;
        for (const auto& m : ida.entradas) {
            anotado = anotado
                || (m.estado == EntradaManifiesto::Estado::Faltante
                    && m.rutaRelativa == kPrefijoRespaldoPaquetes + rutaZip(s, 2));
        }
        QVERIFY(anotado);
        QVERIFY(e.fuente->limpiar(c.valor()));
    }

    void zipQueCambiaODesapareceTrasCongelarFalla_data()
    {
        QTest::addColumn<int>("caso");
        QTest::newRow("contenido") << 0;
        QTest::newRow("reemplazado") << 1;
        QTest::newRow("borrado") << 2;
    }

    void zipQueCambiaODesapareceTrasCongelarFalla()
    {
        QFETCH(int, caso);
        Entorno e;
        QVERIFY(e.preparar());
        const QString s = e.solicitud(e.perfil());
        const QString ruta = rutaZip(s, 1);
        QVERIFY(!e.paquete(s, QStringLiteral("Descargado"), ruta, zipSintetico(1)).isEmpty());
        auto c = e.capturar();
        QVERIFY(c);
        const QString zip = QDir(e.raizPaquetes).filePath(ruta);
        QThread::msleep(5); // mtime con resolucion de ns, pero sin depender de ella
        switch (caso) {
        case 0: {
            QFile f(zip);
            QVERIFY(f.open(QIODevice::Append));
            f.write("x");
            break;
        }
        case 1: {
            QVERIFY(QFile::remove(zip));
            QVERIFY(escribir(zip, zipSintetico(1))); // mismo contenido, otro inodo
            break;
        }
        default: QVERIFY(QFile::remove(zip)); break;
        }
        const Ida ida = respaldar(e, c.valor());
        QVERIFY(ida.error);
        QCOMPARE(ida.error->categoria, ErrorRespaldo::Categoria::FuenteCambio);
        QVERIFY(e.fuente->limpiar(c.valor()));
    }

    // --- D3, D7, D14: lista blanca. ---
    void credentialsNuncaEntra()
    {
        Entorno e;
        QVERIFY(e.preparar());
        QVERIFY(escribir(QDir(e.datos).filePath(QStringLiteral("credentials/0b6f4c4e.scc")),
                         QByteArrayLiteral("CONTRASENA-SAT-SINTETICA")));
        const QString s = e.solicitud(e.perfil());
        // ruta_local hostil hacia credentials/: se omite, no se copia.
        QVERIFY(!e.paquete(s, QStringLiteral("Descargado"), QStringLiteral("../credentials/0b6f4c4e.scc")).isEmpty());
        QVERIFY(!e.paquete(s, QStringLiteral("Descargado"), QStringLiteral("/Users/usuario/x.zip")).isEmpty());
        auto c = e.capturar();
        QVERIFY(c);
        QCOMPARE(c.valor().paquetesOmitidos, 2);
        for (const auto& entrada : c.valor().entradas) {
            QVERIFY(!entrada.rutaEnRespaldo.contains(QLatin1String("credentials")));
            QVERIFY(!entrada.rutaRelativa.contains(QLatin1String("credentials")));
        }
        const Ida ida = respaldar(e, c.valor());
        QVERIFY(!ida.error);
        QVERIFY(ida.resumen.conAdvertencias());
        for (const QString& r : archivosBajo(ida.extraido)) {
            QVERIFY2(!r.contains(QLatin1String("credentials")), qPrintable(r));
        }
        QVERIFY(!leerArchivo(ida.archivo).contains("CONTRASENA-SAT-SINTETICA"));
        QVERIFY(e.fuente->limpiar(c.valor()));
    }

    void preferenciasPorListaBlancaDeClaves_data()
    {
        QTest::addColumn<QString>("valorUltimoPerfil");
        QTest::addColumn<bool>("aceptado");
        QTest::newRow("uuid canonico") << QStringLiteral("0b6f4c4e-6a8e-4d55-9a3f-2f1f6a1b9c01") << true;
        QTest::newRow("relativa documentos/cliente") << QStringLiteral("documentos/cliente") << false;
        QTest::newRow("relativa ./datos") << QStringLiteral("./datos") << false;
        QTest::newRow("relativa ../../otra") << QStringLiteral("../../otra") << false;
        QTest::newRow("absoluta") << QStringLiteral("/Users/usuario/Documents") << false;
        QTest::newRow("uuid en mayusculas") << QStringLiteral("0B6F4C4E-6A8E-4D55-9A3F-2F1F6A1B9C01") << false;
        QTest::newRow("vacio") << QString() << false;
    }

    void preferenciasPorListaBlancaDeClaves()
    {
        QFETCH(QString, valorUltimoPerfil);
        QFETCH(bool, aceptado);
        Entorno e;
        QVERIFY(e.preparar());
        {
            QSettings ini(QDir(e.datos).filePath(QStringLiteral("preferencias.ini")), QSettings::IniFormat);
            ini.setValue(QStringLiteral("facturas/ultimoPerfil"), valorUltimoPerfil);
            // Claves desconocidas del grupo facturas, aunque parezcan inocuas.
            ini.setValue(QStringLiteral("facturas/ultimaCarpeta"), QStringLiteral("documentos/cliente"));
            ini.setValue(QStringLiteral("facturas/otra"), QStringLiteral("./datos"));
            ini.setValue(QStringLiteral("facturas/columnas"), QStringLiteral("rfc,total"));
            ini.setValue(QStringLiteral("respaldo/carpeta"), QStringLiteral("../../otra"));
            ini.setValue(QStringLiteral("respaldo/marcador"), QByteArrayLiteral("bookmark"));
            ini.setValue(QStringLiteral("ventana/geometria"), QStringLiteral("10,10,800,600"));
            ini.setValue(QStringLiteral("sinGrupo"), QStringLiteral("x"));
            ini.sync();
        }
        auto c = e.capturar();
        QVERIFY(c);
        const QString filtrado = QDir(c.valor().staging).filePath(kNombrePreferencias);
        bool hayEntrada = false;
        for (const auto& entrada : c.valor().entradas) {
            hayEntrada = hayEntrada || entrada.tipo == EntradaCaptura::Tipo::Preferencias;
        }
        QCOMPARE(hayEntrada, aceptado);
        if (aceptado) {
            QSettings salida(filtrado, QSettings::IniFormat);
            QCOMPARE(salida.allKeys(), QStringList{QStringLiteral("facturas/ultimoPerfil")});
            QCOMPARE(salida.value(QStringLiteral("facturas/ultimoPerfil")).toString(), valorUltimoPerfil);
            const QByteArray bytes = leerArchivo(filtrado);
            for (const char* prohibido : {"respaldo", "documentos", "./", "../", "/Users/", "columnas", "ventana"}) {
                QVERIFY2(!bytes.contains(prohibido), prohibido);
            }
        } else {
            QVERIFY(!QFileInfo::exists(filtrado) || leerArchivo(filtrado).trimmed().isEmpty());
        }
        QVERIFY(e.fuente->limpiar(c.valor()));
    }

    // --- Vida y concurrencia (revision C1-C3). ---
    void destruirLaFuenteConUnaCapturaEnCurso()
    {
        Entorno e;
        QVERIFY(e.preparar());
        auto bloqueo = e.ejecutor->leer([](OperacionesSolicitudRepository&) { QThread::msleep(200); });
        auto f = e.fuente->capturar();
        const QString raiz = e.raizStaging();
        // La fuente y la referencia de la prueba al lector se sueltan antes de
        // que el ejecutor tome la copia: la continuacion retiene su estado.
        e.fuente.reset();
        e.lector.reset();
        QVERIFY(QTest::qWaitFor([&] { return f.isFinished(); }, 30000));
        bloqueo.waitForFinished();
        QVERIFY(f.resultCount() == 1);
        QVERIFY2(f.result(), f.result() ? "" : qPrintable(f.result().error().diagnostico));
        QVERIFY(QFileInfo::exists(f.result().valor().staging));
        QCOMPARE(integridad(QDir(f.result().valor().staging).filePath(kNombreCopiaBase)), QStringLiteral("ok"));
        QDir(raiz).removeRecursively();
    }

    void destruirElRootConCapturaYLimpiezaEnCurso()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString datos = tmp.filePath(QStringLiteral("datos"));
        const QString raiz = QDir(datos).filePath(QStringLiteral("respaldo-tmp"));
        for (int i = 0; i < 50; ++i) {
            QVERIFY(escribir(QDir(raiz).filePath(QStringLiteral("captura-viejo%1/satcfdi.sqlite3").arg(i)), "copia"));
        }
        AppBootstrapper::Opciones opciones;
        opciones.directorioDatos = datos;
        const auto arranque = AppBootstrapper(opciones).preparar();
        QVERIFY(arranque);
        fakes::FakeSecretStore secretos;
        fakes::FakeOperacionesSat sat;
        QFuture<Resultado<CapturaRespaldo, ErrorCaptura>> f;
        {
            OpcionesMonitoreo m;
            m.operacionesSat = &sat;
            AppCompositionRoot root(arranque.valor().rutaBase, secretos, m);
            // El ejecutor ocupado: la copia queda en cola cuando el root se
            // destruye (sin esperar la limpieza inicial ni la captura).
            root.ejecutor().leer([](OperacionesSolicitudRepository&) { QThread::msleep(200); });
            f = root.fuenteRespaldo().capturar();
        }
        QVERIFY(QTest::qWaitFor([&] { return f.isFinished(); }, 30000));
        if (f.resultCount() == 1 && !f.result()) {
            QCOMPARE(f.result().error().categoria, ErrorCaptura::Categoria::EjecutorNoDisponible);
        }
        // Sin captura viva, el staging queda sin huerfanos de la prueba.
        const QStringList restos =
            QDir(raiz).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QString& r : restos) {
            QVERIFY2(!r.startsWith(QLatin1String("captura-viejo")), qPrintable(r));
        }
    }

    void limpiezaNuncaBorraUnaCapturaActiva()
    {
        Entorno e;
        QVERIFY(e.preparar());
        QVERIFY(escribir(QDir(e.raizStaging()).filePath(QStringLiteral("captura-huerfana/x")), "x"));
        auto bloqueo = e.ejecutor->leer([](OperacionesSolicitudRepository&) { QThread::msleep(200); });
        auto f = e.fuente->capturar(); // staging creado, copia en cola
        QCOMPARE(e.fuente->capturasActivas(), 1);
        QCOMPARE(e.fuente->limpiarHuerfanos(), 1); // solo la huerfana
        QVERIFY(QTest::qWaitFor([&] { return f.isFinished(); }, 30000));
        bloqueo.waitForFinished();
        QVERIFY2(f.result(), f.result() ? "" : qPrintable(f.result().error().diagnostico));
        QCOMPARE(integridad(QDir(f.result().valor().staging).filePath(kNombreCopiaBase)), QStringLiteral("ok"));
        // Una limpieza posterior tampoco la toca hasta limpiar().
        QCOMPARE(e.fuente->limpiarHuerfanos(), 0);
        QVERIFY(QFileInfo::exists(f.result().valor().staging));
        QVERIFY(e.fuente->limpiar(f.result().valor()));
        QCOMPARE(e.fuente->capturasActivas(), 0);
    }

    void capturaInmediataAlArrancarSinEsperarLaLimpieza()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString datos = tmp.filePath(QStringLiteral("datos"));
        const QString raiz = QDir(datos).filePath(QStringLiteral("respaldo-tmp"));
        for (int i = 0; i < 200; ++i) {
            QVERIFY(escribir(QDir(raiz).filePath(QStringLiteral("captura-viejo%1/satcfdi.sqlite3").arg(i)), "copia"));
        }
        AppBootstrapper::Opciones opciones;
        opciones.directorioDatos = datos;
        const auto arranque = AppBootstrapper(opciones).preparar();
        QVERIFY(arranque);
        fakes::FakeSecretStore secretos;
        fakes::FakeOperacionesSat sat;
        OpcionesMonitoreo m;
        m.operacionesSat = &sat;
        AppCompositionRoot root(arranque.valor().rutaBase, secretos, m);
        auto c = root.fuenteRespaldo().capturar(); // sin esperar la limpieza inicial
        QVERIFY(QTest::qWaitFor([&] { return c.isFinished(); }, 30000));
        auto limpieza = root.limpiezaRespaldoInicial();
        QVERIFY(QTest::qWaitFor([&] { return limpieza.isFinished(); }, 30000));
        QCOMPARE(limpieza.result(), 200);
        QVERIFY2(c.result(), c.result() ? "" : qPrintable(c.result().error().diagnostico));
        const CapturaRespaldo captura = c.result().valor();
        QVERIFY(QFileInfo::exists(QDir(captura.staging).filePath(kNombreCopiaBase)));
        QCOMPARE(QDir(raiz).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden),
                 QStringList{QFileInfo(captura.staging).fileName()});
        QVERIFY(root.fuenteRespaldo().limpiar(captura));
    }

    // --- Limpieza del staging. ---
    void limpiezaAlFallar()
    {
        Entorno e;
        QVERIFY(e.preparar(std::make_shared<LectorQueFalla>()));
        auto c = e.capturar();
        QVERIFY(!c);
        QCOMPARE(c.error().categoria, ErrorCaptura::Categoria::Integridad);
        QVERIFY(QDir(e.raizStaging()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden));

        // Ejecutor detenido: EjecutorNoDisponible y staging vacio.
        Entorno d;
        QVERIFY(d.preparar());
        auto fin = d.ejecutor->detener(std::chrono::milliseconds(0));
        QVERIFY(QTest::qWaitFor([&] { return fin.isFinished(); }, 30000));
        auto r = d.capturar();
        QVERIFY(!r);
        QCOMPARE(r.error().categoria, ErrorCaptura::Categoria::EjecutorNoDisponible);
        QVERIFY(QDir(d.raizStaging()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden));
    }

    void limpiarRechazaRutasAjenas()
    {
        Entorno e;
        QVERIFY(e.preparar());
        const QString ajena = e.dir.filePath(QStringLiteral("ajena"));
        QVERIFY(QDir().mkpath(ajena));
        CapturaRespaldo falsa;
        falsa.staging = ajena;
        QVERIFY(!e.fuente->limpiar(falsa));
        QVERIFY(QFileInfo::exists(ajena));
    }

    void stagingEnlazadoSeRechaza()
    {
        Entorno e;
        QVERIFY(e.preparar());
        const QString fuera = e.dir.filePath(QStringLiteral("fuera"));
        QVERIFY(QDir().mkpath(fuera));
        QVERIFY(QFile::link(fuera, e.raizStaging()));
        auto c = e.capturar();
        QVERIFY(!c);
        QCOMPARE(c.error().categoria, ErrorCaptura::Categoria::Staging);
        QVERIFY(QDir(fuera).isEmpty());
        QCOMPARE(e.fuente->limpiarHuerfanos(), 1); // borra el enlace, no su destino
        QVERIFY(QFileInfo::exists(fuera));
        QVERIFY(!QFileInfo(e.raizStaging()).isSymLink());
    }

    void limpiezaDeHuerfanosAlArrancar()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString datos = tmp.filePath(QStringLiteral("datos"));
        // Restos de un proceso que murio a mitad de una captura.
        const QString raiz = QDir(datos).filePath(QStringLiteral("respaldo-tmp"));
        QVERIFY(escribir(QDir(raiz).filePath(QStringLiteral("captura-a/satcfdi.sqlite3")), "copia"));
        QVERIFY(escribir(QDir(raiz).filePath(QStringLiteral("captura-b/preferencias.ini")), "ini"));
        AppBootstrapper::Opciones opciones;
        opciones.directorioDatos = datos;
        const auto arranque = AppBootstrapper(opciones).preparar();
        QVERIFY(arranque);
        fakes::FakeSecretStore secretos;
        fakes::FakeOperacionesSat sat;
        OpcionesMonitoreo m;
        m.operacionesSat = &sat;
        AppCompositionRoot root(arranque.valor().rutaBase, secretos, m);
        auto limpieza = root.limpiezaRespaldoInicial();
        limpieza.waitForFinished();
        QCOMPARE(limpieza.result(), 2);
        QVERIFY(QDir(raiz).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden));

        // La fuente del root captura de verdad (wiring del ejecutor).
        auto c = root.fuenteRespaldo().capturar();
        c.waitForFinished();
        QVERIFY(c.result());
        QCOMPARE(c.result().valor().metadatos.versionEsquema, 7);
        QVERIFY(root.fuenteRespaldo().limpiar(c.result().valor()));
    }
};

int main(int argc, char* argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    TestCapturaRespaldo prueba;
    return QTest::qExec(&prueba, argc, argv);
}

#include "TestCapturaRespaldo.moc"
