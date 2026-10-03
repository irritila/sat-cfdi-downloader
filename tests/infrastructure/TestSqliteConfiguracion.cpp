#include "TestSqliteConfiguracion.h"

#include "SqlitePruebasComun.h"

#include "application/configuration/ConfiguracionAppServicePersistido.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "ports/repositories/ConfiguracionAppRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;

namespace {

struct Entorno {
    QTemporaryDir dir;
    std::unique_ptr<SqlitePersistencia> p;

    bool preparar()
    {
        if (!dir.isValid() || !inicializarBaseSqlite(rutaBase(dir))) {
            return false;
        }
        p = std::make_unique<SqlitePersistencia>(rutaBase(dir));
        return true;
    }
    ~Entorno()
    {
        if (p) {
            p->cerrarConexionDelHiloActual();
        }
    }
    ConfiguracionAppRepository& repo() { return p->configuracion(); }
    UnitOfWork& uow() { return p->unidadDeTrabajo(); }
    SqliteConnectionProvider& proveedor() { return p->proveedor(); }
};

QDateTime instante(int dia, int ms = 0)
{
    return QDateTime(QDate(2026, 10, dia), QTime(8, 30, 15, ms), QTimeZone(QTimeZone::UTC));
}

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

} // namespace

void TestSqliteConfiguracion::valoresPorDefectoDeInstalacionNueva()
{
    Entorno e;
    QVERIFY(e.preparar());
    auto c = e.repo().obtener();
    QVERIFY(c);
    // Instalacion nueva: Login Item deshabilitado por defecto (preferencia false).
    QVERIFY(!c.valor().inicioAutomaticoHabilitado);
    QVERIFY(!c.valor().monitoreoPausado);
    QVERIFY(!c.valor().ultimoCierreEn.has_value());
    QVERIFY(c.valor().actualizadaEn.isValid());
    QCOMPARE(c.valor().actualizadaEn.offsetFromUtc(), 0);
}

void TestSqliteConfiguracion::escrituraSinTransaccionEsTransaccion()
{
    Entorno e;
    QVERIFY(e.preparar());
    auto r1 = e.repo().actualizarInicioAutomatico(true, instante(3));
    auto r2 = e.repo().actualizarMonitoreoPausado(true, instante(3));
    auto r3 = e.repo().registrarUltimoCierre(instante(3));
    for (const auto* r : {&r1, &r2, &r3}) {
        QVERIFY(!*r);
        QCOMPARE(r->error().tipo, ErrorPersistencia::Tipo::Transaccion);
    }
    QVERIFY(!e.repo().obtener().valor().inicioAutomaticoHabilitado);
}

void TestSqliteConfiguracion::actualizacionesPorCampoConTimestamps()
{
    Entorno e;
    QVERIFY(e.preparar());

    QVERIFY(e.uow().begin());
    auto inicio = e.repo().actualizarInicioAutomatico(true, instante(3, 125));
    QVERIFY(inicio);
    QVERIFY(e.uow().commit());
    QVERIFY(inicio.valor().inicioAutomaticoHabilitado);
    QVERIFY(!inicio.valor().monitoreoPausado);
    QCOMPARE(inicio.valor().actualizadaEn, instante(3, 125));
    QCOMPARE(escalar(e.proveedor(), QStringLiteral("SELECT actualizada_en FROM configuracion_app")).toString(),
             QStringLiteral("2026-10-03T08:30:15.125Z"));

    QVERIFY(e.uow().begin());
    auto pausa = e.repo().actualizarMonitoreoPausado(true, instante(4));
    QVERIFY(pausa);
    QVERIFY(e.uow().commit());
    // Escritura por campo: no pisa la preferencia de inicio automatico.
    QVERIFY(pausa.valor().inicioAutomaticoHabilitado);
    QVERIFY(pausa.valor().monitoreoPausado);
    QVERIFY(!pausa.valor().ultimoCierreEn);
    QCOMPARE(pausa.valor().actualizadaEn, instante(4));

    QVERIFY(e.uow().begin());
    auto cierre = e.repo().registrarUltimoCierre(instante(5, 999));
    QVERIFY(cierre);
    QVERIFY(e.uow().commit());
    QCOMPARE(cierre.valor().ultimoCierreEn, std::optional<QDateTime>(instante(5, 999)));
    QCOMPARE(cierre.valor().actualizadaEn, instante(5, 999));
    QCOMPARE(escalar(e.proveedor(), QStringLiteral("SELECT ultimo_cierre_en FROM configuracion_app")).toString(),
             QStringLiteral("2026-10-05T08:30:15.999Z"));

    QVERIFY(e.uow().begin());
    QVERIFY(e.repo().actualizarInicioAutomatico(false, instante(6)));
    QVERIFY(e.repo().actualizarMonitoreoPausado(false, instante(6)));
    QVERIFY(e.uow().commit());

    // Lectura independiente (persistido, no solo devuelto).
    auto leida = e.repo().obtener();
    QVERIFY(leida);
    QVERIFY(!leida.valor().inicioAutomaticoHabilitado);
    QVERIFY(!leida.valor().monitoreoPausado);
    QCOMPARE(leida.valor().ultimoCierreEn, std::optional<QDateTime>(instante(5, 999)));
    QCOMPARE(leida.valor().actualizadaEn, instante(6));
    QCOMPARE(escalar(e.proveedor(), QStringLiteral("SELECT COUNT(*) FROM configuracion_app")).toInt(), 1);
}

void TestSqliteConfiguracion::rollbackDescartaCambios()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QDateTime original = e.repo().obtener().valor().actualizadaEn;

    QVERIFY(e.uow().begin());
    auto dentro = e.repo().actualizarMonitoreoPausado(true, instante(3));
    QVERIFY(dentro);
    QVERIFY(dentro.valor().monitoreoPausado); // visible dentro de la transaccion
    QVERIFY(e.uow().rollback());

    auto despues = e.repo().obtener();
    QVERIFY(despues);
    QVERIFY(!despues.valor().monitoreoPausado);
    QCOMPARE(despues.valor().actualizadaEn, original);
}

void TestSqliteConfiguracion::instanteInvalidoEsInterno()
{
    Entorno e;
    QVERIFY(e.preparar());
    QVERIFY(e.uow().begin());
    auto r = e.repo().registrarUltimoCierre(QDateTime());
    QVERIFY(e.uow().rollback());
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::Interno);
}

void TestSqliteConfiguracion::sinFilaEsNoEncontrado()
{
    Entorno e;
    QVERIFY(e.preparar());
    QCOMPARE(ejecutarSql(e.proveedor(), QStringLiteral("DELETE FROM configuracion_app")), QString());
    QVERIFY(e.uow().begin());
    auto r = e.repo().actualizarInicioAutomatico(true, instante(3));
    QVERIFY(e.uow().rollback());
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::NoEncontrado);
}

void TestSqliteConfiguracion::servicioPersistidoSobreSqlite()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));
    SqlitePersistencia persistencia(rutaBase(dir));
    {
        PersistenceDispatcher dispatcher;
        {
            ConfiguracionAppServicePersistido servicio(dispatcher, persistencia.configuracion(),
                                                       persistencia.unidadDeTrabajo(),
                                                       [] { return instante(7, 500); });
            QSignalSpy cambio(&servicio, &ConfiguracionAppService::configuracionCambiada);

            const auto inicial = esperar(servicio.obtener());
            QVERIFY(inicial && inicial->esExito());
            QVERIFY(!inicial->valor().inicioAutomaticoHabilitado);
            QCOMPARE(cambio.count(), 0);

            const auto pausa = esperar(servicio.actualizarMonitoreoPausado(true));
            QVERIFY(pausa && pausa->esExito());
            QVERIFY(pausa->valor().monitoreoPausado);
            QCOMPARE(pausa->valor().actualizadaEn, instante(7, 500));
            QCOMPARE(cambio.count(), 1);

            const auto cierre = esperar(servicio.registrarUltimoCierre());
            QVERIFY(cierre && cierre->esExito());
            QCOMPARE(cierre->valor().ultimoCierreEn, std::optional<QDateTime>(instante(7, 500)));
            QCOMPARE(cambio.count(), 2);

            const auto releida = esperar(servicio.obtener());
            QVERIFY(releida && releida->esExito());
            QVERIFY(releida->valor().monitoreoPausado);
            QVERIFY(!releida->valor().inicioAutomaticoHabilitado);
        }
        // Orden de cierre de T003: conexion del hilo, luego dispatcher.
        SqlitePersistencia* p = &persistencia;
        dispatcher.despachar<void>([p] { p->cerrarConexionDelHiloActual(); }).waitForFinished();
        dispatcher.cerrar();
    }
    QCOMPARE(persistencia.conexionesAbiertas(), 0);
}
