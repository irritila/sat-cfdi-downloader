#include "TestSqliteConexion.h"

#include "SqlitePruebasComun.h"

#include "domain/perfiles/PerfilSat.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteUnitOfWork.h"
#include "ports/repositories/ConfiguracionAppRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QRegularExpression>
#include <QSqlDatabase>
#include <QTest>
#include <QThread>

#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;

namespace {

NuevoPerfilSat nuevoPerfil(const QString& rfc)
{
    NuevoPerfilSat p;
    p.id = PerfilId::generar();
    p.rfc = rfc;
    p.nombre = QStringLiteral("Perfil de prueba");
    p.creadoEn = timestamp::ahoraUtc();
    p.actualizadoEn = p.creadoEn;
    return p;
}

} // namespace

void TestSqliteConexion::init()
{
    // Ninguna prueba de esta clase debe emitir warnings (removeDatabase con
    // conexiones en uso, queries vivas, conexiones en otros hilos).
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
}

void TestSqliteConexion::pragmasVerificados()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));
    OpcionesConexionSqlite opciones;
    opciones.busyTimeoutMs = 1234;
    SqliteConnectionProvider proveedor(rutaBase(dir), opciones);
    QVERIFY(proveedor.conexion());
    QCOMPARE(escalar(proveedor, QStringLiteral("PRAGMA foreign_keys")).toInt(), 1);
    QCOMPARE(escalar(proveedor, QStringLiteral("PRAGMA busy_timeout")).toInt(), 1234);
    QCOMPARE(escalar(proveedor, QStringLiteral("PRAGMA journal_mode")).toString(),
             QStringLiteral("wal"));
    proveedor.cerrarConexionDelHiloActual();
    QCOMPARE(proveedor.conexionesAbiertas(), 0);
}

void TestSqliteConexion::exigeWalEnBaseNoInicializada()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteConnectionProvider proveedor(rutaBase(dir)); // exigirWal = true
    auto conexion = proveedor.conexion();
    QVERIFY(!conexion);
    QCOMPARE(conexion.error().tipo, ErrorPersistencia::Tipo::Almacenamiento);
    QCOMPARE(proveedor.conexionesAbiertas(), 0);
    QVERIFY(!proveedor.tieneConexionEnHiloActual());
}

void TestSqliteConexion::rutaInaccesibleEsAlmacenamiento()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto r = inicializarBaseSqlite(dir.filePath(QStringLiteral("no-existe/satcfdi.sqlite3")));
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::Almacenamiento);
}

void TestSqliteConexion::beginImmediateTomaBloqueoDeEscritura()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));
    OpcionesConexionSqlite rapido;
    rapido.busyTimeoutMs = 50;

    // Dos proveedores = dos conexiones distintas a la misma base.
    SqliteConnectionProvider a(rutaBase(dir), rapido);
    SqliteConnectionProvider b(rutaBase(dir), rapido);
    SqliteUnitOfWork uowA(a);
    SqliteUnitOfWork uowB(b);

    QVERIFY(uowA.begin());
    QVERIFY(a.transaccionActiva());
    // BEGIN DEFERRED no tomaria bloqueo; IMMEDIATE si: el segundo begin se
    // agota en busy_timeout y se traduce a Ocupado.
    auto bloqueado = uowB.begin();
    QVERIFY(!bloqueado);
    QCOMPARE(bloqueado.error().tipo, ErrorPersistencia::Tipo::Ocupado);
    QVERIFY(!b.transaccionActiva());
    // Las lecturas de B siguen funcionando (WAL).
    SqliteConfiguracionAppRepository configB(b);
    QVERIFY(configB.obtener());

    QVERIFY(uowA.commit());
    QVERIFY(uowB.begin());
    QVERIFY(uowB.rollback());
    a.cerrarConexionDelHiloActual();
    b.cerrarConexionDelHiloActual();
}

void TestSqliteConexion::unidadDeTrabajoValidaEstado()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));
    SqlitePersistencia persistencia(rutaBase(dir));
    UnitOfWork& uow = persistencia.unidadDeTrabajo();

    auto commitSinBegin = uow.commit();
    QVERIFY(!commitSinBegin);
    QCOMPARE(commitSinBegin.error().tipo, ErrorPersistencia::Tipo::Transaccion);
    QVERIFY(uow.rollback()); // idempotente sin transaccion

    QVERIFY(uow.begin());
    auto anidado = uow.begin();
    QVERIFY(!anidado);
    QCOMPARE(anidado.error().tipo, ErrorPersistencia::Tipo::Transaccion);

    // Rollback descarta lo escrito.
    QVERIFY(persistencia.perfiles().insertar(nuevoPerfil(kRfcPerfil)));
    QVERIFY(uow.rollback());
    QVERIFY(uow.rollback());
    auto lista = persistencia.perfiles().listarActivosVisibles();
    QVERIFY(lista);
    QVERIFY(lista.valor().isEmpty());

    // Commit persiste.
    QVERIFY(uow.begin());
    QVERIFY(persistencia.perfiles().insertar(nuevoPerfil(kRfcPerfil)));
    QVERIFY(uow.commit());
    lista = persistencia.perfiles().listarActivosVisibles();
    QVERIFY(lista);
    QCOMPARE(lista.valor().size(), 1);
    persistencia.cerrarConexionDelHiloActual();
}

void TestSqliteConexion::escrituraSinTransaccionFalla()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));
    SqlitePersistencia persistencia(rutaBase(dir));
    auto r = persistencia.perfiles().insertar(nuevoPerfil(kRfcPerfil));
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::Transaccion);
    auto m = persistencia.solicitudes().marcarEliminadaVisible(SolicitudId::generar(),
                                                               timestamp::ahoraUtc());
    QVERIFY(!m);
    QCOMPARE(m.error().tipo, ErrorPersistencia::Tipo::Transaccion);
    persistencia.cerrarConexionDelHiloActual();
}

void TestSqliteConexion::conexionPorHiloSeCierraEnSuHilo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));
    SqlitePersistencia persistencia(rutaBase(dir));
    QCOMPARE(persistencia.conexionesAbiertas(), 0); // perezosa

    QVERIFY(persistencia.configuracion().obtener());
    const QString nombrePrincipal = persistencia.proveedor().nombreConexionHiloActual();
    QVERIFY(!nombrePrincipal.isEmpty());
    QCOMPARE(persistencia.conexionesAbiertas(), 1);

    QString nombreHilo;
    bool lecturaOk = false;
    bool escrituraOk = false;
    int abiertasDuranteHilo = 0;
    std::unique_ptr<QThread> hilo(QThread::create([&] {
        lecturaOk = persistencia.configuracion().obtener().esExito();
        nombreHilo = persistencia.proveedor().nombreConexionHiloActual();
        abiertasDuranteHilo = persistencia.conexionesAbiertas();
        // Transaccion propia del hilo, independiente de la del principal.
        escrituraOk = persistencia.unidadDeTrabajo().begin().esExito()
                      && persistencia.perfiles().insertar(nuevoPerfil(kRfcOtro)).esExito()
                      && persistencia.unidadDeTrabajo().commit().esExito();
        persistencia.cerrarConexionDelHiloActual();
    }));
    hilo->start();
    QVERIFY(hilo->wait(10000));
    QVERIFY(lecturaOk);
    QVERIFY(escrituraOk);
    QCOMPARE(abiertasDuranteHilo, 2);
    QVERIFY(!nombreHilo.isEmpty());
    QVERIFY(nombreHilo != nombrePrincipal);
    QVERIFY(!QSqlDatabase::contains(nombreHilo));
    QCOMPARE(persistencia.conexionesAbiertas(), 1);
    QVERIFY(!persistencia.proveedor().transaccionActiva());

    auto perfiles = persistencia.perfiles().listarActivosVisibles();
    QVERIFY(perfiles);
    QCOMPARE(perfiles.valor().size(), 1);
    persistencia.cerrarConexionDelHiloActual();
    QVERIFY(!QSqlDatabase::contains(nombrePrincipal));
    QCOMPARE(persistencia.conexionesAbiertas(), 0);
}

void TestSqliteConexion::cierreConTransaccionAbiertaRevierte()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));
    {
        SqlitePersistencia persistencia(rutaBase(dir));
        QVERIFY(persistencia.unidadDeTrabajo().begin());
        QVERIFY(persistencia.perfiles().insertar(nuevoPerfil(kRfcPerfil)));
        persistencia.cerrarConexionDelHiloActual();
        QCOMPARE(persistencia.conexionesAbiertas(), 0);
    }
    SqlitePersistencia persistencia(rutaBase(dir));
    auto lista = persistencia.perfiles().listarActivosVisibles();
    QVERIFY(lista);
    QVERIFY(lista.valor().isEmpty());
    // El bloqueo se libero: una nueva transaccion arranca.
    QVERIFY(persistencia.unidadDeTrabajo().begin());
    QVERIFY(persistencia.unidadDeTrabajo().rollback());
    persistencia.cerrarConexionDelHiloActual();
}

void TestSqliteConexion::cierreSinWarnings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));
    QString nombre;
    {
        SqlitePersistencia persistencia(rutaBase(dir));
        QVERIFY(persistencia.unidadDeTrabajo().begin());
        QVERIFY(persistencia.perfiles().insertar(nuevoPerfil(kRfcPerfil)));
        QVERIFY(persistencia.unidadDeTrabajo().commit());
        QVERIFY(persistencia.perfiles().listarActivosVisibles());
        QVERIFY(persistencia.solicitudes().listarVisibles());
        QVERIFY(persistencia.paquetes().contarVisiblesPorSolicitud());
        QVERIFY(persistencia.configuracion().obtener());
        nombre = persistencia.proveedor().nombreConexionHiloActual();
        QVERIFY(QSqlDatabase::contains(nombre));
        persistencia.cerrarConexionDelHiloActual();
        QVERIFY(!QSqlDatabase::contains(nombre));
        persistencia.cerrarConexionDelHiloActual(); // idempotente
    }
    // Destruir sin cerrar explicitamente tambien cierra la del hilo actual.
    {
        SqlitePersistencia persistencia(rutaBase(dir));
        QVERIFY(persistencia.configuracion().obtener());
        nombre = persistencia.proveedor().nombreConexionHiloActual();
    }
    QVERIFY(!QSqlDatabase::contains(nombre));
}
