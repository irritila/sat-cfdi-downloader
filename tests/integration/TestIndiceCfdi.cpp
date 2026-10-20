// T015.2 (integracion): indice de CFDI con el composition root real, SQLite
// real, LectorZipLibzip real y ZIP sinteticos generados en la prueba (libzip):
// - indexar al descargar (flujo SAT real con FakeSatGateway);
// - base T014 (005) con paquetes Descargado: migracion 006 al arrancar,
//   pasada inicial una sola vez y nada al reiniciar;
// - ZIP borrado, reemplazado y corrupto (revisarSolicitud y al arrancar) sin
//   tocar paquete_solicitud;
// - salida durante una indexacion: el paquete queda Pendiente y se reindexa;
// - eliminar la solicitud elimina su indice;
// - una operacion SAT se ejecuta mientras una indexacion grande sigue en curso;
// - logs del indice sin datos fiscales ni rutas (D11).

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"

#include "application/cfdi/CatalogoCfdiService.h"
#include "application/cfdi/IndexadorCfdi.h"
#include "application/operaciones/WorkerLocal.h"
#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/PerfilesSatService.h"
#include "application/requests/SolicitudesService.h"
#include "domain/solicitudes/DedupKey.h"
#include "infrastructure/cfdi/zip/LectorZipLibzip.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"
#include "ports/storage/RutaPaquete.h"

#include "fakes/FakeOSIntegration.h"
#include "fakes/FakeProgramador.h"
#include "fakes/FakeSatGateway.h"
#include "fakes/FakeSecretStore.h"
#include "fakes/LectorCfdiCompuerta.h"
#include "fakes/ZipCfdiSintetico.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>
#include <optional>

using namespace satcfdi;

namespace {

constexpr int kEspera = 20000;
const QString kRfc = QStringLiteral("BBB020202BBB"); // distinto del de FakeSecretStore
const QString kFechaInicial = QStringLiteral("2026-09-01T00:00:00");

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, kEspera) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

bool esperarVoid(QFuture<void> f)
{
    return QTest::qWaitFor([&] { return f.isFinished(); }, kEspera) && !f.isCanceled();
}

// SQL por la CLI del sistema (las pruebas de integracion no enlazan Qt6::Sql).
QString sqlite(const QString& rutaBase, const QString& sql)
{
    QProcess p;
    p.start(QStringLiteral("/usr/bin/sqlite3"), {rutaBase, sql});
    if (!p.waitForFinished(10000) || p.exitCode() != 0) {
        return QStringLiteral("<error>") + QString::fromUtf8(p.readAllStandardError());
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString citar(const QString& v)
{
    return QLatin1Char('\'') + QString(v).replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\'');
}

std::optional<ResumenIndiceSolicitud> resumen(AppCompositionRoot& root, const SolicitudId& id)
{
    const auto r = esperar(root.catalogoCfdi().resumen(id));
    return r && r->esExito() ? std::optional(r->valor()) : std::nullopt;
}

QList<CfdiListado> listar(AppCompositionRoot& root, const SolicitudId& id, const FiltroListaCfdi& filtro = {})
{
    const auto r = esperar(root.catalogoCfdi().listar(id, filtro));
    return r && r->esExito() ? r->valor() : QList<CfdiListado>{};
}

bool inactivo(AppCompositionRoot& root)
{
    QTest::qWait(20); // entrega de senales encoladas del ejecutor
    return QTest::qWaitFor([&] { return !root.indexadorCfdi().ocupado(); }, kEspera);
}

// Dependencias del root comunes (reloj y programadores falsos, gateway falso).
struct Dependencias {
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone::UTC)};
    fakes::FakeProgramador programadorEjecutor{reloj};
    fakes::FakeProgramador programadorWorker{reloj};
    fakes::FakeProgramador programadorVencimiento{reloj};
    fakes::FakeSecretStore secretos;
    fakes::FakeSatGateway gateway;
    fakes::FakeOSIntegration os;
    LectorZipLibzip lectorReal;

    Dependencias() { gateway.ahora = reloj.funcion(); }

    std::unique_ptr<AppCompositionRoot> root(const ArranquePreparado& a, CfdiArchiveReader* lector)
    {
        OpcionesMonitoreo m;
        m.satGateway = &gateway;
        m.reloj = reloj.funcion();
        m.programadorEjecutor = &programadorEjecutor;
        m.programadorWorker = &programadorWorker;
        m.programadorVencimiento = &programadorVencimiento;
        m.raizPaquetes = a.raizPaquetes;
        m.lectorCfdi = lector;
        return std::make_unique<AppCompositionRoot>(a.rutaBase, secretos, m);
    }
};

// Base con el esquema de T014 (migraciones 001-005) y una solicitud Terminada
// con paquetes Descargado cuyos ZIP existen en la raiz de paquetes.
struct BaseT014 {
    QTemporaryDir tmp;
    ArranquePreparado arranque;
    SolicitudId solicitud;
    QStringList paquetes;  // ids locales
    QStringList zips;      // rutas absolutas
    QList<int> cfdiPorPaquete;

    bool preparar(const QList<int>& cfdi)
    {
        cfdiPorPaquete = cfdi;
        auto embebidas = migracionesSqliteEmbebidas();
        if (!tmp.isValid() || !embebidas || embebidas.valor().size() < 6) {
            return false;
        }
        AppBootstrapper::Opciones o;
        o.directorioDatos = tmp.path();
        o.migraciones = embebidas.valor().mid(0, 5);
        const auto a = AppBootstrapper(o).preparar();
        if (!a || a.valor().informe.migracion.versionFinal != 5) {
            return false;
        }
        arranque = a.valor();
        const QString perfil = uuid::generarCanonico();
        solicitud = SolicitudId::generar();
        const QString ahora = QStringLiteral("2026-10-01T12:00:00.000Z");
        QString sql = QStringLiteral(
                          "INSERT INTO perfil_sat (id, rfc, nombre, activo, creado_en, actualizado_en) VALUES "
                          "(%1, %2, 'Perfil T014', 1, %3, %3);"
                          "INSERT INTO solicitud_masiva (id, perfil_sat_id, id_solicitud_sat, tipo_cfdi, operacion_sat, "
                          "rfc_solicitante, rfc_emisor, fecha_inicial_sat, fecha_final_sat, dedup_key, estado_local, "
                          "cod_estatus_solicitud, estado_solicitud_sat, creada_en, envio_iniciado_en, enviada_en, "
                          "ultima_verificacion_en) VALUES (%4, %1, %5, 'emitidos', 'SolicitaDescargaEmitidos', %2, %2, "
                          "%6, '2026-09-30T23:59:59', %7, 'Enviada', '5000', 'Terminada', %3, %3, %3, %3);")
                          .arg(citar(perfil), citar(kRfc), citar(ahora), citar(solicitud.texto()),
                               citar(solicitud.texto().toUpper()), citar(kFechaInicial),
                               citar(DedupKey::calcularV1("t014").texto()));
        int primero = 1;
        for (qsizetype i = 0; i < cfdi.size(); ++i) {
            const QString id = uuid::generarCanonico();
            const QString idSat = QStringLiteral("PAQ_%1").arg(i + 1);
            const auto ruta = rutapaquete::derivarRutaRelativa({kRfc, kFechaInicial, solicitud.texto(), idSat});
            if (!ruta) {
                return false;
            }
            sql += QStringLiteral("INSERT INTO paquete_solicitud (id, solicitud_masiva_id, id_paquete_sat, "
                                  "estado_descarga, ruta_local, disponible_en, descarga_iniciada_en, descargado_en) "
                                  "VALUES (%1, %2, %3, 'Descargado', %4, %5, %5, %5);")
                       .arg(citar(id), citar(solicitud.texto()), citar(idSat), citar(ruta.valor()),
                            citar(QStringLiteral("2026-10-01T12:00:0%1.000Z").arg(i)));
            const QString absoluta = QDir(arranque.raizPaquetes).filePath(ruta.valor());
            if (!QDir().mkpath(QFileInfo(absoluta).absolutePath())
                || !fakes::escribirArchivo(absoluta, fakes::zipConCfdi(primero, cfdi.at(i)))) {
                return false;
            }
            primero += cfdi.at(i);
            paquetes.append(id);
            zips.append(absoluta);
        }
        return sqlite(arranque.rutaBase, sql).isEmpty();
    }

    // Arranque de la version nueva (aplica 006 si falta).
    std::optional<ArranquePreparado> arrancar()
    {
        AppBootstrapper::Opciones o;
        o.directorioDatos = tmp.path();
        const auto a = AppBootstrapper(o).preparar();
        return a ? std::optional(a.valor()) : std::nullopt;
    }

    QString filasPaquetes() const
    {
        return sqlite(arranque.rutaBase, QStringLiteral("SELECT * FROM paquete_solicitud ORDER BY id"));
    }
};

QStringList g_logs;
QMutex g_mutexLogs;
void capturarLog(QtMsgType, const QMessageLogContext& contexto, const QString& mensaje)
{
    QMutexLocker b(&g_mutexLogs);
    // Categoria y mensaje separados por '|': las comprobaciones miran el mensaje.
    g_logs.append(QString::fromLatin1(contexto.category ? contexto.category : "") + QLatin1Char('|') + mensaje);
}

} // namespace

class TestIndiceCfdiIntegracion : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(
            "^(?!Populating font family aliases took|This plugin does not support ).*")));
    }

    void indexaAlDescargarConRootReal();
    void baseT014IndexaUnaVezYNoRepite();
    void zipBorradoReemplazadoCorrupto();
    void salidaDuranteIndexacionQuedaPendiente();
    void eliminarSolicitudEliminaSuIndice();
    void operacionSatNoEsperaIndexacionGrande();
};

void TestIndiceCfdiIntegracion::indexaAlDescargarConRootReal()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    AppBootstrapper::Opciones o;
    o.directorioDatos = tmp.path();
    const auto a = AppBootstrapper(o).preparar();
    QVERIFY(a);
    Dependencias d;
    // Los dos paquetes de verificacionTerminada(): ZIP reales de 3 y 2 CFDI.
    using Op = fakes::FakeSatGateway::Op;
    using Paso = fakes::FakeSatGateway::Paso;
    d.gateway.programar(Op::Descargar,
                        Paso::conDescarga(fakes::FakeSatGateway::descargaAceptada(), fakes::zipConCfdi(1, 3)));
    d.gateway.programar(Op::Descargar,
                        Paso::conDescarga(fakes::FakeSatGateway::descargaAceptada(), fakes::zipConCfdi(4, 2)));
    fakes::LectorCfdiCompuerta lector(d.lectorReal);
    auto root = d.root(a.valor(), &lector);
    QSignalSpy cambios(&root->catalogoCfdi(), &CatalogoCfdiService::indiceCambiado);
    root->iniciarMonitoreo();

    // Perfil con e.firma Lista y solicitud de recibidos.
    const auto perfil = esperar(root->perfiles().crear(d.secretos.rfcCertificado, QStringLiteral("Perfil")));
    QVERIFY(perfil && perfil->esExito());
    EntradaEFirma e;
    e.rutaCertificado = QStringLiteral("/ficticio/efirma.cer");
    e.rutaLlavePrivada = QStringLiteral("/ficticio/efirma.key");
    e.contrasena = BufferSecreto::desdeBytes(d.secretos.contrasenaValida.constData(),
                                             static_cast<std::size_t>(d.secretos.contrasenaValida.size()));
    const auto importada = esperar(root->credenciales().importar(perfil->valor().id, std::move(e)));
    QVERIFY(importada && importada->esExito());
    NuevaSolicitudRequest r;
    r.perfilId = perfil->valor().id;
    r.tipoDescarga = TipoDescarga::Recibidos;
    r.fechaInicial = QDate(2026, 9, 1);
    r.fechaFinal = QDate(2026, 9, 1);
    const auto creada = esperar(root->solicitudes().crear(r));
    QVERIFY(creada && creada->esExito());
    const SolicitudId id = creada->valor();

    // Flujo SAT real hasta Descargado.
    root->worker().enviar(id);
    QTRY_VERIFY_WITH_TIMEOUT(esperar(root->solicitudes().obtener(id))->valor().resumen.estadoLocal
                                 == EstadoLocal::Enviada,
                             kEspera);
    d.reloj.fijar(d.reloj.ahora().addSecs(11 * 60));
    root->worker().ejecutarCiclo();
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
            root->worker().ejecutarCiclo();
            const auto det = esperar(root->solicitudes().obtener(id));
            if (!det || !det->esExito() || det->valor().paquetes.size() != 2) {
                return false;
            }
            for (const PaqueteResumen& p : det->valor().paquetes) {
                if (p.estadoDescarga != EstadoDescarga::Descargado) {
                    return false;
                }
            }
            return true;
        }(),
        kEspera);

    // Indexado por el disparo (b) tras la descarga, en el hilo del indexador.
    QTRY_VERIFY_WITH_TIMEOUT(resumen(*root, id) && resumen(*root, id)->paquetesIndexados == 2, kEspera);
    QVERIFY(inactivo(*root));
    const auto res = resumen(*root, id);
    QCOMPARE(res->paquetesDescargados, 2);
    QCOMPARE(res->paquetesPendientes, 0);
    QCOMPARE(res->cfdiTotal, 5);
    QCOMPARE(res->cfdiIlegibles, 0);
    QVERIFY(!res->indexando);
    QVERIFY(cambios.count() >= 1);
    QCOMPARE(cambios.last().at(0).value<SolicitudId>(), id);
    QCOMPARE(lector.hilo(), root->indexadorCfdi().hilo());

    // Lista por fecha (descendente) con sus resumenes y busqueda normalizada.
    const QList<CfdiListado> lista = listar(*root, id);
    QCOMPARE(lista.size(), 5);
    for (qsizetype i = 1; i < lista.size(); ++i) {
        QVERIFY(lista.at(i - 1).resumen.fecha >= lista.at(i).resumen.fecha);
    }
    QCOMPARE(lista.first().resumen.uuid, fakes::uuidSintetico(5));
    QCOMPARE(lista.first().resumen.total->texto(), QStringLiteral("116.00"));
    QCOMPARE(lista.first().resumen.estado, EstadoParseo::Completo);
    FiltroListaCfdi filtro;
    filtro.texto = fakes::uuidSintetico(3).toLower();
    QCOMPARE(listar(*root, id, filtro).size(), 1);
    filtro.texto = QStringLiteral("Emisor Sintético");
    QCOMPARE(listar(*root, id, filtro).size(), 5);
    filtro.texto.clear();
    filtro.tipo = TipoComprobante::Egreso;
    QCOMPARE(listar(*root, id, filtro).size(), 0);
}

void TestIndiceCfdiIntegracion::baseT014IndexaUnaVezYNoRepite()
{
    BaseT014 base;
    QVERIFY(base.preparar({3, 2}));
    const QString paquetesAntes = base.filasPaquetes();
    const auto a = base.arrancar();
    QVERIFY(a);
    QCOMPARE(a->informe.migracion.versionInicial, 5);
    QCOMPARE(a->informe.migracion.aplicadas, (QList<int>{6, 7})); // 007: T023.2

    Dependencias d;
    QLoggingCategory::setFilterRules(QStringLiteral("satcfdi.cfdi.*=true"));
    {
        QMutexLocker b(&g_mutexLogs);
        g_logs.clear();
    }
    const auto anterior = qInstallMessageHandler(capturarLog);
    QString indiceTrasPrimerArranque;
    {
        fakes::LectorCfdiCompuerta lector(d.lectorReal);
        auto root = d.root(*a, &lector);
        root->iniciarMonitoreo(); // (a): pasada inicial
        QVERIFY(inactivo(*root));
        QTRY_VERIFY_WITH_TIMEOUT(resumen(*root, base.solicitud)->paquetesIndexados == 2, kEspera);
        QCOMPARE(lector.recorridos(), 2);
        QCOMPARE(resumen(*root, base.solicitud)->cfdiTotal, 5);
        root.reset();
        indiceTrasPrimerArranque = sqlite(a->rutaBase, QStringLiteral("SELECT * FROM indice_cfdi_paquete ORDER BY 1"));
    }
    qInstallMessageHandler(anterior);
    QLoggingCategory::setFilterRules(QString());
    {
        // D11: solo conteos, causa e Id enmascarado.
        QMutexLocker b(&g_mutexLogs);
        bool vio = false;
        for (const QString& entrada : std::as_const(g_logs)) {
            if (!entrada.startsWith(QStringLiteral("satcfdi.cfdi"))) {
                continue;
            }
            vio = true;
            const QString linea = entrada.section(QLatin1Char('|'), 1);
            QVERIFY2(!linea.contains(kRfc) && !linea.contains(QStringLiteral("XAXX"))
                         && !linea.contains(QStringLiteral("XEXX")),
                     qPrintable(linea));
            QVERIFY2(!linea.contains(QStringLiteral("SINTETICO"), Qt::CaseInsensitive), qPrintable(linea));
            QVERIFY2(!linea.contains(QStringLiteral("A0000000-")), qPrintable(linea));
            QVERIFY2(!linea.contains(base.tmp.path()) && !linea.contains(QStringLiteral(".zip")), qPrintable(linea));
            for (const QString& id : std::as_const(base.paquetes)) {
                QVERIFY2(!linea.contains(id), qPrintable(linea));
            }
        }
        QVERIFY(vio);
    }
    QVERIFY(!indiceTrasPrimerArranque.isEmpty());

    // Reinicio: la pasada no relee ni reescribe nada.
    {
        fakes::LectorCfdiCompuerta lector(d.lectorReal);
        auto root = d.root(*a, &lector);
        root->iniciarMonitoreo();
        QVERIFY(inactivo(*root));
        QCOMPARE(lector.recorridos(), 0);
        QCOMPARE(resumen(*root, base.solicitud)->cfdiTotal, 5);
        root.reset();
    }
    QCOMPARE(sqlite(a->rutaBase, QStringLiteral("SELECT * FROM indice_cfdi_paquete ORDER BY 1")),
             indiceTrasPrimerArranque);
    QCOMPARE(base.filasPaquetes(), paquetesAntes); // paquete_solicitud no cambia
}

void TestIndiceCfdiIntegracion::zipBorradoReemplazadoCorrupto()
{
    BaseT014 base;
    QVERIFY(base.preparar({3, 2}));
    const QString paquetesAntes = base.filasPaquetes();
    const auto a = base.arrancar();
    QVERIFY(a);
    Dependencias d;
    {
        auto root = d.root(*a, &d.lectorReal);
        root->iniciarMonitoreo();
        QVERIFY(inactivo(*root));
        QCOMPARE(resumen(*root, base.solicitud)->cfdiTotal, 5);
        QSignalSpy cambios(&root->catalogoCfdi(), &CatalogoCfdiService::indiceCambiado);

        // Borrado -> OrigenNoDisponible y sus CFDI salen del indice.
        QVERIFY(QFile::remove(base.zips.at(0)));
        QVERIFY(esperarVoid(root->catalogoCfdi().revisarSolicitud(base.solicitud)));
        auto r = resumen(*root, base.solicitud);
        QCOMPARE(r->paquetesNoDisponibles, 1);
        QCOMPARE(r->paquetesIndexados, 1);
        QCOMPARE(r->cfdiTotal, 2);
        QCOMPARE(cambios.count(), 1);
        QCOMPARE(sqlite(a->rutaBase, QStringLiteral("SELECT causa FROM indice_cfdi_paquete WHERE estado = "
                                                    "'OrigenNoDisponible'")),
                 QStringLiteral("no-existe"));

        // Revisar sin cambios: ni escrituras ni senal.
        QVERIFY(esperarVoid(root->catalogoCfdi().revisarSolicitud(base.solicitud)));
        QCOMPARE(cambios.count(), 1);

        // Reemplazado por otro ZIP valido (4 CFDI) -> reindexa.
        QVERIFY(fakes::escribirArchivo(base.zips.at(1), fakes::zipConCfdi(100, 4)));
        QVERIFY(esperarVoid(root->catalogoCfdi().revisarSolicitud(base.solicitud)));
        r = resumen(*root, base.solicitud);
        QCOMPARE(r->paquetesIndexados, 1);
        QCOMPARE(r->cfdiTotal, 4);
        QCOMPARE(listar(*root, base.solicitud).first().resumen.uuid, fakes::uuidSintetico(103));
        QCOMPARE(cambios.count(), 2);

        // Corrupto -> Ilegible sin CFDI.
        QVERIFY(fakes::escribirArchivo(base.zips.at(1), QByteArray(4096, 'x')));
        QVERIFY(esperarVoid(root->catalogoCfdi().revisarSolicitud(base.solicitud)));
        r = resumen(*root, base.solicitud);
        QCOMPARE(r->paquetesIlegibles, 1);
        QCOMPARE(r->paquetesIndexados, 0);
        QCOMPARE(r->cfdiTotal, 0);
        QVERIFY(listar(*root, base.solicitud).isEmpty());
        root.reset();
    }

    // Se restauran los ZIP: al arrancar se revalida y se reindexa (d).
    QVERIFY(fakes::escribirArchivo(base.zips.at(0), fakes::zipConCfdi(1, 3)));
    QVERIFY(fakes::escribirArchivo(base.zips.at(1), fakes::zipConCfdi(4, 2)));
    {
        auto root = d.root(*a, &d.lectorReal);
        root->iniciarMonitoreo();
        QVERIFY(inactivo(*root));
        const auto r = resumen(*root, base.solicitud);
        QCOMPARE(r->paquetesIndexados, 2);
        QCOMPARE(r->paquetesNoDisponibles + r->paquetesIlegibles, 0);
        QCOMPARE(r->cfdiTotal, 5);
        root.reset();
    }
    QCOMPARE(base.filasPaquetes(), paquetesAntes);
}

void TestIndiceCfdiIntegracion::salidaDuranteIndexacionQuedaPendiente()
{
    BaseT014 base;
    QVERIFY(base.preparar({200}));
    const auto a = base.arrancar();
    QVERIFY(a);
    Dependencias d;
    {
        fakes::LectorCfdiCompuerta lector(d.lectorReal);
        lector.cerrar();
        auto root = d.root(*a, &lector);
        // Si una comprobacion falla, abrir la compuerta antes de destruir el
        // root (su destructor une el hilo del indexador).
        const auto abrir = qScopeGuard([&lector] { lector.liberar(); });
        int salidas = 0;
        QVERIFY(root->cargar());
        root->iniciarCicloDeVida(d.os, nullptr, [&salidas] { ++salidas; }); // (a) pasada inicial
        QVERIFY(lector.esperarBloqueo());
        QVERIFY(root->indexadorCfdi().ocupado());
        // Salida explicita (menu bar): detiene el indexador a mitad del paquete.
        d.os.emitirSalir();
        QTRY_VERIFY(root->indexadorCfdi().detenido());
        lector.liberar();
        QTRY_COMPARE_WITH_TIMEOUT(salidas, 1, kEspera);
        QTRY_VERIFY_WITH_TIMEOUT(!root->indexadorCfdi().ocupado(), kEspera);
        root.reset();
    }
    // Nada del paquete en curso se escribio: queda Pendiente (sin fila).
    QCOMPARE(sqlite(a->rutaBase, QStringLiteral("SELECT count(*) FROM indice_cfdi_paquete")), QStringLiteral("0"));
    QCOMPARE(sqlite(a->rutaBase, QStringLiteral("SELECT count(*) FROM cfdi_indexado")), QStringLiteral("0"));
    {
        auto root = d.root(*a, &d.lectorReal);
        auto r = resumen(*root, base.solicitud);
        QCOMPARE(r->paquetesPendientes, 1);
        QCOMPARE(r->cfdiTotal, 0);
        root->iniciarMonitoreo(); // al arrancar se reindexa
        QVERIFY(inactivo(*root));
        r = resumen(*root, base.solicitud);
        QCOMPARE(r->paquetesIndexados, 1);
        QCOMPARE(r->cfdiTotal, 200);
        root.reset();
    }
}

void TestIndiceCfdiIntegracion::eliminarSolicitudEliminaSuIndice()
{
    BaseT014 base;
    QVERIFY(base.preparar({3, 2}));
    const auto a = base.arrancar();
    QVERIFY(a);
    Dependencias d;
    auto root = d.root(*a, &d.lectorReal);
    root->iniciarMonitoreo();
    QVERIFY(inactivo(*root));
    QCOMPARE(resumen(*root, base.solicitud)->cfdiTotal, 5);
    QSignalSpy cambios(&root->catalogoCfdi(), &CatalogoCfdiService::indiceCambiado);

    const auto eliminada = esperar(root->solicitudes().eliminar(base.solicitud));
    QVERIFY(eliminada && eliminada->esExito() && eliminada->valor().cambio);
    QTRY_COMPARE(cambios.count(), 1);
    QCOMPARE(cambios.at(0).at(0).value<SolicitudId>(), base.solicitud);
    QVERIFY(inactivo(*root));
    QVERIFY(listar(*root, base.solicitud).isEmpty());
    QCOMPARE(resumen(*root, base.solicitud).value(), ResumenIndiceSolicitud{});
    root.reset();
    QCOMPARE(sqlite(a->rutaBase, QStringLiteral("SELECT count(*) FROM indice_cfdi_paquete")), QStringLiteral("0"));
    QCOMPARE(sqlite(a->rutaBase, QStringLiteral("SELECT count(*) FROM cfdi_indexado")), QStringLiteral("0"));
    // Los ZIP no se tocan (T007 D5).
    for (const QString& zip : std::as_const(base.zips)) {
        QVERIFY(QFileInfo::exists(zip));
    }
}

void TestIndiceCfdiIntegracion::operacionSatNoEsperaIndexacionGrande()
{
    BaseT014 base;
    QVERIFY(base.preparar({500, 500}));
    const auto a = base.arrancar();
    QVERIFY(a);
    Dependencias d;
    fakes::LectorCfdiCompuerta lector(d.lectorReal);
    lector.cerrar();
    auto root = d.root(*a, &lector);
    const auto abrir = qScopeGuard([&lector] { lector.liberar(); }); // antes de ~root
    root->iniciarMonitoreo(); // la pasada inicial queda en curso (bloqueada)
    QVERIFY(lector.esperarBloqueo());

    // Mientras tanto, una operacion SAT (FakeSatGateway) por el ejecutor.
    const auto perfil = esperar(root->perfiles().crear(d.secretos.rfcCertificado, QStringLiteral("Perfil")));
    QVERIFY(perfil && perfil->esExito());
    EntradaEFirma e;
    e.rutaCertificado = QStringLiteral("/ficticio/efirma.cer");
    e.rutaLlavePrivada = QStringLiteral("/ficticio/efirma.key");
    e.contrasena = BufferSecreto::desdeBytes(d.secretos.contrasenaValida.constData(),
                                             static_cast<std::size_t>(d.secretos.contrasenaValida.size()));
    const auto importada = esperar(root->credenciales().importar(perfil->valor().id, std::move(e)));
    QVERIFY(importada && importada->esExito());
    NuevaSolicitudRequest r;
    r.perfilId = perfil->valor().id;
    r.tipoDescarga = TipoDescarga::Recibidos;
    r.fechaInicial = QDate(2026, 9, 2);
    r.fechaFinal = QDate(2026, 9, 2);
    const auto creada = esperar(root->solicitudes().crear(r));
    QVERIFY(creada && creada->esExito());
    root->worker().enviar(creada->valor());
    QTRY_VERIFY_WITH_TIMEOUT(esperar(root->solicitudes().obtener(creada->valor()))->valor().resumen.estadoLocal
                                 == EstadoLocal::Enviada,
                             kEspera);
    QCOMPARE(d.gateway.llamadas(fakes::FakeSatGateway::Op::Crear), 1);
    // La indexacion sigue en curso: la operacion no la espero.
    QVERIFY(lector.bloqueado());
    QVERIFY(root->indexadorCfdi().ocupado());
    const auto pendiente = resumen(*root, base.solicitud);
    QCOMPARE(pendiente->paquetesPendientes, 2);
    QVERIFY(pendiente->indexando);

    lector.liberar();
    QVERIFY(inactivo(*root));
    const auto res = resumen(*root, base.solicitud);
    QCOMPARE(res->paquetesIndexados, 2);
    QCOMPARE(res->cfdiTotal, 1000);
    QVERIFY(!res->indexando);
    root.reset();
}

#include "TestIndiceCfdi.moc"

int ejecutarTestIndiceCfdi(int argc, char* argv[])
{
    TestIndiceCfdiIntegracion prueba;
    return QTest::qExec(&prueba, argc, argv);
}
