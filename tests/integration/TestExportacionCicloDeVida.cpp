// T026.2 (criterio 5, D19) con el composition root real: SQLite real, ZIP
// sinteticos (libzip) indexados por el indexador real, plan del
// PlanificadorExportacion por el dispatcher, DestinoExportacionPosix,
// EscritorZipStreaming y RenderizadorPdfCfdi de produccion. Solo el lector de
// la exportacion es una compuerta (OpcionesMonitoreo::lectorExportacion) para
// mantener la exportacion bloqueada en la relectura por posicion:
// - restauracion: detenerParaRestaurar() no se completa mientras la
//   exportacion siga viva; al completarse el `.partial` ya no existe, no hay
//   ZIP y el resultado es Cancelada;
// - salida: la salida explicita cancela, y la app sale (callback de salida)
//   solo despues de descartar el `.partial`; no se publica ZIP;
// - eliminacion local: SolicitudesService::solicitudEliminada llega al
//   servicio (cableado del root) y las facturas pendientes se omiten;
// - C5: con el lector bloqueado indefinidamente, salida y restauracion siguen
//   al agotarse el plazo (corto e inyectado aqui; 10 s en produccion) con un
//   log de solo codigo, y el `.partial` que quedo lo borra la limpieza de un
//   nuevo arranque del root.
// Datos sinteticos (XAXX010101000, UUID inventados).

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"

#include "application/cfdi/ExportacionFacturasService.h"
#include "application/cfdi/IndexadorCfdi.h"
#include "application/requests/SolicitudesService.h"
#include "domain/common/UuidCanonico.h"
#include "domain/solicitudes/DedupKey.h"
#include "infrastructure/cfdi/zip/LectorZipLibzip.h"
#include "infrastructure/storage/DestinoExportacionPosix.h"
#include "ports/storage/RutaPaquete.h"

#include "fakes/FakeOSIntegration.h"
#include "fakes/FakeProgramador.h"
#include "fakes/FakeSatGateway.h"
#include "fakes/FakeSecretStore.h"
#include "fakes/LectorCfdiCompuerta.h"
#include "fakes/ZipCfdiSintetico.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QPointer>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <chrono>
#include <memory>
#include <optional>

using namespace satcfdi;

namespace {

constexpr int kEspera = 20000;
const QString kRfc = QStringLiteral("XAXX010101000");
const QString kFechaInicial = QStringLiteral("2026-09-01T00:00:00");
const MesCalendario kSeptiembre = *MesCalendario::crear(2026, 9);

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, kEspera) || f.isCanceled() || f.resultCount() == 0) {
        return std::nullopt;
    }
    return f.result();
}

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

// Archivos regulares (incluidos ocultos) de una carpeta; 0 si no existe.
int archivosEn(const QString& carpeta)
{
    return int(QDir(carpeta).entryList(QDir::Files | QDir::Hidden | QDir::System).size());
}

// Base con un perfil, una solicitud de emitidos de septiembre 2026 Terminada
// y un paquete Descargado cuyo ZIP (3 CFDI) existe en la raiz de paquetes.
struct Base {
    QTemporaryDir tmp;
    ArranquePreparado arranque;
    PerfilId perfil;
    SolicitudId solicitud;
    QString destino;

    bool preparar()
    {
        AppBootstrapper::Opciones o;
        o.directorioDatos = tmp.filePath(QStringLiteral("datos"));
        const auto a = AppBootstrapper(o).preparar();
        if (!a) {
            return false;
        }
        arranque = a.valor();
        destino = tmp.filePath(QStringLiteral("destino"));
        if (!QDir().mkpath(destino)) {
            return false;
        }
        const QString idPerfil = uuid::generarCanonico();
        perfil = *PerfilId::desdeTexto(idPerfil);
        solicitud = SolicitudId::generar();
        const QString paquete = uuid::generarCanonico();
        const QString ahora = QStringLiteral("2026-10-01T12:00:00.000Z");
        const auto ruta =
            rutapaquete::derivarRutaRelativa({kRfc, kFechaInicial, solicitud.texto(), QStringLiteral("PAQ_1")});
        if (!ruta) {
            return false;
        }
        const QString zip = QDir(arranque.raizPaquetes).filePath(ruta.valor());
        if (!QDir().mkpath(QFileInfo(zip).absolutePath()) || !fakes::escribirArchivo(zip, fakes::zipConCfdi(1, 3))) {
            return false;
        }
        const QString sql =
            QStringLiteral(
                "INSERT INTO perfil_sat (id, rfc, nombre, activo, creado_en, actualizado_en) VALUES "
                "(%1, %2, 'Perfil exportacion', 1, %3, %3);"
                "INSERT INTO solicitud_masiva (id, perfil_sat_id, id_solicitud_sat, tipo_cfdi, operacion_sat, "
                "rfc_solicitante, rfc_emisor, fecha_inicial_sat, fecha_final_sat, dedup_key, estado_local, "
                "cod_estatus_solicitud, estado_solicitud_sat, creada_en, envio_iniciado_en, enviada_en, "
                "ultima_verificacion_en) VALUES (%4, %1, %5, 'emitidos', 'SolicitaDescargaEmitidos', %2, %2, "
                "%6, '2026-09-30T23:59:59', %7, 'Enviada', '5000', 'Terminada', %3, %3, %3, %3);")
                .arg(citar(idPerfil), citar(kRfc), citar(ahora), citar(solicitud.texto()),
                     citar(solicitud.texto().toUpper()), citar(kFechaInicial),
                     citar(DedupKey::calcularV1("t0262").texto()))
            + QStringLiteral("INSERT INTO paquete_solicitud (id, solicitud_masiva_id, id_paquete_sat, estado_descarga, "
                             "ruta_local, disponible_en, descarga_iniciada_en, descargado_en) VALUES (%1, %2, "
                             "'PAQ_1', 'Descargado', %3, %4, %4, %4);")
                  .arg(citar(paquete), citar(solicitud.texto()), citar(ruta.valor()), citar(ahora));
        return sqlite(arranque.rutaBase, sql).isEmpty();
    }

    QString parciales() const { return DestinoExportacionPosix::carpetaParciales(arranque.directorioDatos); }
};

struct Dependencias {
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone::UTC)};
    fakes::FakeProgramador programadorEjecutor{reloj};
    fakes::FakeProgramador programadorWorker{reloj};
    fakes::FakeProgramador programadorVencimiento{reloj};
    fakes::FakeSecretStore secretos;
    fakes::FakeSatGateway gateway;
    fakes::FakeOSIntegration os;
    // Compartidos (C7): el servicio del root los retiene mientras viva su hilo.
    std::shared_ptr<LectorZipLibzip> lectorReal = std::make_shared<LectorZipLibzip>();
    std::shared_ptr<fakes::LectorCfdiCompuerta> compuerta = std::make_shared<fakes::LectorCfdiCompuerta>(lectorReal);

    Dependencias() { gateway.ahora = reloj.funcion(); }

    std::unique_ptr<AppCompositionRoot> root(const ArranquePreparado& a,
                                             std::chrono::milliseconds plazoCierre = {},
                                             std::shared_ptr<CfdiArchiveReader> lector = nullptr)
    {
        OpcionesMonitoreo m;
        m.satGateway = &gateway;
        m.reloj = reloj.funcion();
        m.programadorEjecutor = &programadorEjecutor;
        m.programadorWorker = &programadorWorker;
        m.programadorVencimiento = &programadorVencimiento;
        m.raizPaquetes = a.raizPaquetes;
        m.lectorExportacion = lector ? std::move(lector) : compuerta;
        m.plazoCierreExportacion = plazoCierre;
        return std::make_unique<AppCompositionRoot>(a.rutaBase, secretos, m);
    }
};

bool inactivo(AppCompositionRoot& root)
{
    QTest::qWait(20);
    return QTest::qWaitFor([&] { return !root.indexadorCfdi().ocupado(); }, kEspera);
}

// Plan real del mes (indice ya construido).
std::optional<PlanExportacion> planDelMes(AppCompositionRoot& root, const Base& base)
{
    const auto r = esperar(root.exportacionFacturas().planear(
        AlcanceExportacion{AlcanceMesExportacion{base.perfil, kSeptiembre, TipoDescarga::Emitidos}}));
    if (!r || !r->esExito()) {
        return std::nullopt;
    }
    return r->valor();
}

} // namespace

class TestExportacionCicloDeVida : public QObject {
    Q_OBJECT

private slots:
    void restauracionEsperaCierreYNoPublica();
    void salidaEsperaCierreYNoPublica();
    void eliminacionLocalLlegaAlServicio();
    void restauracionConPlazoAgotadoSigueYArranqueLimpia();
    void salidaConPlazoAgotadoSigueYArranqueLimpia();
    void destruirRootConLecturaBloqueadaNoUsaPuertosLiberados();
};

void TestExportacionCicloDeVida::restauracionEsperaCierreYNoPublica()
{
    Base base;
    QVERIFY(base.preparar());
    Dependencias d;
    auto root = d.root(base.arranque);
    // Si algo falla, abrir la compuerta antes de destruir el root (une hilos).
    const auto abrir = qScopeGuard([&d] { d.compuerta->liberar(); });
    root->iniciarMonitoreo(); // limpieza inicial de `.partial` e indice
    QVERIFY(inactivo(*root));
    const auto plan = planDelMes(*root, base);
    QVERIFY(plan);
    QCOMPARE(plan->conteoPrevisto(), 3);

    d.compuerta->cerrar();
    QFuture<ResultadoExportacion> exportacion =
        root->exportacionFacturas().iniciar(*plan, base.destino, QStringLiteral("facturas.zip"), true);
    QVERIFY(d.compuerta->esperarBloqueo());
    QCOMPARE(archivosEn(base.parciales()), 1); // `.partial` reservado

    QFuture<void> detenido = root->detenerParaRestaurar();
    // Mientras la exportacion siga bloqueada no se puede sustituir nada.
    QTest::qWait(300);
    QVERIFY(!detenido.isFinished());
    QCOMPARE(archivosEn(base.parciales()), 1);

    d.compuerta->liberar();
    QVERIFY(QTest::qWaitFor([&] { return detenido.isFinished(); }, kEspera));
    // Al completarse: sin `.partial`, sin ZIP y la exportacion cancelada.
    QCOMPARE(archivosEn(base.parciales()), 0);
    QCOMPARE(archivosEn(base.destino), 0);
    QVERIFY(root->exportacionFacturas().cerrado());
    const auto r = esperar(exportacion);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Cancelada);
    // Cerrado: no acepta otra exportacion.
    const auto otra = esperar(root->exportacionFacturas().iniciar(*plan, base.destino, QStringLiteral("otra.zip"), false));
    QVERIFY(otra);
    QCOMPARE(otra->estado, ResultadoExportacion::Estado::Error);
    QCOMPARE(otra->error->codigo, ErrorExportacion::Codigo::Detenido);
    root.reset();
    QCOMPARE(archivosEn(base.destino), 0);
}

void TestExportacionCicloDeVida::salidaEsperaCierreYNoPublica()
{
    Base base;
    QVERIFY(base.preparar());
    Dependencias d;
    auto root = d.root(base.arranque);
    const auto abrir = qScopeGuard([&d] { d.compuerta->liberar(); });
    int salidas = 0;
    int parcialesAlSalir = -1;
    int destinoAlSalir = -1;
    const QString parciales = base.parciales();
    const QString destino = base.destino;
    QVERIFY(root->cargar());
    root->iniciarCicloDeVida(d.os, nullptr, [&] {
        ++salidas;
        parcialesAlSalir = archivosEn(parciales);
        destinoAlSalir = archivosEn(destino);
    });
    QVERIFY(inactivo(*root));
    const auto plan = planDelMes(*root, base);
    QVERIFY(plan);
    QCOMPARE(plan->conteoPrevisto(), 3);

    d.compuerta->cerrar();
    QFuture<ResultadoExportacion> exportacion =
        root->exportacionFacturas().iniciar(*plan, base.destino, QStringLiteral("facturas.zip"), true);
    QVERIFY(d.compuerta->esperarBloqueo());
    QCOMPARE(archivosEn(parciales), 1);

    d.os.emitirSalir(); // salida explicita (menu bar)
    QTest::qWait(300);
    QCOMPARE(salidas, 0); // la salida espera a la exportacion
    QVERIFY(!d.os.salidaPreparada);

    d.compuerta->liberar();
    QTRY_COMPARE_WITH_TIMEOUT(salidas, 1, kEspera);
    // En el momento de salir el `.partial` ya no existia y no hubo ZIP.
    QCOMPARE(parcialesAlSalir, 0);
    QCOMPARE(destinoAlSalir, 0);
    const auto r = esperar(exportacion);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Cancelada);
    root.reset();
    QCOMPARE(archivosEn(base.destino), 0);
}

void TestExportacionCicloDeVida::eliminacionLocalLlegaAlServicio()
{
    Base base;
    QVERIFY(base.preparar());
    Dependencias d;
    auto root = d.root(base.arranque);
    const auto abrir = qScopeGuard([&d] { d.compuerta->liberar(); });
    root->iniciarMonitoreo();
    QVERIFY(inactivo(*root));
    const auto plan = planDelMes(*root, base);
    QVERIFY(plan);
    QCOMPARE(plan->conteoPrevisto(), 3);

    // Bloqueada en la primera factura (ya paso la revalidacion del paquete).
    d.compuerta->cerrar();
    QFuture<ResultadoExportacion> exportacion =
        root->exportacionFacturas().iniciar(*plan, base.destino, QStringLiteral("facturas.zip"), false);
    QVERIFY(d.compuerta->esperarBloqueo());
    // La senal del servicio de solicitudes (la que emite eliminar() tras su
    // commit) llega por el cableado del root.
    emit root->solicitudes().solicitudEliminada(base.solicitud);
    d.compuerta->liberar();
    const auto r = esperar(exportacion);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
    QCOMPARE(r->exportadas, 0);
    QCOMPARE(r->omitidas, 3);
    QCOMPARE(archivosEn(base.parciales()), 0);
    root.reset();
}

namespace {

constexpr std::chrono::milliseconds kPlazoCorto{300};

// Nuevo arranque simulado (otro root sobre los mismos datos, lector real):
// su limpieza inicial debe borrar el `.partial` que dejo el plazo agotado.
void arranqueLimpiaParcial(Dependencias& d, const Base& base)
{
    auto nuevo = d.root(base.arranque, {}, d.lectorReal);
    nuevo->iniciarMonitoreo();
    const auto borrados = esperar(nuevo->limpiezaExportacionInicial());
    QVERIFY(borrados);
    QCOMPARE(*borrados, 1);
    QCOMPARE(archivosEn(base.parciales()), 0);
    QCOMPARE(archivosEn(base.destino), 0);
    QVERIFY(inactivo(*nuevo));
    nuevo.reset();
}

} // namespace

void TestExportacionCicloDeVida::restauracionConPlazoAgotadoSigueYArranqueLimpia()
{
    Base base;
    QVERIFY(base.preparar());
    Dependencias d;
    auto root = d.root(base.arranque, kPlazoCorto);
    const auto abrir = qScopeGuard([&d] { d.compuerta->liberar(); });
    root->iniciarMonitoreo();
    QVERIFY(inactivo(*root));
    const auto plan = planDelMes(*root, base);
    QVERIFY(plan);

    d.compuerta->cerrar(); // no se abre hasta el final: lector colgado
    QFuture<ResultadoExportacion> exportacion =
        root->exportacionFacturas().iniciar(*plan, base.destino, QStringLiteral("facturas.zip"), true);
    QVERIFY(d.compuerta->esperarBloqueo());
    QCOMPARE(archivosEn(base.parciales()), 1);

    QTest::ignoreMessage(QtWarningMsg,
                         "cierre con plazo agotado; codigo: restauracion.exportacion_plazo_agotado");
    QElapsedTimer t;
    t.start();
    QFuture<void> detenido = root->detenerParaRestaurar();
    // La restauracion sigue tras el plazo aunque el lector siga bloqueado.
    QVERIFY(QTest::qWaitFor([&] { return detenido.isFinished(); }, kEspera));
    QVERIFY(t.elapsed() >= kPlazoCorto.count() - 50);
    QVERIFY(d.compuerta->bloqueado());
    QVERIFY(!exportacion.isFinished());
    QCOMPARE(archivosEn(base.parciales()), 1); // queda para el siguiente arranque

    arranqueLimpiaParcial(d, base);
    if (QTest::currentTestFailed()) {
        return;
    }
    d.compuerta->liberar();
    const auto r = esperar(exportacion);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Cancelada);
    root.reset();
    QCOMPARE(archivosEn(base.parciales()), 0);
    QCOMPARE(archivosEn(base.destino), 0);
}

void TestExportacionCicloDeVida::salidaConPlazoAgotadoSigueYArranqueLimpia()
{
    Base base;
    QVERIFY(base.preparar());
    Dependencias d;
    auto root = d.root(base.arranque, kPlazoCorto);
    const auto abrir = qScopeGuard([&d] { d.compuerta->liberar(); });
    int salidas = 0;
    int parcialesAlSalir = -1;
    const QString parciales = base.parciales();
    QVERIFY(root->cargar());
    root->iniciarCicloDeVida(d.os, nullptr, [&] {
        ++salidas;
        parcialesAlSalir = archivosEn(parciales);
    });
    QVERIFY(inactivo(*root));
    const auto plan = planDelMes(*root, base);
    QVERIFY(plan);

    d.compuerta->cerrar();
    QFuture<ResultadoExportacion> exportacion =
        root->exportacionFacturas().iniciar(*plan, base.destino, QStringLiteral("facturas.zip"), true);
    QVERIFY(d.compuerta->esperarBloqueo());

    QTest::ignoreMessage(QtWarningMsg, "cierre con plazo agotado; codigo: salida.exportacion_plazo_agotado");
    d.os.emitirSalir();
    // La salida termina tras el plazo con el lector aun bloqueado.
    QTRY_COMPARE_WITH_TIMEOUT(salidas, 1, kEspera);
    QVERIFY(d.os.salidaPreparada);
    QVERIFY(d.compuerta->bloqueado());
    QCOMPARE(parcialesAlSalir, 1);
    QCOMPARE(archivosEn(base.destino), 0);

    arranqueLimpiaParcial(d, base);
    if (QTest::currentTestFailed()) {
        return;
    }
    d.compuerta->liberar();
    const auto r = esperar(exportacion);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Cancelada);
    root.reset();
    QCOMPARE(archivosEn(base.parciales()), 0);
    QCOMPARE(archivosEn(base.destino), 0);
}

// C7: el root real se destruye (plazo corto) con la lectura bloqueada; el hilo
// abandonado sigue usando lector, archivos y destino que el root ya solto (los
// retiene el servicio). Al liberar la compuerta vuelve a consultar el ZIP,
// descarta su `.partial` y termina sin fallos (ASan: sin uso tras liberar).
void TestExportacionCicloDeVida::destruirRootConLecturaBloqueadaNoUsaPuertosLiberados()
{
    Base base;
    QVERIFY(base.preparar());
    Dependencias d;
    auto root = d.root(base.arranque, kPlazoCorto);
    const auto abrir = qScopeGuard([&d] { d.compuerta->liberar(); });
    root->iniciarMonitoreo();
    QVERIFY(inactivo(*root));
    const auto plan = planDelMes(*root, base);
    QVERIFY(plan);

    d.compuerta->cerrar();
    QFuture<ResultadoExportacion> exportacion =
        root->exportacionFacturas().iniciar(*plan, base.destino, QStringLiteral("facturas.zip"), true);
    QVERIFY(d.compuerta->esperarBloqueo());
    QCOMPARE(archivosEn(base.parciales()), 1);
    QPointer<QThread> hilo(const_cast<QThread*>(root->exportacionFacturas().hilo()));
    QVERIFY(hilo);

    QTest::ignoreMessage(QtWarningMsg, "hilo de exportacion abandonado; plazo de cierre agotado");
    QElapsedTimer t;
    t.start();
    root.reset();
    QVERIFY2(t.elapsed() < 5000, "la destruccion del root espero sin limite");
    const auto r = esperar(exportacion);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Cancelada);
    QVERIFY(hilo);
    QVERIFY(hilo->isRunning());
    QVERIFY(d.compuerta->bloqueado());
    QCOMPARE(archivosEn(base.parciales()), 1);

    d.compuerta->liberar();
    QTRY_VERIFY_WITH_TIMEOUT(hilo.isNull(), kEspera);
    QCOMPARE(archivosEn(base.parciales()), 0);
    QCOMPARE(archivosEn(base.destino), 0);
}

int main(int argc, char* argv[])
{
    // Ninguna prueba toca rutas reales del usuario.
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    satcfdi::configurarIdentidadAplicacion();
    TestExportacionCicloDeVida t;
    return QTest::qExec(&t, argc, argv);
}

#include "TestExportacionCicloDeVida.moc"
