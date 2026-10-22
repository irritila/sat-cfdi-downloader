// T025.5: restauracion transaccional. Contenedor AFB1 real (T025.1), captura
// real (T025.2), SQLite real, AreaRestauracionLocal y RestauracionArranque
// sobre QTemporaryDir con datos sinteticos (XAXX010101000, /Users/usuario).
// Sin red, Keychain ni datos reales.

#include "SqlitePruebasComun.h"

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"
#include "app_core/AreaRestauracionLocal.h"
#include "app_core/DatosPreviosRestauracion.h"
#include "app_core/ArranqueProceso.h"
#include "app_core/FuenteRespaldoLocal.h"
#include "app_core/MarcadorRestauracion.h"
#include "app_core/MigracionRaizPaquetes.h"
#include "app_core/RestauracionArranque.h"
#include "app_core/SingleInstanceCoordinator.h"
#include "application/logging/RegexLogSanitizer.h"
#include "application/operaciones/OperacionExecutor.h"
#include "application/operaciones/WorkerLocal.h"
#include "application/profiles/CredencialesSatService.h"
#include "application/respaldo/AlimentadorRespaldo.h"
#include "application/respaldo/RestauracionService.h"
#include "infrastructure/crypto/ContenedorRespaldoOpenSsl.h"
#include "infrastructure/persistence/sqlite/SqliteBaseRestauracion.h"
#include "infrastructure/persistence/sqlite/SqliteCopiaRespaldo.h"
#include "infrastructure/storage/FileOps.h"
#include "infrastructure/storage/FilesystemPackageStorage.h"

#include "fakes/FakeOperacionesSat.h"
#include "fakes/FakeOSIntegration.h"
#include "fakes/FakeProgramador.h"
#include "fakes/FakeSatGateway.h"
#include "fakes/FakeSecretStore.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMutex>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QThread>
#include <QTimeZone>
#include <QTimer>
#include <QWaitCondition>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <atomic>
#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;
using namespace satcfdi::respaldo;

namespace {

const QString kRfc = QStringLiteral("XAXX010101000");
const QUuid kInstalacionA = QUuid(QStringLiteral("{6f1c2a7e-3b9d-4c5e-8f10-2a3b4c5d6e7f}"));
const QString kIdInstalacionB = QStringLiteral("2b0c7d1e-5a4f-4e3d-9c2b-1a0f9e8d7c6b");
const QString kContrasena = QStringLiteral("contrasena-sintetica-1");
const qint64 kMtimeIndexadoMs = 1769904000123; // 2026-02-01T00:00:00.123Z
using EstadoArranque = ResultadoRestauracionArranque::Estado;

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

bool excluidoDeRespaldo(const QString& ruta)
{
    return ::getxattr(QFile::encodeName(ruta).constData(), "com.apple.metadata:com_apple_backup_excludeItem", nullptr,
                      0, 0, XATTR_NOFOLLOW)
        > 0;
}

bool fijarMtime(const QString& ruta, qint64 ms)
{
    struct timespec t {};
    t.tv_sec = static_cast<time_t>(ms / 1000);
    t.tv_nsec = static_cast<long>((ms % 1000) * 1'000'000);
    const struct timespec ts[2] = {t, t};
    return ::utimensat(AT_FDCWD, QFile::encodeName(ruta).constData(), ts, 0) == 0;
}

qint64 mtimeMs(const QString& ruta)
{
    struct stat st {};
    if (::lstat(QFile::encodeName(ruta).constData(), &st) != 0) {
        return -1;
    }
    return static_cast<qint64>(st.st_mtimespec.tv_sec) * 1000 + st.st_mtimespec.tv_nsec / 1'000'000;
}

QStringList entradasBajo(const QString& raiz)
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

// Consulta con conexion propia (base cerrada por la app).
QVariant consultar(const QString& base, const QString& sql)
{
    const QString nombre = QStringLiteral("prueba-") + uuid::generarCanonico();
    QVariant v;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), nombre);
        db.setDatabaseName(base);
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(sql) && q.next()) {
                v = q.value(0);
            }
            q = QSqlQuery();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(nombre);
    return v;
}

QByteArray zipSintetico(int n)
{
    QByteArray b("PK\x03\x04");
    for (int i = 0; i < 1500; ++i) {
        b += QStringLiteral("<cfdi:Comprobante Rfc=\"%1\" Folio=\"%2-%3\"/>").arg(kRfc).arg(n).arg(i).toUtf8();
    }
    return b;
}

// --- Origen del contenedor ----------------------------------------------------

// QFile que entrega pocos bytes por lectura, con espera opcional y una
// compuerta que bloquea la lectura hasta liberarla (archivo en la nube).
class DispositivoLento final : public QIODevice {
public:
    struct Control {
        QMutex mutex;
        QWaitCondition cambio;
        bool bloquear = false;
        bool liberado = false;
        std::atomic<int> lecturasBloqueadas{0};
        int esperaMs = 0;
        qint64 bytesPorLectura = 64 * 1024;

        void liberar()
        {
            QMutexLocker l(&mutex);
            liberado = true;
            cambio.wakeAll();
        }
    };

    DispositivoLento(const QString& ruta, std::shared_ptr<Control> c) : m_f(ruta), m_c(std::move(c)) {}

    bool abrir()
    {
        return m_f.open(QIODevice::ReadOnly) && QIODevice::open(QIODevice::ReadOnly);
    }
    qint64 size() const override { return m_f.size(); }

protected:
    qint64 readData(char* datos, qint64 max) override
    {
        {
            QMutexLocker l(&m_c->mutex);
            if (m_c->bloquear && !m_c->liberado) {
                ++m_c->lecturasBloqueadas;
                while (!m_c->liberado) {
                    m_c->cambio.wait(&m_c->mutex);
                }
                return -1; // el proveedor no entrego el archivo
            }
        }
        if (m_c->esperaMs > 0) {
            QThread::msleep(static_cast<unsigned long>(m_c->esperaMs));
        }
        m_f.seek(pos());
        return m_f.read(datos, std::min(max, m_c->bytesPorLectura));
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    QFile m_f;
    std::shared_ptr<Control> m_c;
};

class OrigenLento final : public OrigenRestauracion {
public:
    OrigenLento(QString ruta, std::shared_ptr<DispositivoLento::Control> c) : m_ruta(std::move(ruta)), m_c(std::move(c)) {}

    Resultado<std::unique_ptr<QIODevice>, ErrorRestauracion> abrir() override
    {
        using R = Resultado<std::unique_ptr<QIODevice>, ErrorRestauracion>;
        auto d = std::make_unique<DispositivoLento>(m_ruta, m_c);
        if (!d->abrir()) {
            return R::fallo(ErrorRestauracion::de(ErrorRestauracion::Categoria::Origen));
        }
        return R::exito(std::unique_ptr<QIODevice>(std::move(d)));
    }
    Resultado<HuellaArchivo, ErrorRestauracion> huella() override { return OrigenRestauracionArchivo(m_ruta).huella(); }

private:
    QString m_ruta;
    std::shared_ptr<DispositivoLento::Control> m_c;
};

// FileOps real con renames fallidos inyectados (para revertir).
class FileOpsConFallos final : public FileOps {
public:
    QByteArray fallarRenombrarHacia; // sufijo del destino
    QByteArray siOrigenContiene;     // y fragmento del origen
    int tipo(const QByteArray& r, TipoEntrada& t) override { return m_real.tipo(r, t); }
    int tipoResuelto(const QByteArray& r, TipoEntrada& t) override { return m_real.tipoResuelto(r, t); }
    int crearDirectorio(const QByteArray& r, unsigned m) override { return m_real.crearDirectorio(r, m); }
    int abrirExclusivo(const QByteArray& r, unsigned m, int& fd) override { return m_real.abrirExclusivo(r, m, fd); }
    int escribir(int fd, const char* d, std::size_t n) override { return m_real.escribir(fd, d, n); }
    int sincronizarArchivo(int fd) override { return m_real.sincronizarArchivo(fd); }
    int cerrar(int fd) override { return m_real.cerrar(fd); }
    int promoverSinReemplazo(const QByteArray& o, const QByteArray& d) override { return m_real.promoverSinReemplazo(o, d); }
    int sincronizarDirectorio(const QByteArray& d) override { return m_real.sincronizarDirectorio(d); }
    int eliminarArchivo(const QByteArray& r) override { return m_real.eliminarArchivo(r); }
    int listar(const QByteArray& d, QList<EntradaDirectorio>& e) override { return m_real.listar(d, e); }
    void aleatorio(unsigned char* d, std::size_t n) override { m_real.aleatorio(d, n); }
    int eliminarDirectorioVacio(const QByteArray& r) override { return m_real.eliminarDirectorioVacio(r); }
    int renombrarSinReemplazo(const QByteArray& o, const QByteArray& d) override
    {
        if (!fallarRenombrarHacia.isEmpty() && d.endsWith(fallarRenombrarHacia) && o.contains(siOrigenContiene)) {
            return EIO;
        }
        return m_real.renombrarSinReemplazo(o, d);
    }

private:
    PosixFileOps m_real;
};

// --- Instalacion de origen (A): base real, ejecutor real y captura ----------

struct Origen {
    QTemporaryDir dir;
    QString datos;
    QString raiz;
    std::unique_ptr<SqlitePersistencia> p;
    std::unique_ptr<SqliteCopiaRespaldo> copia;
    RegexLogSanitizer sanitizer;
    fakes::FakeOperacionesSat sat;
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 21), QTime(12, 0), QTimeZone::UTC)};
    std::unique_ptr<fakes::FakeProgramador> programador;
    std::unique_ptr<OperacionExecutor> ejecutor;
    std::unique_ptr<FuenteRespaldoLocal> fuente;
    QString perfil;
    QString solicitud;
    QStringList zips;              // rutas relativas
    QList<QByteArray> shas;
    QList<qint64> mtimes;

    bool preparar()
    {
        // Simula /Users/usuario/... dentro del temporal.
        datos = dir.filePath(QStringLiteral("Users/usuario/datos"));
        raiz = QDir(datos).filePath(QStringLiteral("paquetes"));
        if (!dir.isValid() || !QDir().mkpath(raiz)) {
            return false;
        }
        const QString base = QDir(datos).filePath(QStringLiteral("satcfdi.sqlite3"));
        if (!inicializarBaseSqlite(base)) {
            return false;
        }
        p = std::make_unique<SqlitePersistencia>(base);
        copia = std::make_unique<SqliteCopiaRespaldo>(*p);
        programador = std::make_unique<fakes::FakeProgramador>(reloj);
        SqlitePersistencia* persistencia = p.get();
        PuertosEjecutor puertos{p->solicitudes(), p->logs(), p->operaciones(), p->unidadDeTrabajo(), sanitizer,
                                sat, [persistencia] { persistencia->cerrarConexionDelHiloActual(); },
                                &p->paquetes(), nullptr, copia.get()};
        ejecutor = std::make_unique<OperacionExecutor>(std::move(puertos), reloj.funcion(), *programador);
        FuenteRespaldoLocal::Opciones o;
        o.directorioDatos = datos;
        o.raizPaquetes = raiz;
        o.rutaPreferencias = QDir(datos).filePath(QStringLiteral("preferencias.ini"));
        o.versionApp = QStringLiteral("0.1.0");
        o.reloj = reloj.funcion();
        fuente = std::make_unique<FuenteRespaldoLocal>(*ejecutor, std::make_shared<SqliteLectorCopiaRespaldo>(),
                                                       std::move(o));
        return true;
    }

    ~Origen()
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

    // Perfil con e.firma registrada y aviso de vencimiento; solicitud
    // Enviada en verificacion; `n` ZIP Descargado (el ultimo sin indexar).
    bool poblar(int n)
    {
        perfil = uuid::generarCanonico();
        if (!insertarPerfilFixture(prov(), perfil, kRfc).isEmpty()) {
            return false;
        }
        if (!ejecutarSql(prov(), QStringLiteral("INSERT INTO credencial_sat (id, perfil_sat_id, certificado_ref, "
                                                "llave_privada_ref, contrasena_ref, registrada_en, actualizada_en) "
                                                "VALUES (%1, %2, 'scs1:x:cert', 'scs1:x:key', 'scs1:x:pass', %3, %3)")
                                     .arg(citar(uuid::generarCanonico()), citar(perfil), citar(kAhora)))
                 .isEmpty()
            || !ejecutarSql(prov(), QStringLiteral("INSERT INTO aviso_vencimiento_efirma (perfil_sat_id, "
                                                   "vigente_hasta, umbral_dias, avisado_en) VALUES (%1, "
                                                   "'2027-01-01T00:00:00.000Z', 30, %2)")
                                        .arg(citar(perfil), citar(kAhora)))
                    .isEmpty()) {
            return false;
        }
        solicitud = uuid::generarCanonico();
        if (!ejecutarSql(prov(),
                         QStringLiteral(
                             "INSERT INTO solicitud_masiva (id, perfil_sat_id, id_solicitud_sat, tipo_cfdi, "
                             "operacion_sat, rfc_solicitante, rfc_emisor, fecha_inicial_sat, fecha_final_sat, "
                             "dedup_key, estado_local, cod_estatus_solicitud, estado_solicitud_sat, creada_en, "
                             "envio_iniciado_en, enviada_en, ultima_verificacion_en, siguiente_verificacion_en) "
                             "VALUES (%1, %2, %3, 'emitidos', 'SolicitaDescargaEmitidos', %4, %4, "
                             "'2026-01-01T00:00:00', '2026-01-31T23:59:59', %5, 'Enviada', '5000', 'EnProceso', "
                             "%6, %6, %6, %6, %6)")
                             .arg(citar(solicitud), citar(perfil), citar(solicitud.toUpper()), citar(kRfc),
                                  citar(DedupKey::calcularV1(solicitud.toUtf8()).texto()), citar(kAhora)))
                 .isEmpty()) {
            return false;
        }
        for (int i = 1; i <= n; ++i) {
            const QString id = uuid::generarCanonico();
            const QString rel = QStringLiteral("%1/2026-01/%2/PAQ_%3.zip").arg(kRfc, solicitud).arg(i);
            const QString abs = QDir(raiz).filePath(rel);
            const QByteArray contenido = zipSintetico(i);
            if (!insertarPaqueteFixture(prov(), id, solicitud, QStringLiteral("PAQ_%1").arg(i),
                                        QStringLiteral("Descargado"))
                     .isEmpty()
                || !ejecutarSql(prov(), QStringLiteral("UPDATE paquete_solicitud SET ruta_local = %1 WHERE id = %2")
                                            .arg(citar(rel), citar(id)))
                        .isEmpty()
                || !escribir(abs, contenido)) {
                return false;
            }
            const bool indexado = i < n;
            const qint64 ms = indexado ? kMtimeIndexadoMs + i : timestamp::desdeTexto(kAhora)->toMSecsSinceEpoch();
            if (!fijarMtime(abs, ms)) {
                return false;
            }
            if (indexado
                && !ejecutarSql(prov(), QStringLiteral("INSERT INTO indice_cfdi_paquete (paquete_solicitud_id, estado, "
                                                       "tamano_bytes, modificado_en_ms, sha256, version_parser, "
                                                       "actualizado_en) VALUES (%1, 'Indexado', %2, %3, %4, 1, %5)")
                                            .arg(citar(id))
                                            .arg(contenido.size())
                                            .arg(ms)
                                            .arg(citar(QString::fromLatin1(QCryptographicHash::hash(
                                                                               contenido, QCryptographicHash::Sha256)
                                                                               .toHex())),
                                                 citar(kAhora)))
                        .isEmpty()) {
                return false;
            }
            zips.append(rel);
            shas.append(sha(abs));
            mtimes.append(mtimeMs(abs));
        }
        return escribir(QDir(datos).filePath(QStringLiteral("preferencias.ini")),
                        QByteArrayLiteral("[facturas]\nultimoPerfil=") + perfil.toUtf8()
                            + "\n[respaldo]\nidInstalacion=" + kInstalacionA.toString(QUuid::WithoutBraces).toUtf8()
                            + "\n");
    }
};

// Captura (T025.2) -> AFB1 (T025.1) en `archivo`.
bool respaldar(crypto::ContenedorRespaldoOpenSsl& cr, const KekRespaldo& pw, const KekRespaldo& rec, Origen& a,
               const QString& archivo)
{
    auto f = a.fuente->capturar();
    f.waitForFinished();
    if (f.resultCount() == 0 || !f.result()) {
        return false;
    }
    const CapturaRespaldo captura = f.result().valor();
    bool ok = false;
    {
        QFile salida(archivo);
        if (salida.open(QIODevice::WriteOnly)) {
            auto esc = cr.crearEscritor(salida, pw, rec, datosNuevoRespaldo(captura, kInstalacionA, a.reloj.ahora()));
            ok = esc && alimentarEscritor(*esc.valor(), captura) && esc.valor()->finalizar();
        }
    }
    (void)a.fuente->limpiar(captura);
    return ok;
}

// AFB1 armado a mano (fixtures de esquema): base dada y metadatos con `esquema`.
bool respaldoManual(crypto::ContenedorRespaldoOpenSsl& cr, const KekRespaldo& pw, const KekRespaldo& rec,
                    const QString& base, int esquema, const QString& archivo)
{
    MetadatosCaptura m;
    m.versionEsquema = esquema;
    m.versionApp = QStringLiteral("0.0.9");
    m.capturadaEnUtc = QDateTime(QDate(2026, 9, 1), QTime(8, 0), QTimeZone::UTC);
    QFile salida(archivo);
    if (!salida.open(QIODevice::WriteOnly)) {
        return false;
    }
    auto esc = cr.crearEscritor(salida, pw, rec, DatosNuevoRespaldo{kInstalacionA, m.capturadaEnUtc, m.serializar()});
    return esc && esc.valor()->agregarArchivo(kRutaRespaldoBase, QFileInfo(base).absolutePath(), QFileInfo(base).fileName())
        && esc.valor()->finalizar();
}

// --- Instalacion de destino (B): otro HOME / --data-dir con datos propios ---

struct Destino {
    QTemporaryDir dir;
    QString datos;
    QString raiz;
    QString base;
    QString zipViejo;
    QByteArray shaBaseVieja;
    QByteArray prefsViejas;
    mutable fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 22), QTime(9, 30), QTimeZone::UTC)};

    // `raizRelativa` vacia = <datos>/paquetes (--data-dir).
    bool preparar(const QString& raizAbs = {})
    {
        datos = dir.filePath(QStringLiteral("otro-home/datos"));
        raiz = raizAbs.isEmpty() ? QDir(datos).filePath(QStringLiteral("paquetes")) : raizAbs;
        base = QDir(datos).filePath(QStringLiteral("satcfdi.sqlite3"));
        if (!dir.isValid() || !QDir().mkpath(raiz) || !QDir().mkpath(datos) || !inicializarBaseSqlite(base)) {
            return false;
        }
        {
            SqliteConnectionProvider prov(base);
            const QString perfil = uuid::generarCanonico();
            if (!insertarPerfilFixture(prov, perfil, kRfcOtro).isEmpty()) {
                return false;
            }
            prov.cerrarConexionDelHiloActual();
        }
        zipViejo = QDir(raiz).filePath(QStringLiteral("BBB020202BBB/2025-12/viejo/PAQ_V.zip"));
        prefsViejas = QByteArrayLiteral("[facturas]\nultimoPerfil=") + uuid::generarCanonico().toUtf8()
            + "\n\n[respaldo]\nidInstalacion=" + kIdInstalacionB.toUtf8() + "\ndiario=true\n";
        if (!escribir(zipViejo, "PK-viejo") || !escribir(QDir(datos).filePath(QStringLiteral("credentials/c.bin")), "x")
            || !escribir(QDir(datos).filePath(QStringLiteral("respaldo/publicados.json")), "{\"publicados\":[]}")
            || !escribir(QDir(datos).filePath(QStringLiteral("respaldo/parciales/p.partial")), "parcial")
            || !escribir(QDir(datos).filePath(QStringLiteral("preferencias.ini")), prefsViejas)) {
            return false;
        }
        shaBaseVieja = sha(base);
        return true;
    }

    QString marcador() const { return marcador_restauracion::rutaArchivo(datos); }

    RestauracionArranque::Opciones opcionesArranque() const
    {
        RestauracionArranque::Opciones o;
        o.directorioDatos = datos;
        o.raicesPermitidas = {raiz};
        o.reloj = reloj.funcion();
        return o;
    }

    // Datos actuales intactos (nada se toco).
    bool intacto() const
    {
        QFile prefs(QDir(datos).filePath(QStringLiteral("preferencias.ini")));
        return sha(base) == shaBaseVieja && QFileInfo::exists(zipViejo)
            && QFileInfo::exists(QDir(datos).filePath(QStringLiteral("credentials/c.bin"))) && prefs.open(QIODevice::ReadOnly)
            && prefs.readAll() == prefsViejas;
    }
};

// Servicio con dependencias falsas de detencion, relanzamiento y salida.
struct Servicio {
    std::atomic<int> detenciones{0};
    std::atomic<int> relanzamientos{0};
    std::atomic<int> salidas{0};
    bool relanzamientoProgramado = true;
    std::function<QFuture<void>()> detener = [] { return QtFuture::makeReadyVoidFuture(); };
    std::unique_ptr<RestauracionService> s;

    Servicio(std::shared_ptr<ContenedorRespaldo> cr, const Destino& b,
             std::shared_ptr<BaseRestauracion> base = std::make_shared<SqliteBaseRestauracion>(),
             std::function<qint64(const QString&)> espacioLibre = {})
    {
        RestauracionService::Dependencias d;
        d.contenedor = std::move(cr);
        d.area = std::make_shared<AreaRestauracionLocal>(AreaRestauracionLocal::Opciones{b.datos, b.raiz, espacioLibre});
        d.base = std::move(base);
        d.detenerApp = [this] {
            ++detenciones;
            return detener();
        };
        d.relanzar = [this] {
            ++relanzamientos;
            return relanzamientoProgramado;
        };
        d.salir = [this] { ++salidas; };
        s = std::make_unique<RestauracionService>(std::move(d));
    }

    ~Servicio()
    {
        if (s) {
            s->cerrar();
            (void)QTest::qWaitFor([&] { return !s->enCurso(); }, 30000);
        }
    }

    Resultado<ResumenRestauracion, ErrorRestauracion> preparar(std::shared_ptr<OrigenRestauracion> origen,
                                                               const QString& secreto,
                                                               PeticionRestauracion::Credencial tipo =
                                                                   PeticionRestauracion::Credencial::Automatica)
    {
        PeticionRestauracion p;
        p.origen = std::move(origen);
        p.credencial = tipo;
        p.secreto = std::make_shared<BufferSecreto>(BufferSecreto::desdeTexto(secreto));
        auto f = s->preparar(std::move(p));
        if (!QTest::qWaitFor([&] { return f.isFinished(); }, 120000) || f.resultCount() == 0) {
            return Resultado<ResumenRestauracion, ErrorRestauracion>::fallo(
                ErrorRestauracion::de(ErrorRestauracion::Categoria::Interno, QStringLiteral("timeout")));
        }
        (void)QTest::qWaitFor([&] { return !s->enCurso(); }, 30000);
        return f.result();
    }

    ResultadoSolicitudRestauracion solicitar()
    {
        auto f = s->solicitar();
        if (!QTest::qWaitFor([&] { return f.isFinished(); }, 60000) || f.resultCount() == 0) {
            return {};
        }
        QTest::qWait(10); // salir() llega encolado
        return f.result();
    }
};

} // namespace

class TestRestauracion : public QObject {
    Q_OBJECT

private:
    std::shared_ptr<crypto::ContenedorRespaldoOpenSsl> cr = std::make_shared<crypto::ContenedorRespaldoOpenSsl>();
    KekRespaldo kekPw;
    KekRespaldo kekRec;
    QString claveFormateada;

    std::shared_ptr<OrigenRestauracion> archivo(const QString& ruta)
    {
        return std::make_shared<OrigenRestauracionArchivo>(ruta);
    }

    // Origen con 3 ZIP respaldado en `afbak` (dentro de `a.dir`).
    bool origenConTresZip(Origen& a, QString& afbak)
    {
        if (!a.preparar() || !a.poblar(3)) {
            return false;
        }
        afbak = a.dir.filePath(QStringLiteral("nube/AFB1-20261021.afbak"));
        QDir().mkpath(QFileInfo(afbak).absolutePath());
        return respaldar(*cr, kekPw, kekRec, a, afbak);
    }

    // Prepara + solicita en `b`; devuelve false si algo no salio como se espera.
    bool hastaSolicitado(Destino& b, const QString& afbak)
    {
        Servicio svc(cr, b);
        auto r = svc.preparar(archivo(afbak), kContrasena);
        if (!r) {
            qWarning("preparar: %s", qPrintable(r.error().diagnostico));
            return false;
        }
        const auto s = svc.solicitar();
        MarcadorRestauracion m;
        return s.estado == ResultadoSolicitudRestauracion::Estado::Relanzando
            && marcador_restauracion::leer(b.datos, m) == marcador_restauracion::Lectura::Leido
            && m.fase == MarcadorRestauracion::Fase::Solicitado;
    }

    void verificarRestaurado(const Destino& b, const Origen& a)
    {
        for (qsizetype i = 0; i < a.zips.size(); ++i) {
            const QString z = QDir(b.raiz).filePath(a.zips.at(i));
            QCOMPARE(sha(z), a.shas.at(i));
            QCOMPARE(mtimeMs(z), a.mtimes.at(i));
        }
        QVERIFY(!QFileInfo::exists(b.zipViejo));
        QCOMPARE(consultar(b.base, QStringLiteral("PRAGMA integrity_check")).toString(), QStringLiteral("ok"));
        QCOMPARE(consultar(b.base, QStringLiteral("SELECT rfc FROM perfil_sat")).toString(), kRfc);
        QCOMPARE(consultar(b.base, QStringLiteral("SELECT COUNT(*) FROM credencial_sat")).toInt(), 0);
        QCOMPARE(consultar(b.base, QStringLiteral("SELECT COUNT(*) FROM aviso_vencimiento_efirma")).toInt(), 0);
        QVERIFY(!QFileInfo::exists(b.marcador()));
        QVERIFY(!QFileInfo::exists(QDir(b.datos).filePath(QStringLiteral("credentials"))));
    }

private slots:
    void initTestCase()
    {
        auto sal = cr->generarSal();
        QVERIFY(sal);
        auto k = cr->derivarKekContrasena(BufferSecreto::desdeTexto(kContrasena),
                                          ParametrosKdf::predeterminados(sal.valor()));
        QVERIFY(k);
        kekPw = std::move(k).valor();
        auto clave = cr->generarClaveRecuperacion();
        QVERIFY(clave);
        auto texto = cr->formatearClaveRecuperacion(clave.valor());
        QVERIFY(texto);
        claveFormateada = QString::fromLatin1(reinterpret_cast<const char*>(texto.valor().datos()),
                                              static_cast<qsizetype>(texto.valor().tamano()));
        auto r = cr->derivarKekRecuperacion(clave.valor());
        QVERIFY(r);
        kekRec = std::move(r).valor();
    }

    // Ida y vuelta con 3 ZIP en otro HOME/--data-dir: SHA-256 y mtime
    // identicos, integrity_check ok, e.firma fuera, seccion `respaldo`,
    // idInstalacion y respaldo/ del equipo conservados.
    void idaYVueltaConTresZipEnOtroHome()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());

        Servicio svc(cr, b);
        auto r = svc.preparar(archivo(afbak), kContrasena, PeticionRestauracion::Credencial::Contrasena);
        QVERIFY2(r, r ? "" : qPrintable(r.error().diagnostico));
        const ResumenRestauracion& res = r.valor();
        // Fecha e id autenticados (los de la cabecera del respaldo).
        QFile f(afbak);
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto cab = cr->autenticarCabecera(f, kekPw);
        QVERIFY(cab);
        QCOMPARE(res.idRespaldo, cab.valor().idRespaldo);
        QCOMPARE(res.creadoEnUtc, cab.valor().creadoEnUtc);
        QCOMPARE(res.idInstalacionRespaldo, kInstalacionA);
        QCOMPARE(res.esquemaRespaldo, 7);
        QCOMPARE(res.esquemaFinal, 7);
        QVERIFY(!res.migrada);
        QCOMPARE(res.perfiles, 1);
        QCOMPARE(res.solicitudes, 1);
        QCOMPARE(res.paquetes, 3);
        QCOMPARE(res.faltantes, 0);
        QCOMPARE(res.credencialesBorradas, 1);
        QCOMPARE(res.mtimesConservados, 3);
        // Preparar no toca los datos actuales.
        QVERIFY(b.intacto());
        QVERIFY(svc.s->preparada());
        {
            // R8: stagings fuera de Time Machine; la extraccion con .noindex.
            MarcadorRestauracion m;
            QCOMPARE(marcador_restauracion::leer(b.datos, m), marcador_restauracion::Lectura::Leido);
            QVERIFY(m.stagingExtraccion.endsWith(QLatin1String(".noindex")));
            QVERIFY(excluidoDeRespaldo(m.stagingDatos));
            QVERIFY(excluidoDeRespaldo(m.stagingExtraccion));
        }

        const auto s = svc.solicitar();
        QCOMPARE(s.estado, ResultadoSolicitudRestauracion::Estado::Relanzando);
        QCOMPARE(svc.detenciones.load(), 1);
        QCOMPARE(svc.relanzamientos.load(), 1);
        QCOMPARE(svc.salidas.load(), 1);
        QVERIFY(b.intacto());

        // Siguiente arranque.
        const auto ap = RestauracionArranque(b.opcionesArranque()).aplicar();
        QCOMPARE(ap.estado, EstadoArranque::Aplicada);
        QCOMPARE(ap.idRespaldo, res.idRespaldo);
        verificarRestaurado(b, a);
        // pre-restauracion intacta: base, credentials/ y paquetes anteriores.
        QCOMPARE(QFileInfo(ap.preDatos).fileName(), QStringLiteral("pre-restauracion-20261022T093000Z"));
        QCOMPARE(sha(QDir(ap.preDatos).filePath(QStringLiteral("satcfdi.sqlite3"))), b.shaBaseVieja);
        QVERIFY(QFileInfo::exists(QDir(ap.preDatos).filePath(QStringLiteral("credentials/c.bin"))));
        QVERIFY(excluidoDeRespaldo(ap.preDatos));
        QVERIFY(QFileInfo::exists(QDir(ap.prePaquetes).filePath(QStringLiteral("paquetes/BBB020202BBB/2025-12/viejo/PAQ_V.zip"))));
        // Preferencias: lista blanca restaurada; seccion `respaldo` del equipo.
        QSettings prefs(QDir(b.datos).filePath(QStringLiteral("preferencias.ini")), QSettings::IniFormat);
        QCOMPARE(prefs.value(QStringLiteral("facturas/ultimoPerfil")).toString(), a.perfil);
        QCOMPARE(prefs.value(QStringLiteral("respaldo/idInstalacion")).toString(), kIdInstalacionB);
        QCOMPARE(prefs.value(QStringLiteral("respaldo/diario")).toString(), QStringLiteral("true"));
        QVERIFY(QFileInfo::exists(QDir(b.datos).filePath(QStringLiteral("respaldo/publicados.json"))));
        QVERIFY(QFileInfo::exists(QDir(b.datos).filePath(QStringLiteral("respaldo/parciales/p.partial"))));
        // Sin stagings; solo el registro de restauraciones aplicadas (D22).
        QCOMPARE(entradasBajo(marcador_restauracion::rutaCarpeta(b.datos)),
                 QStringList{QString::fromLatin1(datos_previos_restauracion::kArchivoRegistro)});
        for (const QString& e : QDir(QFileInfo(QFileInfo(b.raiz).canonicalFilePath()).absolutePath())
                                    .entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
            QVERIFY2(!e.startsWith(QLatin1String(".restauracion-")), qPrintable(e));
        }
        // El bootstrap normal abre la base restaurada (WAL, sin migraciones).
        AppBootstrapper::Opciones o;
        o.directorioDatos = b.datos;
        const auto arranque = AppBootstrapper(o).preparar();
        QVERIFY(arranque);
        QCOMPARE(arranque.valor().informe.migracion.aplicadas, QList<int>());
    }

    void idaYVueltaBaseVacia()
    {
        Origen a;
        QVERIFY(a.preparar());
        const QString afbak = a.dir.filePath(QStringLiteral("vacio.afbak"));
        QVERIFY(respaldar(*cr, kekPw, kekRec, a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        Servicio svc(cr, b);
        auto r = svc.preparar(archivo(afbak), kContrasena);
        QVERIFY2(r, r ? "" : qPrintable(r.error().diagnostico));
        QCOMPARE(r.valor().perfiles, 0);
        QCOMPARE(r.valor().paquetes, 0);
        QCOMPARE(svc.solicitar().estado, ResultadoSolicitudRestauracion::Estado::Relanzando);
        const auto ap = RestauracionArranque(b.opcionesArranque()).aplicar();
        QCOMPARE(ap.estado, EstadoArranque::Aplicada);
        QCOMPARE(consultar(b.base, QStringLiteral("PRAGMA integrity_check")).toString(), QStringLiteral("ok"));
        QCOMPARE(consultar(b.base, QStringLiteral("SELECT COUNT(*) FROM perfil_sat")).toInt(), 0);
        QVERIFY(QFileInfo(b.raiz).isDir());
        QCOMPARE(entradasBajo(b.raiz), QStringList());
        // ultimoPerfil del equipo apuntaba a la base anterior: se retira.
        QSettings prefs(QDir(b.datos).filePath(QStringLiteral("preferencias.ini")), QSettings::IniFormat);
        QVERIFY(!prefs.contains(QStringLiteral("facturas/ultimoPerfil")));
        QCOMPARE(prefs.value(QStringLiteral("respaldo/idInstalacion")).toString(), kIdInstalacionB);
    }

    void claveDeRecuperacionYCredencialIncorrecta()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        {
            Servicio svc(cr, b);
            auto mal = svc.preparar(archivo(afbak), QStringLiteral("otra-contrasena"));
            QVERIFY(!mal);
            QCOMPARE(mal.error().categoria, ErrorRestauracion::Categoria::CredencialIncorrecta);
            QVERIFY(!QFileInfo::exists(b.marcador()));
            QVERIFY(b.intacto());
            // La clave de recuperacion tecleada (minusculas, sin guiones).
            QString tecleada = claveFormateada.toLower();
            tecleada.remove(QLatin1Char('-'));
            auto ok = svc.preparar(archivo(afbak), tecleada);
            QVERIFY2(ok, ok ? "" : qPrintable(ok.error().diagnostico));
            QCOMPARE(ok.valor().paquetes, 3);
            // Descartar retira el staging y el marcador.
            auto d = svc.s->descartar();
            QVERIFY(QTest::qWaitFor([&] { return d.isFinished(); }, 30000));
            QVERIFY(!QFileInfo::exists(b.marcador()));
            QVERIFY(!svc.s->preparada());
        }
        QVERIFY(b.intacto());
    }

    // Esquema mas nuevo: rechazado antes de tocar nada (por metadatos y por
    // la base). Esquema anterior: migrado en el staging.
    void esquemaMasNuevoSeRechazaYViejoSeMigra()
    {
        QTemporaryDir fx;
        QVERIFY(fx.isValid());
        auto embebidas = migracionesSqliteEmbebidas();
        QVERIFY(embebidas);
        // Base con el esquema de T014 (001-005) y una e.firma registrada.
        const QString vieja = fx.filePath(QStringLiteral("vieja/satcfdi.sqlite3"));
        QDir().mkpath(QFileInfo(vieja).absolutePath());
        QVERIFY(inicializarBaseSqlite(vieja, embebidas.valor().mid(0, 5)));
        {
            SqliteConnectionProvider prov(vieja);
            const QString perfil = uuid::generarCanonico();
            QCOMPARE(insertarPerfilFixture(prov, perfil, kRfc), QString());
            QCOMPARE(ejecutarSql(prov, QStringLiteral("INSERT INTO credencial_sat (id, perfil_sat_id, certificado_ref, "
                                                      "llave_privada_ref, contrasena_ref, registrada_en, actualizada_en) "
                                                      "VALUES (%1, %2, 'a', 'b', 'c', %3, %3)")
                                           .arg(citar(uuid::generarCanonico()), citar(perfil), citar(kAhora))),
                     QString());
            QCOMPARE(ejecutarSql(prov, QStringLiteral("PRAGMA journal_mode=DELETE")), QString());
            prov.cerrarConexionDelHiloActual();
        }
        // Base "del futuro": version 8 registrada.
        const QString futura = fx.filePath(QStringLiteral("futura/satcfdi.sqlite3"));
        QDir().mkpath(QFileInfo(futura).absolutePath());
        QVERIFY(inicializarBaseSqlite(futura));
        {
            SqliteConnectionProvider prov(futura, OpcionesConexionSqlite{5000, false});
            QCOMPARE(ejecutarSql(prov, QStringLiteral("INSERT INTO schema_migrations (version, aplicada_en) VALUES "
                                                      "(8, %1)")
                                           .arg(citar(kAhora))),
                     QString());
            QCOMPARE(ejecutarSql(prov, QStringLiteral("PRAGMA journal_mode=DELETE")), QString());
            prov.cerrarConexionDelHiloActual();
        }
        const QString afViejo = fx.filePath(QStringLiteral("viejo.afbak"));
        const QString afFuturoMeta = fx.filePath(QStringLiteral("futuro-meta.afbak"));
        const QString afFuturoBase = fx.filePath(QStringLiteral("futuro-base.afbak"));
        QVERIFY(respaldoManual(*cr, kekPw, kekRec, vieja, 5, afViejo));
        QVERIFY(respaldoManual(*cr, kekPw, kekRec, futura, 8, afFuturoMeta));
        QVERIFY(respaldoManual(*cr, kekPw, kekRec, futura, 7, afFuturoBase)); // metadatos que mienten

        Destino b;
        QVERIFY(b.preparar());
        Servicio svc(cr, b);
        for (const QString& af : {afFuturoMeta, afFuturoBase}) {
            auto r = svc.preparar(archivo(af), kContrasena);
            QVERIFY(!r);
            QCOMPARE(r.error().categoria, ErrorRestauracion::Categoria::EsquemaMasNuevo);
            QVERIFY(b.intacto());
            QVERIFY(!QFileInfo::exists(b.marcador()));
            QCOMPARE(entradasBajo(marcador_restauracion::rutaCarpeta(b.datos)), QStringList());
        }
        QVERIFY(!svc.s->preparada());
        QCOMPARE(svc.solicitar().estado, ResultadoSolicitudRestauracion::Estado::Rechazada);
        QCOMPARE(svc.detenciones.load(), 0);

        auto r = svc.preparar(archivo(afViejo), kContrasena);
        QVERIFY2(r, r ? "" : qPrintable(r.error().diagnostico));
        QCOMPARE(r.valor().esquemaRespaldo, 5);
        QCOMPARE(r.valor().esquemaFinal, 7);
        QVERIFY(r.valor().migrada);
        QCOMPARE(r.valor().credencialesBorradas, 1);
        QCOMPARE(svc.solicitar().estado, ResultadoSolicitudRestauracion::Estado::Relanzando);
        QCOMPARE(RestauracionArranque(b.opcionesArranque()).aplicar().estado, EstadoArranque::Aplicada);
        QCOMPARE(consultar(b.base, QStringLiteral("SELECT MAX(version) FROM schema_migrations")).toInt(), 7);
        QCOMPARE(consultar(b.base, QStringLiteral("SELECT COUNT(*) FROM credencial_sat")).toInt(), 0);
        QCOMPARE(consultar(b.base, QStringLiteral("PRAGMA integrity_check")).toString(), QStringLiteral("ok"));
    }

    // Tras restaurar: perfil SinCredencial ("requiere registrar e.firma") y
    // ninguna llamada SAT aunque haya una solicitud por verificar.
    void sinLlamadasSatHastaRegistrarLaEFirma()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        QVERIFY(hastaSolicitado(b, afbak));
        QCOMPARE(RestauracionArranque(b.opcionesArranque()).aplicar().estado, EstadoArranque::Aplicada);

        AppBootstrapper::Opciones o;
        o.directorioDatos = b.datos;
        const auto arranque = AppBootstrapper(o).preparar();
        QVERIFY(arranque);
        fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 22), QTime(10, 0), QTimeZone::UTC)};
        fakes::FakeProgramador pe{reloj};
        fakes::FakeProgramador pw{reloj};
        fakes::FakeProgramador pv{reloj};
        fakes::FakeProgramador pr{reloj};
        fakes::FakeSecretStore secretos;
        fakes::FakeSatGateway gateway;
        gateway.ahora = reloj.funcion();
        OpcionesMonitoreo m;
        m.satGateway = &gateway;
        m.reloj = reloj.funcion();
        m.programadorEjecutor = &pe;
        m.programadorWorker = &pw;
        m.programadorVencimiento = &pv;
        m.programadorRespaldo = &pr;
        m.raizPaquetes = arranque.valor().raizPaquetes;
        AppCompositionRoot root(arranque.valor().rutaBase, secretos, m);
        const auto perfil = PerfilId::desdeTexto(a.perfil);
        QVERIFY(perfil);
        auto estado = root.credenciales().obtenerEstado(*perfil);
        QVERIFY(QTest::qWaitFor([&] { return estado.isFinished(); }, 10000));
        QVERIFY(estado.result());
        QCOMPARE(estado.result().valor(), EstadoCredencial::SinCredencial);
        root.iniciarMonitoreo();
        const auto solicitud = SolicitudId::desdeTexto(a.solicitud);
        QVERIFY(solicitud);
        root.worker().verificarAhora(*solicitud);
        pw.avanzar(std::chrono::minutes(30));
        QTest::qWait(300);
        using Op = fakes::FakeSatGateway::Op;
        QCOMPARE(gateway.llamadas(Op::Autenticar) + gateway.llamadas(Op::Crear) + gateway.llamadas(Op::Verificar)
                     + gateway.llamadas(Op::Descargar),
                 0);
        auto fin = root.ejecutor().detener(std::chrono::milliseconds(0));
        QVERIFY(QTest::qWaitFor([&] { return fin.isFinished(); }, 10000));
    }

    // Con una operacion SAT activa: rechaza nuevas, cancela, espera el
    // cierre, no aplica resultados posteriores y despues escribe el marcador.
    void restaurarConUnaOperacionSatActiva()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());

        // Ejecutor real sobre la base de B con una solicitud Creada.
        auto p = std::make_unique<SqlitePersistencia>(b.base);
        const QString perfil = uuid::generarCanonico();
        QCOMPARE(insertarPerfilFixture(p->proveedor(), perfil, kRfcPerfil), QString());
        const SolicitudFixture sf = fixture(perfil, DedupKey::calcularV1("restaurar"));
        QCOMPARE(insertarSolicitudFixture(p->proveedor(), sf), QString());
        p->cerrarConexionDelHiloActual();
        RegexLogSanitizer sanitizer;
        fakes::FakeOperacionesSat sat;
        fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 22), QTime(9, 0), QTimeZone::UTC)};
        fakes::FakeProgramador programador{reloj};
        SqlitePersistencia* persistencia = p.get();
        PuertosEjecutor puertos{p->solicitudes(), p->logs(), p->operaciones(), p->unidadDeTrabajo(), sanitizer, sat,
                                [persistencia] { persistencia->cerrarConexionDelHiloActual(); }};
        auto ejecutor = std::make_unique<OperacionExecutor>(std::move(puertos), reloj.funcion(), programador);
        sat.bloquearSiguiente(fakes::FakeOperacionesSat::Op::Enviar, FaseOperacion::AntesDeEnvio);
        const auto id = SolicitudId::desdeTexto(sf.id);
        QVERIFY(id);
        auto activa = ejecutor->enviar(*id);
        QVERIFY(sat.esperarBloqueo());

        Servicio svc(cr, b);
        std::atomic<bool> detenidoAntesDelMarcador{false};
        svc.detener = [&] {
            // Al pedir la detencion aun no hay marcador `solicitado`.
            MarcadorRestauracion m;
            detenidoAntesDelMarcador = marcador_restauracion::leer(b.datos, m) == marcador_restauracion::Lectura::Leido
                && m.fase == MarcadorRestauracion::Fase::Preparado;
            return ejecutor->detener(std::chrono::milliseconds(0));
        };
        auto r = svc.preparar(archivo(afbak), kContrasena);
        QVERIFY2(r, r ? "" : qPrintable(r.error().diagnostico));
        QCOMPARE(sat.activas(), 1); // la operacion sigue activa mientras se prepara
        const auto s = svc.solicitar();
        QCOMPARE(s.estado, ResultadoSolicitudRestauracion::Estado::Relanzando);
        QVERIFY(detenidoAntesDelMarcador.load());
        QVERIFY(QTest::qWaitFor([&] { return activa.isFinished(); }, 10000));
        QVERIFY(activa.result().falla && activa.result().falla->cancelada);
        QCOMPARE(sat.resultados(), QStringList{QStringLiteral("enviar:falla:AntesDeEnvio:cancelada")});
        // Nuevas operaciones rechazadas.
        auto nueva = ejecutor->enviar(*id);
        QVERIFY(QTest::qWaitFor([&] { return nueva.isFinished(); }, 10000));
        QCOMPARE(nueva.result().desenlace, ResultadoOperacion::Desenlace::Rechazada);
        QCOMPARE(sat.llamadas().size(), 1);
        // La cancelacion se aplico con la regla de su fase (Creada): ningun
        // resultado SAT tardio quedo en la base actual.
        QCOMPARE(consultar(b.base, QStringLiteral("SELECT estado_local FROM solicitud_masiva WHERE id = '%1'").arg(sf.id))
                     .toString(),
                 QStringLiteral("Creada"));
        ejecutor.reset();
        p.reset();
        MarcadorRestauracion m;
        QCOMPARE(marcador_restauracion::leer(b.datos, m), marcador_restauracion::Lectura::Leido);
        QCOMPARE(m.fase, MarcadorRestauracion::Fase::Solicitado);
        QCOMPARE(svc.relanzamientos.load(), 1);
        QCOMPARE(svc.salidas.load(), 1);
        QCOMPARE(RestauracionArranque(b.opcionesArranque()).aplicar().estado, EstadoArranque::Aplicada);
        verificarRestaurado(b, a);
    }

    // Sin relanzamiento programado: el marcador queda y se aplica al abrir.
    void sinRelanzamientoElMarcadorQueda()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        Servicio svc(cr, b);
        svc.relanzamientoProgramado = false;
        QVERIFY(svc.preparar(archivo(afbak), kContrasena));
        QCOMPARE(svc.solicitar().estado, ResultadoSolicitudRestauracion::Estado::SinRelanzamiento);
        QCOMPARE(svc.salidas.load(), 1);
        QCOMPARE(RestauracionArranque(b.opcionesArranque()).aplicar().estado, EstadoArranque::Aplicada);
    }

    // Lector bloqueante (archivo aun en la nube): la UI sigue y cancelar
    // resuelve de inmediato; al liberarse, el hilo descarta su staging.
    void lectorBloqueanteSeCancelaSinCongelarLaUi()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        auto control = std::make_shared<DispositivoLento::Control>();
        control->bloquear = true;
        Servicio svc(cr, b);
        PeticionRestauracion p;
        p.origen = std::make_shared<OrigenLento>(afbak, control);
        p.secreto = std::make_shared<BufferSecreto>(BufferSecreto::desdeTexto(kContrasena));
        auto f = svc.s->preparar(std::move(p));
        QVERIFY(QTest::qWaitFor([&] { return control->lecturasBloqueadas.load() > 0; }, 10000));
        // El hilo grafico sigue atendiendo eventos.
        int ticks = 0;
        QTimer t;
        QObject::connect(&t, &QTimer::timeout, [&] { ++ticks; });
        t.start(5);
        QTest::qWait(100);
        QVERIFY(ticks >= 5);
        QVERIFY(!f.isFinished());
        QElapsedTimer reloj;
        reloj.start();
        svc.s->cancelar();
        QVERIFY(QTest::qWaitFor([&] { return f.isFinished(); }, 1000));
        QVERIFY(reloj.elapsed() < 1000);
        QCOMPARE(f.result().error().categoria, ErrorRestauracion::Categoria::Cancelado);
        QVERIFY(svc.s->enCurso()); // el hilo sigue bloqueado
        QVERIFY(debeTerminarSinDestructores()); // R2: main saldria con _exit()
        PeticionRestauracion otra;
        otra.origen = archivo(afbak);
        otra.secreto = std::make_shared<BufferSecreto>(BufferSecreto::desdeTexto(kContrasena));
        auto f2 = svc.s->preparar(std::move(otra));
        QVERIFY(f2.isFinished());
        QCOMPARE(f2.result().error().categoria, ErrorRestauracion::Categoria::EnCurso);
        control->liberar();
        QVERIFY(QTest::qWaitFor([&] { return !svc.s->enCurso(); }, 10000));
        QVERIFY(QTest::qWaitFor([&] { return !debeTerminarSinDestructores(); }, 10000));
        QVERIFY(!QFileInfo::exists(b.marcador()));
        QCOMPARE(entradasBajo(marcador_restauracion::rutaCarpeta(b.datos)), QStringList());
        QVERIFY(b.intacto());
    }

    // Lector lento: la extraccion avanza con progreso sin bloquear la UI y se
    // puede cancelar a mitad (el contenedor lo observa por bloque).
    void lectorLentoAvanzaYSeCancelaAMitad()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        auto control = std::make_shared<DispositivoLento::Control>();
        control->esperaMs = 2;
        control->bytesPorLectura = 4096;
        Servicio svc(cr, b);
        int avisos = 0;
        QObject::connect(svc.s.get(), &RestauracionService::progreso, [&](RestauracionService::Fase, quint64, quint64) { ++avisos; });
        auto lento = svc.preparar(std::make_shared<OrigenLento>(afbak, control), kContrasena);
        QVERIFY2(lento, lento ? "" : qPrintable(lento.error().diagnostico));
        QVERIFY(avisos >= 3);
        auto d = svc.s->descartar();
        QVERIFY(QTest::qWaitFor([&] { return d.isFinished(); }, 30000));

        control->esperaMs = 50;
        PeticionRestauracion p;
        p.origen = std::make_shared<OrigenLento>(afbak, control);
        p.secreto = std::make_shared<BufferSecreto>(BufferSecreto::desdeTexto(kContrasena));
        auto f = svc.s->preparar(std::move(p));
        QTest::qWait(1500); // Argon2id y algunos bloques
        svc.s->cancelar();
        QVERIFY(QTest::qWaitFor([&] { return f.isFinished(); }, 1000));
        QCOMPARE(f.result().error().categoria, ErrorRestauracion::Categoria::Cancelado);
        QVERIFY(QTest::qWaitFor([&] { return !svc.s->enCurso(); }, 30000));
        QVERIFY(!QFileInfo::exists(b.marcador()));
        QVERIFY(b.intacto());
    }

    // Reverificacion: un cambio del `.afbak` o del staging entre la
    // verificacion y la sustitucion obliga a verificar de nuevo.
    void reverificacionAnteUnCambio()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        {
            // (1) El `.afbak` cambia antes de solicitar: nada se detiene.
            Destino b;
            QVERIFY(b.preparar());
            Servicio svc(cr, b);
            QVERIFY(svc.preparar(archivo(afbak), kContrasena));
            QThread::msleep(5);
            QFile f(afbak);
            QVERIFY(f.open(QIODevice::Append));
            f.write("x");
            f.close();
            const auto s = svc.solicitar();
            QCOMPARE(s.estado, ResultadoSolicitudRestauracion::Estado::Rechazada);
            QCOMPARE(s.error->categoria, ErrorRestauracion::Categoria::OrigenCambio);
            QCOMPARE(svc.detenciones.load(), 0);
            QVERIFY(!QFileInfo::exists(b.marcador()));
            QVERIFY(b.intacto());
            QVERIFY(f.resize(f.size() - 1));
        }
        {
            // (2) El staging cambia antes de solicitar.
            Destino b;
            QVERIFY(b.preparar());
            Servicio svc(cr, b);
            QVERIFY(svc.preparar(archivo(afbak), kContrasena));
            MarcadorRestauracion m;
            QCOMPARE(marcador_restauracion::leer(b.datos, m), marcador_restauracion::Lectura::Leido);
            QVERIFY(escribir(QDir(m.stagingExtraccion).filePath(QStringLiteral("paquetes/") + a.zips.first()), "alterado"));
            const auto s = svc.solicitar();
            QCOMPARE(s.estado, ResultadoSolicitudRestauracion::Estado::Rechazada);
            QCOMPARE(s.error->categoria, ErrorRestauracion::Categoria::StagingCambio);
            QCOMPARE(svc.detenciones.load(), 0);
            QVERIFY(b.intacto());
        }
        {
            // (3) El staging cambia despues de solicitar: el arranque lo
            // detecta antes de sustituir y no toca nada.
            Destino b;
            QVERIFY(b.preparar());
            QVERIFY(hastaSolicitado(b, afbak));
            MarcadorRestauracion m;
            QCOMPARE(marcador_restauracion::leer(b.datos, m), marcador_restauracion::Lectura::Leido);
            const QString zip = QDir(m.stagingExtraccion).filePath(QStringLiteral("paquetes/") + a.zips.last());
            QFile f(zip);
            QVERIFY(f.open(QIODevice::Append));
            f.write("y");
            f.close();
            const auto ap = RestauracionArranque(b.opcionesArranque()).aplicar();
            QCOMPARE(ap.estado, EstadoArranque::Descartada);
            QCOMPARE(ap.diagnostico, QStringLiteral("staging_cambio"));
            QVERIFY(b.intacto());
            QVERIFY(!QFileInfo::exists(b.marcador()));
            QVERIFY(!QFileInfo::exists(m.stagingExtraccion));
        }
    }

    // Corte simulado en cada paso del arranque: el siguiente arranque
    // termina sin estado mixto.
    void corteEnCadaFaseEsRecuperable()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        int corte = 0;
        for (;; ++corte) {
            Destino b;
            QVERIFY(b.preparar());
            QVERIFY(hastaSolicitado(b, afbak));
            auto o = b.opcionesArranque();
            int pasos = 0;
            QString pasoCortado;
            o.puntoDeCorte = [&](const char* paso) {
                if (pasos++ == corte) {
                    pasoCortado = QString::fromLatin1(paso);
                    return false;
                }
                return true;
            };
            const auto primero = RestauracionArranque(o).aplicar();
            if (primero.estado == EstadoArranque::Aplicada) {
                verificarRestaurado(b, a);
                break;
            }
            QCOMPARE(primero.estado, EstadoArranque::Interrumpida);
            // El marcador sigue y la app no arrancaria (prepararProceso -> Error).
            QVERIFY2(QFileInfo::exists(b.marcador()), qPrintable(pasoCortado));
            const auto segundo = RestauracionArranque(b.opcionesArranque()).aplicar();
            QVERIFY2(segundo.estado == EstadoArranque::Aplicada, qPrintable(pasoCortado + QLatin1Char(' ') + segundo.diagnostico));
            verificarRestaurado(b, a);
            QCOMPARE(sha(QDir(segundo.preDatos).filePath(QStringLiteral("satcfdi.sqlite3"))), b.shaBaseVieja);
        }
        QVERIFY(corte >= 10);
    }

    // Un paso que falla revierte; un corte durante la reversion la continua.
    void falloAlPromoverRevierteAunConCortes()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        for (int corte = 0;; ++corte) {
            Destino b;
            QVERIFY(b.preparar());
            QVERIFY(hastaSolicitado(b, afbak));
            FileOpsConFallos ops;
            // Falla solo la promocion de `paquetes/` (staging -> raiz).
            ops.fallarRenombrarHacia = QFile::encodeName(QFileInfo(b.raiz).canonicalFilePath());
            ops.siOrigenContiene = QByteArrayLiteral("/.restauracion-");
            auto o = b.opcionesArranque();
            o.ops = &ops;
            int pasos = 0;
            bool revirtiendo = false;
            o.puntoDeCorte = [&](const char* paso) {
                revirtiendo = revirtiendo || QByteArray(paso).startsWith("revertir");
                return !(revirtiendo && pasos++ == corte);
            };
            const auto primero = RestauracionArranque(o).aplicar();
            if (primero.estado == EstadoArranque::Revertida) {
                QVERIFY(b.intacto());
                QVERIFY(!QFileInfo::exists(b.marcador()));
                break;
            }
            QCOMPARE(primero.estado, EstadoArranque::Interrumpida);
            MarcadorRestauracion m;
            QCOMPARE(marcador_restauracion::leer(b.datos, m), marcador_restauracion::Lectura::Leido);
            const auto segundo = RestauracionArranque(b.opcionesArranque()).aplicar();
            if (m.fase != MarcadorRestauracion::Fase::Revirtiendo) {
                // Cortado antes de anotar la reversion: el siguiente arranque
                // reintenta hacia adelante (la falla ya no ocurre).
                QCOMPARE(segundo.estado, EstadoArranque::Aplicada);
                verificarRestaurado(b, a);
                continue;
            }
            QCOMPARE(segundo.estado, EstadoArranque::Revertida);
            QVERIFY(b.intacto());
            QVERIFY(!QFileInfo::exists(m.preDatos) || QDir(m.preDatos).isEmpty());
            QVERIFY(!QFileInfo::exists(m.stagingDatos));
            QVERIFY(!QFileInfo::exists(m.stagingExtraccion));
        }
    }

    // D22: con datos previos sin descartar, el arranque no borra secretos de
    // e.firma del almacen; al descartarlos (solo los propios) si se reconcilia.
    void eFirmaSeConservaHastaDescartarDatosPrevios()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        QVERIFY(hastaSolicitado(b, afbak));
        const auto ap = RestauracionArranque(b.opcionesArranque()).aplicar();
        QCOMPARE(ap.estado, EstadoArranque::Aplicada);
        QCOMPARE(datos_previos_restauracion::pendientes(b.datos), QStringList{ap.preDatos});

        // Ajena (sin marca) y enlazada (a una carpeta con una marca que imita
        // la propia): no cuentan ni se borran.
        const QString ajena = QDir(b.datos).filePath(QStringLiteral("pre-restauracion-ajena"));
        QVERIFY(escribir(QDir(ajena).filePath(QStringLiteral("nota.txt")), "del usuario"));
        const QString objetivo = b.dir.filePath(QStringLiteral("fuera/objetivo"));
        const QString enlace = QDir(b.datos).filePath(QStringLiteral("pre-restauracion-enlace"));
        QVERIFY(escribir(QDir(objetivo).filePath(QStringLiteral("dato.bin")), "fuera"));
        QVERIFY(datos_previos_restauracion::escribirMarca(objetivo, uuid::generarCanonico(), enlace, enlace));
        QVERIFY(QFile::link(objetivo, enlace));
        QCOMPARE(datos_previos_restauracion::pendientes(b.datos).size(), qsizetype(1));

        AppBootstrapper::Opciones o;
        o.directorioDatos = b.datos;
        const auto arranque = AppBootstrapper(o).preparar();
        QVERIFY(arranque);
        fakes::FakeSecretStore secretos;
        const CredencialRef previa = secretos.sembrar(); // e.firma de la base anterior
        OpcionesMonitoreo m;
        m.raizPaquetes = arranque.valor().raizPaquetes;
        fakes::FakeOperacionesSat sat;
        m.operacionesSat = &sat;
        AppCompositionRoot root(arranque.valor().rutaBase, secretos, m);
        auto inicial = root.reconciliacionInicial();
        QVERIFY(QTest::qWaitFor([&] { return inicial.isFinished(); }, 10000));
        QVERIFY(inicial.result());
        QCOMPARE(inicial.result().valor().generacionesEliminadas, 0);
        QCOMPARE(secretos.reconciliaciones(), 0);
        QVERIFY(secretos.existe(previa));
        auto hay = root.hayDatosPrevios();
        QVERIFY(QTest::qWaitFor([&] { return hay.isFinished(); }, 10000));
        QVERIFY(hay.result());

        auto d = root.descartarDatosPrevios();
        QVERIFY(QTest::qWaitFor([&] { return d.isFinished(); }, 30000));
        const ResultadoDescarteDatosPrevios r = d.result();
        QCOMPARE(r.estado, ResultadoDescarteDatosPrevios::Estado::Descartados);
        QCOMPARE(r.carpetasBorradas, 1);
        QCOMPARE(r.carpetasAjenas, 2);
        QCOMPARE(r.generacionesEliminadas, 1);
        QCOMPARE(secretos.reconciliaciones(), 1);
        QVERIFY(!secretos.existe(previa));
        QVERIFY(!QFileInfo::exists(ap.preDatos));
        QVERIFY(QFileInfo::exists(QDir(ajena).filePath(QStringLiteral("nota.txt"))));
        QVERIFY(QFileInfo(enlace).isSymLink());
        QVERIFY(QFileInfo::exists(QDir(objetivo).filePath(QStringLiteral("dato.bin"))));
        auto hay2 = root.hayDatosPrevios();
        QVERIFY(QTest::qWaitFor([&] { return hay2.isFinished(); }, 10000));
        QVERIFY(!hay2.result());
        // Sin datos previos, otro descarte no borra nada.
        auto d2 = root.descartarDatosPrevios();
        QVERIFY(QTest::qWaitFor([&] { return d2.isFinished(); }, 30000));
        QCOMPARE(d2.result().estado, ResultadoDescarteDatosPrevios::Estado::SinDatosPrevios);
        QVERIFY(QFileInfo::exists(QDir(ajena).filePath(QStringLiteral("nota.txt"))));
        auto fin = root.ejecutor().detener(std::chrono::milliseconds(0));
        QVERIFY(QTest::qWaitFor([&] { return fin.isFinished(); }, 10000));
    }

    // Seguridad 1: un marcador manipulado (raiz o pre-restauracion ajenas) no
    // mueve ni borra ninguna carpeta ajena, en ninguna fase.
    void marcadorManipuladoNoTocaCarpetasAjenas_data()
    {
        QTest::addColumn<int>("fase");
        QTest::addColumn<bool>("raizAjena");
        using F = MarcadorRestauracion::Fase;
        for (F f : {F::Preparando, F::Preparado, F::Solicitado, F::PreElegida, F::MoviendoActuales,
                    F::ActualesMovidos, F::Promoviendo, F::Promovido, F::Revirtiendo}) {
            const QByteArray nombre = MarcadorRestauracion::claveFase(f).toUtf8();
            QTest::newRow(nombre + " raiz ajena") << static_cast<int>(f) << true;
            if (static_cast<int>(f) >= static_cast<int>(F::PreElegida)) {
                QTest::newRow(nombre + " pre ajena") << static_cast<int>(f) << false;
            }
        }
    }

    void marcadorManipuladoNoTocaCarpetasAjenas()
    {
        QFETCH(int, fase);
        QFETCH(bool, raizAjena);
        using F = MarcadorRestauracion::Fase;
        Destino b;
        QVERIFY(b.preparar());
        const QString ajenas = b.dir.filePath(QStringLiteral("Users/usuario/Documentos"));
        const QString raizOtra = QDir(ajenas).filePath(QStringLiteral("paquetes"));
        QVERIFY(escribir(QDir(raizOtra).filePath(QStringLiteral("a.zip")), "ajeno"));
        const QString canonAjenas = QFileInfo(ajenas).canonicalFilePath();
        MarcadorRestauracion m;
        m.id = uuid::generarCanonico();
        m.fase = static_cast<F>(fase);
        m.faseAntesDeRevertir = F::ActualesMovidos;
        m.directorioDatos = QDir::cleanPath(b.datos);
        m.raizPaquetes = raizAjena ? QFileInfo(raizOtra).canonicalFilePath() : QFileInfo(b.raiz).canonicalFilePath();
        const QString padre = QFileInfo(m.raizPaquetes).absolutePath();
        m.stagingDatos = QDir(marcador_restauracion::rutaCarpeta(m.directorioDatos)).filePath(QStringLiteral("staging-") + m.id);
        m.stagingExtraccion = QDir(padre).filePath(QStringLiteral(".restauracion-") + m.id + QStringLiteral(".noindex"));
        QVERIFY(escribir(QDir(m.stagingExtraccion).filePath(QStringLiteral("paquetes/x.zip")), "staging"));
        if (fase >= static_cast<int>(F::PreElegida)) {
            m.fechaPre = QStringLiteral("20261022T093000Z");
            // Pre ajena: fuera de la carpeta de datos, con contenido del usuario.
            m.preDatos = QDir(canonAjenas).filePath(QStringLiteral("pre-restauracion-20261022T093000Z"));
            m.prePaquetes = m.preDatos;
            QVERIFY(escribir(QDir(m.preDatos).filePath(QStringLiteral("doc.txt")), "usuario"));
        }
        QVERIFY(marcador_restauracion::escribir(b.datos, m));
        const QStringList antes = entradasBajo(ajenas);
        const auto ap = RestauracionArranque(b.opcionesArranque()).aplicar();
        if (raizAjena && fase <= static_cast<int>(F::Solicitado)) {
            QCOMPARE(ap.estado, EstadoArranque::Descartada); // solo el marcador y el staging de datos
        } else {
            QCOMPARE(ap.estado, EstadoArranque::Error);
        }
        QCOMPARE(entradasBajo(ajenas), antes);
        QVERIFY(QFileInfo::exists(QDir(raizOtra).filePath(QStringLiteral("a.zip"))));
        QVERIFY(b.intacto());
        if (!raizAjena) {
            QVERIFY(QFileInfo::exists(QDir(m.stagingExtraccion).filePath(QStringLiteral("paquetes/x.zip"))));
        }
    }

    // Seguridad 2: objetos de esquema ajenos en la base restaurada.
    void esquemaAjenoEnLaBaseSeRechaza_data()
    {
        QTest::addColumn<QString>("sql");
        QTest::newRow("trigger que reinserta credencial_sat")
            << QStringLiteral("CREATE TRIGGER reinsertar AFTER DELETE ON credencial_sat BEGIN "
                              "INSERT INTO credencial_sat (id, perfil_sat_id, certificado_ref, llave_privada_ref, "
                              "contrasena_ref, registrada_en, actualizada_en) VALUES (OLD.id, OLD.perfil_sat_id, "
                              "OLD.certificado_ref, OLD.llave_privada_ref, OLD.contrasena_ref, OLD.registrada_en, "
                              "OLD.actualizada_en); END");
        QTest::newRow("vista extra") << QStringLiteral("CREATE VIEW extra AS SELECT rfc FROM perfil_sat");
        QTest::newRow("tabla extra") << QStringLiteral("CREATE TABLE extra (x TEXT)");
        QTest::newRow("indice extra") << QStringLiteral("CREATE INDEX ix_extra ON perfil_sat (nombre)");
    }

    void esquemaAjenoEnLaBaseSeRechaza()
    {
        QFETCH(QString, sql);
        QTemporaryDir fx;
        QVERIFY(fx.isValid());
        const QString base = fx.filePath(QStringLiteral("b/satcfdi.sqlite3"));
        QDir().mkpath(QFileInfo(base).absolutePath());
        QVERIFY(inicializarBaseSqlite(base));
        {
            SqliteConnectionProvider prov(base, OpcionesConexionSqlite{5000, false});
            const QString perfil = uuid::generarCanonico();
            QCOMPARE(insertarPerfilFixture(prov, perfil, kRfc), QString());
            QCOMPARE(ejecutarSql(prov, QStringLiteral("INSERT INTO credencial_sat (id, perfil_sat_id, certificado_ref, "
                                                      "llave_privada_ref, contrasena_ref, registrada_en, actualizada_en) "
                                                      "VALUES (%1, %2, 'a', 'b', 'c', %3, %3)")
                                           .arg(citar(uuid::generarCanonico()), citar(perfil), citar(kAhora))),
                     QString());
            QCOMPARE(ejecutarSql(prov, sql), QString());
            QCOMPARE(ejecutarSql(prov, QStringLiteral("PRAGMA journal_mode=DELETE")), QString());
            prov.cerrarConexionDelHiloActual();
        }
        const QString af = fx.filePath(QStringLiteral("ajeno.afbak"));
        QVERIFY(respaldoManual(*cr, kekPw, kekRec, base, 7, af));
        Destino b;
        QVERIFY(b.preparar());
        Servicio svc(cr, b);
        auto r = svc.preparar(archivo(af), kContrasena);
        QVERIFY(!r);
        QCOMPARE(r.error().categoria, ErrorRestauracion::Categoria::EsquemaInvalido);
        QVERIFY(b.intacto());
        QVERIFY(!QFileInfo::exists(b.marcador()));
    }

    // Calidad 1: al retomar desde actuales_movidos o a mitad de promover, un
    // staging (o una parte ya promovida) alterado se detecta y se revierte.
    void stagingAlteradoAlRetomarSeRevierte_data()
    {
        QTest::addColumn<QString>("corte");
        QTest::addColumn<QString>("alterar");
        QTest::newRow("actuales_movidos, zip del staging") << QStringLiteral("fase.promoviendo") << QStringLiteral("zip");
        QTest::newRow("actuales_movidos, base del staging") << QStringLiteral("fase.promoviendo") << QStringLiteral("base");
        QTest::newRow("promoviendo, zip del staging") << QStringLiteral("promover.paquetes") << QStringLiteral("zip");
        QTest::newRow("promoviendo, base promovida") << QStringLiteral("promover.paquetes") << QStringLiteral("base_promovida");
    }

    void stagingAlteradoAlRetomarSeRevierte()
    {
        QFETCH(QString, corte);
        QFETCH(QString, alterar);
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        QVERIFY(hastaSolicitado(b, afbak));
        auto o = b.opcionesArranque();
        o.puntoDeCorte = [&](const char* paso) { return QString::fromLatin1(paso) != corte; };
        QCOMPARE(RestauracionArranque(o).aplicar().estado, EstadoArranque::Interrumpida);
        MarcadorRestauracion m;
        QCOMPARE(marcador_restauracion::leer(b.datos, m), marcador_restauracion::Lectura::Leido);
        QString objetivo;
        if (alterar == QLatin1String("zip")) {
            objetivo = QDir(m.stagingExtraccion).filePath(QStringLiteral("paquetes/") + a.zips.first());
        } else if (alterar == QLatin1String("base")) {
            objetivo = QDir(m.stagingDatos).filePath(QStringLiteral("satcfdi.sqlite3"));
        } else {
            objetivo = b.base; // ya promovida
        }
        QFile f(objetivo);
        QVERIFY2(f.open(QIODevice::Append), qPrintable(alterar));
        f.write("z");
        f.close();
        const auto ap = RestauracionArranque(b.opcionesArranque()).aplicar();
        QCOMPARE(ap.estado, EstadoArranque::Revertida);
        QCOMPARE(ap.diagnostico, QStringLiteral("staging_cambio"));
        QVERIFY(b.intacto());
        QVERIFY(!QFileInfo::exists(b.marcador()));
    }

    // Calidad 3: raiz en otro directorio que los datos, con una colision
    // preexistente en cada lado.
    void preRestauracionNoChocaEnNingunLado()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString padre = QFileInfo(tmp.path()).canonicalFilePath() + QStringLiteral("/Users/usuario/Archivo-Fiscal");
        Destino b;
        QVERIFY(b.preparar(padre + QStringLiteral("/paquetes")));
        const QString enDatos = QDir(b.datos).filePath(QStringLiteral("pre-restauracion-20261022T093000Z"));
        const QString enRaiz = QDir(padre).filePath(QStringLiteral("pre-restauracion-20261022T093000Z-2"));
        QVERIFY(escribir(QDir(enDatos).filePath(QStringLiteral("a")), "1"));
        QVERIFY(escribir(QDir(enRaiz).filePath(QStringLiteral("b")), "2"));
        QVERIFY(hastaSolicitado(b, afbak));
        const auto ap = RestauracionArranque(b.opcionesArranque()).aplicar();
        QCOMPARE(ap.estado, EstadoArranque::Aplicada);
        QCOMPARE(ap.preDatos, QDir(b.datos).filePath(QStringLiteral("pre-restauracion-20261022T093000Z-3")));
        QCOMPARE(ap.prePaquetes, QDir(padre).filePath(QStringLiteral("pre-restauracion-20261022T093000Z-3")));
        QCOMPARE(entradasBajo(enDatos), QStringList{QStringLiteral("a")});
        QCOMPARE(entradasBajo(enRaiz), QStringList{QStringLiteral("b")});
        QVERIFY(QFileInfo::exists(QDir(ap.prePaquetes).filePath(QStringLiteral("paquetes/BBB020202BBB/2025-12/viejo/PAQ_V.zip"))));
        QCOMPARE(sha(QDir(ap.preDatos).filePath(QStringLiteral("satcfdi.sqlite3"))), b.shaBaseVieja);
        QCOMPARE(datos_previos_restauracion::pendientes(b.datos), (QStringList{ap.preDatos, ap.prePaquetes}));
    }

    // R8: sin espacio en el volumen de la raiz no se crea nada.
    void sinEspacioNoSeTocaNada()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        Servicio svc(cr, b, std::make_shared<SqliteBaseRestauracion>(), [](const QString&) { return qint64(1024); });
        auto r = svc.preparar(archivo(afbak), kContrasena);
        QVERIFY(!r);
        QCOMPARE(r.error().categoria, ErrorRestauracion::Categoria::EspacioInsuficiente);
        QVERIFY(!QFileInfo::exists(b.marcador()));
        QVERIFY(b.intacto());
    }

    // Seguridad 4: una marca fabricada (id sin registrar) no cuenta.
    void marcaSinRegistroNoCuenta()
    {
        Destino b;
        QVERIFY(b.preparar());
        const QString falsa = QDir(b.datos).filePath(QStringLiteral("pre-restauracion-20260101T000000Z"));
        QVERIFY(QDir().mkpath(falsa));
        QVERIFY(datos_previos_restauracion::escribirMarca(falsa, uuid::generarCanonico(), falsa, falsa));
        QCOMPARE(datos_previos_restauracion::pendientes(b.datos), QStringList());
        QCOMPARE(datos_previos_restauracion::descartar(b.datos).borradas, 0);
        QVERIFY(QFileInfo::exists(falsa));
    }

    // Seguridad 5: un respaldo hecho despues de restaurar no lleva
    // pre-restauracion ni credentials/.
    void respaldoPosteriorNoIncluyePreRestauracion()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        QVERIFY(hastaSolicitado(b, afbak));
        const auto ap = RestauracionArranque(b.opcionesArranque()).aplicar();
        QCOMPARE(ap.estado, EstadoArranque::Aplicada);
        QVERIFY(QFileInfo::exists(QDir(ap.preDatos).filePath(QStringLiteral("credentials/c.bin"))));
        QVERIFY(inicializarBaseSqlite(b.base));
        SqlitePersistencia p(b.base);
        SqliteCopiaRespaldo copia(p);
        RegexLogSanitizer sanitizer;
        fakes::FakeOperacionesSat sat;
        fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 23), QTime(8, 0), QTimeZone::UTC)};
        fakes::FakeProgramador programador{reloj};
        SqlitePersistencia* persistencia = &p;
        PuertosEjecutor puertos{p.solicitudes(), p.logs(), p.operaciones(), p.unidadDeTrabajo(), sanitizer, sat,
                                [persistencia] { persistencia->cerrarConexionDelHiloActual(); }, &p.paquetes(), nullptr,
                                &copia};
        OperacionExecutor ejecutor(std::move(puertos), reloj.funcion(), programador);
        FuenteRespaldoLocal::Opciones o;
        o.directorioDatos = b.datos;
        o.raizPaquetes = b.raiz;
        o.rutaPreferencias = QDir(b.datos).filePath(QStringLiteral("preferencias.ini"));
        o.versionApp = QStringLiteral("0.1.0");
        o.reloj = reloj.funcion();
        FuenteRespaldoLocal fuente(ejecutor, std::make_shared<SqliteLectorCopiaRespaldo>(), std::move(o));
        auto f = fuente.capturar();
        f.waitForFinished();
        QVERIFY(f.resultCount() == 1 && f.result());
        const CapturaRespaldo c = f.result().valor();
        QCOMPARE(c.paquetes(), 3);
        for (const EntradaCaptura& e : c.entradas) {
            QVERIFY2(!e.rutaEnRespaldo.contains(QLatin1String("pre-restauracion")), qPrintable(e.rutaEnRespaldo));
            QVERIFY2(!e.rutaEnRespaldo.contains(QLatin1String("credentials")), qPrintable(e.rutaEnRespaldo));
            QVERIFY(!QDir::cleanPath(QDir(e.raiz).filePath(e.rutaRelativa)).startsWith(ap.preDatos));
        }
        QVERIFY(fuente.limpiar(c));
        auto fin = ejecutor.detener(std::chrono::milliseconds(0));
        QVERIFY(QTest::qWaitFor([&] { return fin.isFinished(); }, 10000));
    }

    // Calidad (alta): una pre-restauracion preexistente con contenido ajeno y
    // nombre valido nunca se adopta: ni se marca, ni se mueve nada a ella, ni
    // la borra despues un descarte.
    void preRestauracionPreexistenteNoSeAdopta_data()
    {
        QTest::addColumn<int>("fase");
        QTest::newRow("pre_elegida") << static_cast<int>(MarcadorRestauracion::Fase::PreElegida);
        QTest::newRow("moviendo_actuales") << static_cast<int>(MarcadorRestauracion::Fase::MoviendoActuales);
        QTest::newRow("actuales_movidos") << static_cast<int>(MarcadorRestauracion::Fase::ActualesMovidos);
    }

    void preRestauracionPreexistenteNoSeAdopta()
    {
        QFETCH(int, fase);
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        QVERIFY(hastaSolicitado(b, afbak));
        const QString ajena = QDir(b.datos).filePath(QStringLiteral("pre-restauracion-20261022T093000Z"));
        QVERIFY(escribir(QDir(ajena).filePath(QStringLiteral("satcfdi.sqlite3")), "del usuario"));
        QVERIFY(escribir(QDir(ajena).filePath(QStringLiteral("paquetes/x.zip")), "zip del usuario"));
        const QStringList antes = entradasBajo(ajena);
        MarcadorRestauracion m;
        QCOMPARE(marcador_restauracion::leer(b.datos, m), marcador_restauracion::Lectura::Leido);
        m.fase = static_cast<MarcadorRestauracion::Fase>(fase);
        m.fechaPre = QStringLiteral("20261022T093000Z");
        m.preDatos = ajena;
        m.prePaquetes = ajena; // --data-dir: comparten carpeta
        QVERIFY(marcador_restauracion::escribir(b.datos, m));
        const auto ap = RestauracionArranque(b.opcionesArranque()).aplicar();
        QCOMPARE(ap.estado, EstadoArranque::Error);
        QCOMPARE(entradasBajo(ajena), antes);
        QVERIFY(!QFileInfo::exists(QDir(ajena).filePath(QLatin1String(datos_previos_restauracion::kArchivoMarca))));
        QVERIFY(b.intacto());
        QCOMPARE(datos_previos_restauracion::pendientes(b.datos), QStringList());
        QVERIFY(!datos_previos_restauracion::bloqueanReconciliacion(b.datos));
        QCOMPARE(datos_previos_restauracion::descartar(b.datos).borradas, 0);
        QCOMPARE(entradasBajo(ajena), antes);
    }

    // Seguridad (baja): un registro aplicadas.json perdido o danado no
    // desactiva D22; descartar sigue siendo solo para las registradas.
    void registroPerdidoOCorruptoNoHabilitaLaReconciliacion_data()
    {
        QTest::addColumn<QString>("caso");
        QTest::newRow("borrado") << QStringLiteral("borrado");
        QTest::newRow("corrupto") << QStringLiteral("corrupto");
        QTest::newRow("demasiado grande") << QStringLiteral("grande");
    }

    void registroPerdidoOCorruptoNoHabilitaLaReconciliacion()
    {
        QFETCH(QString, caso);
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        QVERIFY(hastaSolicitado(b, afbak));
        const auto ap = RestauracionArranque(b.opcionesArranque()).aplicar();
        QCOMPARE(ap.estado, EstadoArranque::Aplicada);
        const QString registro = QDir(marcador_restauracion::rutaCarpeta(b.datos))
                                     .filePath(QLatin1String(datos_previos_restauracion::kArchivoRegistro));
        QVERIFY(QFileInfo::exists(registro));
        if (caso == QLatin1String("borrado")) {
            QVERIFY(QFile::remove(registro));
        } else if (caso == QLatin1String("corrupto")) {
            QVERIFY(escribir(registro, "{no es json"));
        } else {
            QVERIFY(escribir(registro, QByteArray(2 * 1024 * 1024, ' ')));
        }
        QVERIFY(datos_previos_restauracion::bloqueanReconciliacion(b.datos));
        QCOMPARE(datos_previos_restauracion::pendientes(b.datos), QStringList());

        AppBootstrapper::Opciones o;
        o.directorioDatos = b.datos;
        const auto arranque = AppBootstrapper(o).preparar();
        QVERIFY(arranque);
        fakes::FakeSecretStore secretos;
        const CredencialRef previa = secretos.sembrar();
        OpcionesMonitoreo mo;
        mo.raizPaquetes = arranque.valor().raizPaquetes;
        fakes::FakeOperacionesSat sat;
        mo.operacionesSat = &sat;
        AppCompositionRoot root(arranque.valor().rutaBase, secretos, mo);
        auto inicial = root.reconciliacionInicial();
        QVERIFY(QTest::qWaitFor([&] { return inicial.isFinished(); }, 10000));
        QCOMPARE(secretos.reconciliaciones(), 0);
        QVERIFY(secretos.existe(previa));
        auto d = root.descartarDatosPrevios();
        QVERIFY(QTest::qWaitFor([&] { return d.isFinished(); }, 30000));
        QCOMPARE(d.result().estado, ResultadoDescarteDatosPrevios::Estado::SinDatosPrevios);
        QVERIFY(QFileInfo::exists(QDir(ap.preDatos).filePath(QStringLiteral("credentials/c.bin"))));
        QCOMPARE(secretos.reconciliaciones(), 0);
        QVERIFY(secretos.existe(previa));
        auto fin = root.ejecutor().detener(std::chrono::milliseconds(0));
        QVERIFY(QTest::qWaitFor([&] { return fin.isFinished(); }, 10000));
    }

    // Seguridad (baja): al preparar de nuevo, una preparacion previa con una
    // raiz que no es la de la sesion solo se descarta en la carpeta de datos.
    void preparacionPreviaConRaizAjenaNoBorraFuera()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        const QString raizOtra = b.dir.filePath(QStringLiteral("Users/usuario/Documentos/paquetes"));
        QVERIFY(QDir().mkpath(raizOtra));
        MarcadorRestauracion m;
        m.id = uuid::generarCanonico();
        m.fase = MarcadorRestauracion::Fase::Preparado;
        m.directorioDatos = QDir::cleanPath(b.datos);
        m.raizPaquetes = QFileInfo(raizOtra).canonicalFilePath();
        m.stagingDatos = QDir(marcador_restauracion::rutaCarpeta(m.directorioDatos)).filePath(QStringLiteral("staging-") + m.id);
        m.stagingExtraccion = QDir(QFileInfo(m.raizPaquetes).absolutePath())
                                  .filePath(QStringLiteral(".restauracion-") + m.id + QStringLiteral(".noindex"));
        QVERIFY(marcador_restauracion::escribir(b.datos, m)); // crea <datos>/restauracion (0700)
        QVERIFY(escribir(QDir(m.stagingExtraccion).filePath(QStringLiteral("doc.txt")), "ajeno"));
        QVERIFY(escribir(QDir(m.stagingDatos).filePath(QStringLiteral("satcfdi.sqlite3")), "staging previo"));
        Servicio svc(cr, b);
        auto r = svc.preparar(archivo(afbak), kContrasena);
        QVERIFY2(r, r ? "" : qPrintable(r.error().diagnostico));
        QVERIFY(QFileInfo::exists(QDir(m.stagingExtraccion).filePath(QStringLiteral("doc.txt"))));
        QVERIFY(!QFileInfo::exists(m.stagingDatos));
    }

    void preRestauracionNoChocaConUnaExistente()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        const QString existente = QDir(b.datos).filePath(QStringLiteral("pre-restauracion-20261022T093000Z"));
        QVERIFY(escribir(QDir(existente).filePath(QStringLiteral("satcfdi.sqlite3")), "anterior"));
        QVERIFY(hastaSolicitado(b, afbak));
        const auto ap = RestauracionArranque(b.opcionesArranque()).aplicar();
        QCOMPARE(ap.estado, EstadoArranque::Aplicada);
        QCOMPARE(ap.preDatos, existente + QStringLiteral("-2"));
        QCOMPARE(sha(QDir(existente).filePath(QStringLiteral("satcfdi.sqlite3"))),
                 QCryptographicHash::hash("anterior", QCryptographicHash::Sha256));
        QCOMPARE(entradasBajo(existente), QStringList{QStringLiteral("satcfdi.sqlite3")});
        QCOMPARE(sha(QDir(ap.preDatos).filePath(QStringLiteral("satcfdi.sqlite3"))), b.shaBaseVieja);
    }

    // Una preparacion sin confirmar (o a medias) se descarta al arrancar.
    void preparacionSinConfirmarSeDescartaAlArrancar()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        MarcadorRestauracion m;
        {
            Servicio svc(cr, b);
            QVERIFY(svc.preparar(archivo(afbak), kContrasena));
            QCOMPARE(marcador_restauracion::leer(b.datos, m), marcador_restauracion::Lectura::Leido);
            QCOMPARE(m.fase, MarcadorRestauracion::Fase::Preparado);
        } // la app sale sin confirmar
        const auto ap = RestauracionArranque(b.opcionesArranque()).aplicar();
        QCOMPARE(ap.estado, EstadoArranque::Descartada);
        QVERIFY(b.intacto());
        QVERIFY(!QFileInfo::exists(m.stagingDatos));
        QVERIFY(!QFileInfo::exists(m.stagingExtraccion));
        QVERIFY(!QFileInfo::exists(b.marcador()));
    }

    // Matriz T019 en modo produccion (home inyectado): tras restaurar, la
    // migracion de la raiz no elige la carpeta equivocada.
    void matrizT019_data()
    {
        QTest::addColumn<QString>("caso");
        QTest::newRow("raiz nueva") << QStringLiteral("nueva");
        QTest::newRow("raiz legada") << QStringLiteral("legada");
        QTest::newRow("conflicto") << QStringLiteral("conflicto");
    }

    void matrizT019()
    {
        QFETCH(QString, caso);
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString home = QFileInfo(tmp.path()).canonicalFilePath() + QStringLiteral("/Users/usuario");
        const QString nueva = home + QStringLiteral("/Archivo-Fiscal/paquetes");
        const QString legada = home + QStringLiteral("/") + QLatin1String(FilesystemPackageStorage::kCarpetaHomeLegada) + QStringLiteral("/paquetes");
        Destino b;
        const QString raiz = caso == QLatin1String("nueva") ? nueva : legada;
        QVERIFY(b.preparar(raiz)); // ZIP viejo en la raiz de la sesion
        if (caso == QLatin1String("conflicto")) {
            QVERIFY(escribir(nueva + QStringLiteral("/BBB020202BBB/2025-11/otro/PAQ_N.zip"), "PK-nueva"));
        }
        QVERIFY(hastaSolicitado(b, afbak));
        const auto ap = aplicadorRestauracionPosix(b.datos, false, home)();
        QCOMPARE(ap.estado, EstadoArranque::Aplicada);
        QVERIFY(!QFileInfo::exists(b.zipViejo));

        PosixFileOps ops;
        const ResolucionRaizPaquetes r = MigracionRaizPaquetes(home, ops).resolver();
        if (caso == QLatin1String("nueva")) {
            QCOMPARE(r.estado, EstadoRaizPaquetes::YaNueva);
            QCOMPARE(r.raizEfectiva, nueva);
        } else if (caso == QLatin1String("legada")) {
            // pre-restauracion queda junto a la legada: se migra solo `paquetes`.
            QCOMPARE(r.estado, EstadoRaizPaquetes::MigradaPaquetes);
            QCOMPARE(r.raizEfectiva, nueva);
            QVERIFY(QFileInfo::exists(QDir(ap.prePaquetes).filePath(QStringLiteral("paquetes/BBB020202BBB/2025-12/viejo/PAQ_V.zip"))));
            QVERIFY(ap.prePaquetes.startsWith(home + QStringLiteral("/") + QLatin1String(FilesystemPackageStorage::kCarpetaHomeLegada) + QStringLiteral("/")));
            // Dos carpetas propias (datos y junto a la legada): ambas cuentan y se descartan.
            QCOMPARE(datos_previos_restauracion::pendientes(b.datos), (QStringList{ap.preDatos, ap.prePaquetes}));
            const auto descarte = datos_previos_restauracion::descartar(b.datos);
            QCOMPARE(descarte.borradas, 2);
            QVERIFY(!QFileInfo::exists(ap.prePaquetes));
        } else {
            QCOMPARE(r.estado, EstadoRaizPaquetes::UsaLegadaPorConflicto);
            QCOMPARE(r.raizEfectiva, legada);
            QVERIFY(QFileInfo::exists(nueva + QStringLiteral("/BBB020202BBB/2025-11/otro/PAQ_N.zip")));
        }
        // La raiz efectiva tiene los ZIP restaurados con su mtime.
        for (qsizetype i = 0; i < a.zips.size(); ++i) {
            const QString z = QDir(r.raizEfectiva).filePath(a.zips.at(i));
            QCOMPARE(sha(z), a.shas.at(i));
            QCOMPARE(mtimeMs(z), a.mtimes.at(i));
        }
        // Ningun staging oculto quedo junto a las raices.
        for (const QString& padre : {home + QStringLiteral("/Archivo-Fiscal"), home + QStringLiteral("/") + QLatin1String(FilesystemPackageStorage::kCarpetaHomeLegada)}) {
            for (const QString& e : QDir(padre).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
                QVERIFY2(!e.startsWith(QLatin1String(".restauracion-")), qPrintable(e));
            }
        }
    }

    // Una raiz de paquetes distinta a la de esta sesion (otro HOME al abrir)
    // no se sustituye: se descarta sin tocar nada.
    void raizDistintaAlArrancarSeDescarta()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        QVERIFY(hastaSolicitado(b, afbak));
        auto o = b.opcionesArranque();
        o.raicesPermitidas = {b.dir.filePath(QStringLiteral("otra/paquetes"))};
        const auto ap = RestauracionArranque(o).aplicar();
        QCOMPARE(ap.estado, EstadoArranque::Descartada);
        QVERIFY(b.intacto());
    }

    // ArranqueProceso: instancia unica -> restauracion -> bootstrap SQLite.
    void arranqueProcesoAplicaAntesDelBootstrap()
    {
        Origen a;
        QString afbak;
        QVERIFY(origenConTresZip(a, afbak));
        Destino b;
        QVERIFY(b.preparar());
        QVERIFY(hastaSolicitado(b, afbak));
        SingleInstanceCoordinator instancia(b.dir.filePath(QStringLiteral("canal")));
        AppBootstrapper::Opciones o;
        o.directorioDatos = b.datos;
        std::atomic<bool> restauradaAntes{false};
        o.inicializador = [&](const QString& ruta) {
            restauradaAntes = !QFileInfo::exists(b.marcador())
                && consultar(ruta, QStringLiteral("SELECT rfc FROM perfil_sat")).toString() == kRfc;
            return inicializarBaseSqlite(ruta);
        };
        const AppBootstrapper bootstrapper(o);
        const auto prep = prepararProceso(instancia, bootstrapper, [&] { return RestauracionArranque(b.opcionesArranque()).aplicar(); });
        QCOMPARE(prep.tipo, PreparacionProceso::Tipo::Lista);
        QCOMPARE(prep.restauracion.estado, EstadoArranque::Aplicada);
        QVERIFY(restauradaAntes.load());
        QCOMPARE(prep.arranque.informe.journalMode, QStringLiteral("wal"));
    }

    void marcadorInvalidoImpideArrancar()
    {
        Destino b;
        QVERIFY(b.preparar());
        QVERIFY(marcador_restauracion::asegurarCarpeta(b.datos));
        QVERIFY(escribir(b.marcador(), "{no es json"));
        SingleInstanceCoordinator instancia(b.dir.filePath(QStringLiteral("canal")));
        AppBootstrapper::Opciones o;
        o.directorioDatos = b.datos;
        const auto prep = prepararProceso(instancia, AppBootstrapper(o),
                                          [&] { return RestauracionArranque(b.opcionesArranque()).aplicar(); });
        QCOMPARE(prep.tipo, PreparacionProceso::Tipo::Error);
        QVERIFY(!prep.mensajeError.contains(b.datos));
        QVERIFY(b.intacto());
    }
};

int main(int argc, char* argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    TestRestauracion prueba;
    return QTest::qExec(&prueba, argc, argv);
}

#include "TestRestauracion.moc"
