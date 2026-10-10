#include "TestSqliteMigraciones.h"

#include "SqlitePruebasComun.h"

#include "infrastructure/persistence/sqlite/SqliteMigrationRunner.h"

#include <QFileInfo>
#include <QSqlDatabase>
#include <QTest>
#include <QVersionNumber>

using namespace satcfdi;
using namespace satcfdi::pruebas;

namespace {

OpcionesConexionSqlite sinWal()
{
    OpcionesConexionSqlite o;
    o.exigirWal = false;
    return o;
}

QList<MigracionSql> embebidas()
{
    auto m = migracionesSqliteEmbebidas();
    return m ? m.valor() : QList<MigracionSql>{};
}

// Version mas reciente embebida (T007: deriva de la lista, no un literal).
int ultimaVersion()
{
    const QList<MigracionSql> m = embebidas();
    return m.isEmpty() ? 0 : m.last().version;
}

QList<int> versionesEmbebidas()
{
    QList<int> v;
    for (const MigracionSql& m : embebidas()) {
        v.append(m.version);
    }
    return v;
}

QStringList versionesTexto()
{
    QStringList v;
    for (int n : versionesEmbebidas()) {
        v.append(QString::number(n));
    }
    return v;
}

const QString kObjetosUsuario = QStringLiteral(
    "SELECT type || ':' || name FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY 1");

} // namespace

void TestSqliteMigraciones::driverDisponibleYDm4()
{
    QVERIFY(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteConnectionProvider proveedor(rutaBase(dir), sinWal());
    QVERIFY(proveedor.conexion());
    auto version = proveedor.versionSqlite();
    QVERIFY(version);
    QVERIFY(QVersionNumber::fromString(version.valor()) >= QVersionNumber(3, 9, 0));
    QCOMPARE(escalar(proveedor, QStringLiteral("SELECT json_valid('{}')")).toInt(), 1);
    QCOMPARE(escalar(proveedor, QStringLiteral("SELECT json_type('[1]')")).toString(),
             QStringLiteral("array"));
    proveedor.cerrarConexionDelHiloActual();
}

void TestSqliteMigraciones::recursoEmbebidoIgualAlFuente()
{
    QVERIFY(QFile::exists(QStringLiteral(":/migrations/001_initial_schema.sql")));
    const QByteArray recurso = leerArchivo(QStringLiteral(":/migrations/001_initial_schema.sql"));
    const QByteArray fuente = leerArchivo(QStringLiteral(SATCFDI_MIGRACION_001_FUENTE));
    QVERIFY(!fuente.isEmpty());
    QCOMPARE(recurso, fuente);

    // T005: 002 embebida igual al fuente.
    const QByteArray recurso002 = leerArchivo(QStringLiteral(":/migrations/002_credencial_metadata.sql"));
    const QByteArray fuente002 = leerArchivo(QStringLiteral(SATCFDI_MIGRACION_002_FUENTE));
    QVERIFY(!fuente002.isEmpty());
    QCOMPARE(recurso002, fuente002);

    auto lista = migracionesSqliteEmbebidas();
    QVERIFY(lista);
    QCOMPARE(lista.valor().size(), 4);
    for (int i = 0; i < lista.valor().size(); ++i) {
        QCOMPARE(lista.valor().at(i).version, i + 1); // consecutivas desde 1
    }
    QCOMPARE(lista.valor().first().version, 1);
    QCOMPARE(lista.valor().first().sql.toUtf8(), fuente);
    QCOMPARE(lista.valor().at(1).version, 2);
    QCOMPARE(lista.valor().at(1).nombre, QStringLiteral("002_credencial_metadata"));
    QCOMPARE(lista.valor().at(1).sql.toUtf8(), fuente002);

    // T007: 003 embebida igual al fuente.
    const QByteArray fuente003 = leerArchivo(QStringLiteral(SATCFDI_MIGRACION_003_FUENTE));
    QVERIFY(!fuente003.isEmpty());
    QCOMPARE(lista.valor().at(2).nombre, QStringLiteral("003_worker_ejecutor"));
    QCOMPARE(lista.valor().at(2).sql.toUtf8(), fuente003);

    // T014.2: 004 embebida igual al fuente.
    const QByteArray fuente004 = leerArchivo(QStringLiteral(SATCFDI_MIGRACION_004_FUENTE));
    QVERIFY(!fuente004.isEmpty());
    QCOMPARE(lista.valor().at(3).nombre, QStringLiteral("004_reintento_paquete"));
    QCOMPARE(lista.valor().at(3).sql.toUtf8(), fuente004);
}

void TestSqliteMigraciones::dividirSentenciasDm2()
{
    const QString sql = QStringLiteral(
        "-- solo comentario\n"
        ";\n"
        "CREATE TABLE a (\n"
        "    id INTEGER\n"
        "--  comentario interno\n"
        ")\n"
        "  ;  \r\n"
        "\n"
        ";\n"
        "INSERT INTO a VALUES (1)\n"
        ";\n");
    auto r = SqliteMigrationRunner::dividirSentencias(sql);
    QVERIFY(r);
    QCOMPARE(r.valor().size(), 2);
    QVERIFY(r.valor().at(0).startsWith(QStringLiteral("CREATE TABLE a")));
    QVERIFY(r.valor().at(0).contains(QStringLiteral("--  comentario interno")));
    QCOMPARE(r.valor().at(1), QStringLiteral("INSERT INTO a VALUES (1)"));

    // La migracion real se divide sin errores y en mas de una sentencia.
    auto real = SqliteMigrationRunner::dividirSentencias(embebidas().first().sql);
    QVERIFY(real);
    QVERIFY(real.valor().size() > 10);
    for (const QString& s : real.valor()) {
        QVERIFY(!s.trimmed().isEmpty());
        QVERIFY(!s.trimmed().endsWith(QLatin1Char(';')));
    }
}

void TestSqliteMigraciones::dividirSentenciasRechazaSinTerminadorYBom()
{
    auto sinTerminador =
        SqliteMigrationRunner::dividirSentencias(QStringLiteral("CREATE TABLE a (id INTEGER);\n"));
    QVERIFY(!sinTerminador);
    QCOMPARE(sinTerminador.error().tipo, ErrorPersistencia::Tipo::Migracion);

    auto conBom = SqliteMigrationRunner::dividirSentencias(
        QString(QChar(0xFEFF)) + QStringLiteral("CREATE TABLE a (id INTEGER)\n;\n"));
    QVERIFY(!conBom);
    QCOMPARE(conBom.error().tipo, ErrorPersistencia::Tipo::Migracion);

    // Comentario final sin terminador es valido.
    QVERIFY(SqliteMigrationRunner::dividirSentencias(QStringLiteral("-- fin\n   \n")));
}

void TestSqliteMigraciones::migraBaseVacia()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto informe = inicializarBaseSqlite(rutaBase(dir));
    QVERIFY2(informe, informe ? "" : qPrintable(informe.error().mensaje));
    QCOMPARE(informe.valor().migracion.versionInicial, 0);
    QCOMPARE(informe.valor().migracion.versionFinal, ultimaVersion());
    QCOMPARE(informe.valor().migracion.aplicadas, versionesEmbebidas());
    QCOMPARE(informe.valor().journalMode, QStringLiteral("wal"));
    QVERIFY(!informe.valor().versionSqlite.isEmpty());

    SqliteConnectionProvider proveedor(rutaBase(dir));
    const QStringList tablas = columna(
        proveedor, QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' "
                                  "AND name NOT LIKE 'sqlite_%' ORDER BY name"));
    QCOMPARE(tablas, (QStringList{QStringLiteral("configuracion_app"), QStringLiteral("credencial_sat"),
                                  QStringLiteral("log_solicitud"), QStringLiteral("paquete_solicitud"),
                                  QStringLiteral("perfil_sat"), QStringLiteral("schema_migrations"),
                                  QStringLiteral("solicitud_masiva")}));
    QCOMPARE(columna(proveedor, QStringLiteral("SELECT version FROM schema_migrations ORDER BY version")),
             versionesTexto());
    const QString aplicada =
        escalar(proveedor, QStringLiteral("SELECT aplicada_en FROM schema_migrations "
                                          "WHERE version = %1").arg(ultimaVersion())).toString();
    QVERIFY(timestamp::desdeTexto(aplicada).has_value());
    QCOMPARE(escalar(proveedor, QStringLiteral("SELECT count(*) FROM configuracion_app")).toInt(), 1);
    proveedor.cerrarConexionDelHiloActual();
}

void TestSqliteMigraciones::segundaAperturaNoDuplica()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));
    QStringList antes;
    {
        SqliteConnectionProvider proveedor(rutaBase(dir));
        antes = columna(proveedor, kObjetosUsuario);
    }
    auto segunda = inicializarBaseSqlite(rutaBase(dir));
    QVERIFY(segunda);
    QCOMPARE(segunda.valor().migracion.versionInicial, ultimaVersion());
    QCOMPARE(segunda.valor().migracion.versionFinal, ultimaVersion());
    QVERIFY(segunda.valor().migracion.aplicadas.isEmpty());

    SqliteConnectionProvider proveedor(rutaBase(dir));
    QCOMPARE(columna(proveedor, kObjetosUsuario), antes);
    QCOMPARE(escalar(proveedor, QStringLiteral("SELECT count(*) FROM schema_migrations")).toInt(),
             versionesEmbebidas().size());
    QCOMPARE(escalar(proveedor, QStringLiteral("SELECT count(*) FROM configuracion_app")).toInt(), 1);
    proveedor.cerrarConexionDelHiloActual();
}

void TestSqliteMigraciones::indicesParcialesYJson1()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));
    SqliteConnectionProvider proveedor(rutaBase(dir));
    const QString sqlDedup = escalar(proveedor,
                                     QStringLiteral("SELECT sql FROM sqlite_master WHERE name = "
                                                    "'ux_solicitud_masiva_dedup_bloqueante'"))
                                 .toString();
    QVERIFY(sqlDedup.contains(QStringLiteral("WHERE eliminado_en IS NULL")));

    // El indice parcial permite la misma clave si la anterior esta eliminada
    // o en un estado no bloqueante, y la rechaza si ambas bloquean.
    const QString perfil = uuid::generarCanonico();
    QCOMPARE(insertarPerfilFixture(proveedor, perfil, kRfcPerfil), QString());
    const DedupKey clave = DedupKey::calcularV1("indices-parciales");
    const SolicitudFixture eliminada = fixture(perfil, clave, QStringLiteral("Creada"), {}, true);
    QCOMPARE(insertarSolicitudFixture(proveedor, eliminada), QString());
    const SolicitudFixture fallida = fixture(perfil, clave, QStringLiteral("EnvioFallido"));
    QCOMPARE(insertarSolicitudFixture(proveedor, fallida), QString());
    QCOMPARE(insertarSolicitudFixture(proveedor, fixture(perfil, clave)), QString());
    QVERIFY(!insertarSolicitudFixture(proveedor, fixture(perfil, clave)).isEmpty());

    // JSON1 en CHECK: un objeto no es arreglo de receptores.
    const QString error = ejecutarSql(
        proveedor, QStringLiteral("UPDATE solicitud_masiva SET rfc_receptores_json = '{}' "
                                  "WHERE id = %1")
                       .arg(citar(fallida.id)));
    QVERIFY(error.contains(QStringLiteral("CHECK")));
    proveedor.cerrarConexionDelHiloActual();
}

void TestSqliteMigraciones::migracionRotaHaceRollbackCompleto()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));

    QList<MigracionSql> lista = embebidas();
    const int rota = ultimaVersion() + 1;
    lista.append(MigracionSql{rota, QStringLiteral("rota"),
                              QStringLiteral("CREATE TABLE tabla_parcial (id INTEGER)\n;\n"
                                             "INSERT INTO tabla_inexistente VALUES (1)\n;\n")});
    auto r = inicializarBaseSqlite(rutaBase(dir), lista);
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::Migracion);
    QVERIFY(r.error().mensaje.contains(QStringLiteral("migracion %1").arg(rota)));

    SqliteConnectionProvider proveedor(rutaBase(dir));
    QCOMPARE(escalar(proveedor, QStringLiteral("SELECT count(*) FROM sqlite_master "
                                               "WHERE name = 'tabla_parcial'"))
                 .toInt(),
             0);
    QCOMPARE(escalar(proveedor, QStringLiteral("SELECT max(version) FROM schema_migrations")).toInt(),
             ultimaVersion());
    QVERIFY(!proveedor.transaccionActiva());
    proveedor.cerrarConexionDelHiloActual();
}

void TestSqliteMigraciones::migracionRotaSobreBaseVaciaNoDejaSchemaMigrations()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QList<MigracionSql> lista{
        MigracionSql{1, QStringLiteral("001_rota"),
                     QStringLiteral("CREATE TABLE t1 (id INTEGER)\n;\nSELECT * FROM nada\n;\n")}};
    auto r = inicializarBaseSqlite(rutaBase(dir), lista);
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::Migracion);

    SqliteConnectionProvider proveedor(rutaBase(dir), sinWal());
    QCOMPARE(columna(proveedor, kObjetosUsuario), QStringList());
    proveedor.cerrarConexionDelHiloActual();

    // Tras el fallo, la migracion correcta se aplica sobre la misma base.
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));
}

void TestSqliteMigraciones::versionFuturaSeRechazaSinModificarArchivo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ruta = rutaBase(dir);
    QVERIFY(inicializarBaseSqlite(ruta));
    {
        SqliteConnectionProvider proveedor(ruta);
        QCOMPARE(ejecutarSql(proveedor,
                             QStringLiteral("INSERT INTO schema_migrations VALUES (99, %1)")
                                 .arg(citar(kAhora))),
                 QString());
        QCOMPARE(escalar(proveedor, QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)")).toInt(), 0);
        proveedor.cerrarConexionDelHiloActual();
    }
    // Tras checkpoint y cierre, el -wal no existe o queda vacio (el SQLite
    // de macOS puede conservarlo con 0 bytes).
    QCOMPARE(QFileInfo(ruta + QStringLiteral("-wal")).size(), 0);
    const QByteArray antes = huellaArchivo(ruta);
    const qint64 tamano = QFileInfo(ruta).size();
    QVERIFY(!antes.isEmpty());

    auto r = inicializarBaseSqlite(ruta);
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::Migracion);
    QVERIFY(r.error().mensaje.contains(QStringLiteral("futura")));
    QCOMPARE(huellaArchivo(ruta), antes);
    QCOMPARE(QFileInfo(ruta).size(), tamano);
    QCOMPARE(QFileInfo(ruta + QStringLiteral("-wal")).size(), 0);

    SqliteConnectionProvider proveedor(ruta);
    QCOMPARE(escalar(proveedor, QStringLiteral("SELECT max(version) FROM schema_migrations")).toInt(),
             99);
    proveedor.cerrarConexionDelHiloActual();
}

void TestSqliteMigraciones::tablasSinSchemaMigrationsFalla()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ruta = rutaBase(dir);
    {
        SqliteConnectionProvider proveedor(ruta, sinWal());
        QCOMPARE(ejecutarSql(proveedor, QStringLiteral("CREATE TABLE ajena (id INTEGER)")), QString());
        proveedor.cerrarConexionDelHiloActual();
    }
    const QByteArray antes = huellaArchivo(ruta);
    auto r = inicializarBaseSqlite(ruta);
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::Migracion);
    QCOMPARE(huellaArchivo(ruta), antes);

    SqliteConnectionProvider proveedor(ruta, sinWal());
    QCOMPARE(columna(proveedor, kObjetosUsuario), QStringList{QStringLiteral("table:ajena")});
    proveedor.cerrarConexionDelHiloActual();
}

void TestSqliteMigraciones::listaInvalidaFalla()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const MigracionSql m{1, QStringLiteral("a"), QStringLiteral("CREATE TABLE a (id INTEGER)\n;\n")};
    auto duplicada = inicializarBaseSqlite(rutaBase(dir), {m, m});
    QVERIFY(!duplicada);
    QCOMPARE(duplicada.error().tipo, ErrorPersistencia::Tipo::Migracion);
    auto cero = inicializarBaseSqlite(rutaBase(dir), {MigracionSql{0, QStringLiteral("x"), m.sql}});
    QVERIFY(!cero);
    QCOMPARE(cero.error().tipo, ErrorPersistencia::Tipo::Migracion);
}

void TestSqliteMigraciones::integridadTrasMigrar()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(inicializarBaseSqlite(rutaBase(dir)));
    SqliteConnectionProvider proveedor(rutaBase(dir));
    QCOMPARE(escalar(proveedor, QStringLiteral("PRAGMA integrity_check")).toString(),
             QStringLiteral("ok"));
    QCOMPARE(columna(proveedor, QStringLiteral("PRAGMA foreign_key_check")), QStringList());
    proveedor.cerrarConexionDelHiloActual();
}
