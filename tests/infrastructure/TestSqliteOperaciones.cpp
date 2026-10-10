#include "TestSqliteOperaciones.h"

#include "SqlitePruebasComun.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "domain/logs/LogSolicitud.h"
#include "domain/perfiles/PerfilSat.h"
#include "infrastructure/persistence/sqlite/SqliteOperacionesConsultas.h"
#include "ports/repositories/LogSolicitudRepository.h"
#include "ports/repositories/OperacionesSolicitudRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QRegularExpression>
#include <QSqlRecord>
#include <QTest>
#include <QThread>
#include <QTimeZone>

#include <algorithm>
#include <atomic>
#include <climits>
#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;

namespace {

const QDateTime kT0 = QDateTime(QDate(2026, 10, 3), QTime(12, 0, 0), QTimeZone(QTimeZone::UTC));

QDateTime en(int minutos)
{
    return kT0.addSecs(qint64(minutos) * 60);
}

QString ts(int minutos)
{
    return timestamp::aTexto(en(minutos));
}

SolicitudId sid(const QString& texto)
{
    return *SolicitudId::desdeTexto(texto);
}

PerfilId pid(const QString& texto)
{
    return *PerfilId::desdeTexto(texto);
}

QStringList ordenados(QStringList l)
{
    std::sort(l.begin(), l.end());
    return l;
}

// Fila completa como texto (valores separados por '|'), para comprobar que
// una escritura que no aplica no modifico nada.
QString fila(SqliteConnectionProvider& prov, const QString& tabla, const QString& id)
{
    auto conexion = prov.conexion();
    if (!conexion) {
        return QStringLiteral("<sin conexion>");
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT * FROM %1 WHERE id = %2").arg(tabla, citar(id))) || !q.next()) {
        return QStringLiteral("<sin fila>");
    }
    QStringList valores;
    for (int i = 0; i < q.record().count(); ++i) {
        valores.append(q.value(i).isNull() ? QStringLiteral("<NULL>") : q.value(i).toString());
    }
    return valores.join(QLatin1Char('|'));
}

// integrity_check (incluye CHECK, NOT NULL y UNIQUE) y foreign_key_check.
bool consistente(SqliteConnectionProvider& prov)
{
    return escalar(prov, QStringLiteral("PRAGMA integrity_check")).toString() == QStringLiteral("ok")
           && columna(prov, QStringLiteral("PRAGMA foreign_key_check")).isEmpty();
}

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
    SqliteConnectionProvider& prov() { return p->proveedor(); }
    OperacionesSolicitudRepository& ops() { return p->operaciones(); }

    QString perfil(const QString& rfc)
    {
        const QString id = uuid::generarCanonico();
        return insertarPerfilFixture(prov(), id, rfc).isEmpty() ? id : QString();
    }
    QString solicitud(const QString& perfilId, const QString& estadoLocal, const QString& estadoSat = {},
                      bool eliminada = false)
    {
        const SolicitudFixture f = fixture(perfilId, DedupKey::calcularV1(uuid::generarCanonico().toUtf8()),
                                           estadoLocal, estadoSat, eliminada);
        return insertarSolicitudFixture(prov(), f).isEmpty() ? f.id : QString();
    }
    QString paquete(const QString& solicitudId, const QString& estado, bool eliminado = false)
    {
        const QString id = uuid::generarCanonico();
        return insertarPaqueteFixture(prov(), id, solicitudId, QStringLiteral("PAQ_") + id.left(8), estado, eliminado)
                       .isEmpty()
                   ? id
                   : QString();
    }
    QString sql(const QString& s) { return ejecutarSql(prov(), s); }
    QString set(const QString& tabla, const QString& id, const QString& asignaciones)
    {
        return sql(QStringLiteral("UPDATE %1 SET %2 WHERE id = %3").arg(tabla, asignaciones, citar(id)));
    }
    QString campo(const QString& tabla, const QString& col, const QString& id)
    {
        return columna(prov(), QStringLiteral("SELECT %1 FROM %2 WHERE id = %3").arg(col, tabla, citar(id))).value(0);
    }
    QString s(const QString& col, const QString& id) { return campo(QStringLiteral("solicitud_masiva"), col, id); }
    QString pq(const QString& col, const QString& id) { return campo(QStringLiteral("paquete_solicitud"), col, id); }

    template <typename F>
    auto tx(F f)
    {
        (void)p->unidadDeTrabajo().begin();
        auto r = f();
        if (r) {
            (void)p->unidadDeTrabajo().commit();
        } else {
            (void)p->unidadDeTrabajo().rollback();
        }
        return r;
    }
};

QStringList idsPaquetes(const QList<PaqueteDescargable>& l)
{
    QStringList ids;
    for (const PaqueteDescargable& d : l) {
        ids.append(d.paquete.id);
    }
    return ids;
}

const QString kNulo = QStringLiteral("<NULL>");

} // namespace

// --- Migracion 003 --------------------------------------------------------------

void TestSqliteOperaciones::migracion003SobreBase002ConDatos()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ruta = rutaBase(dir);
    auto embebidas = migracionesSqliteEmbebidas();
    QVERIFY(embebidas);
    QVERIFY(embebidas.valor().size() >= 3);
    QVERIFY(inicializarBaseSqlite(ruta, embebidas.valor().mid(0, 2)));

    const QString huellaSql = QStringLiteral(
        "SELECT quote(id) || quote(solicitud_masiva_id) || quote(tipo_evento) || quote(origen) || "
        "quote(origen_codigo_sat) || quote(codigo_sat) || quote(mensaje_sat) || quote(payload_resumen_json) || "
        "quote(creado_en) || quote(eliminado_en) FROM log_solicitud ORDER BY id");
    const QString perfil = uuid::generarCanonico();
    QStringList huellaAntes;
    {
        SqliteConnectionProvider prov(ruta);
        QCOMPARE(insertarPerfilFixture(prov, perfil, kRfcPerfil), QString());
        const SolicitudFixture enviada = fixture(perfil, DedupKey::calcularV1("m003-a"), QStringLiteral("Enviada"),
                                                 QStringLiteral("Aceptada"));
        const SolicitudFixture borrada =
            fixture(perfil, DedupKey::calcularV1("m003-b"), QStringLiteral("Creada"), {}, true);
        QCOMPARE(insertarSolicitudFixture(prov, enviada), QString());
        QCOMPARE(insertarSolicitudFixture(prov, borrada), QString());
        QCOMPARE(insertarPaqueteFixture(prov, uuid::generarCanonico(), enviada.id, QStringLiteral("PAQ_1"),
                                        QStringLiteral("Disponible")),
                 QString());
        const QString insertarLog = QStringLiteral("INSERT INTO log_solicitud VALUES (%1, %2, %3, %4, %5, %6, %7, "
                                                   "%8, %9, %10)");
        QCOMPARE(ejecutarSql(prov, insertarLog.arg(citar(uuid::generarCanonico()), citar(enviada.id),
                                                   QStringLiteral("'solicitud_enviada'"), QStringLiteral("'worker'"),
                                                   QStringLiteral("'creacion'"), QStringLiteral("'5000'"),
                                                   QStringLiteral("'Solicitud Aceptada'"),
                                                   QStringLiteral("'{\"n\":1}'"), citar(kAhora))
                                        .arg(QStringLiteral("NULL"))),
                 QString());
        QCOMPARE(ejecutarSql(prov, insertarLog.arg(citar(uuid::generarCanonico()), citar(enviada.id),
                                                   QStringLiteral("'accion_pendiente_descartada'"),
                                                   QStringLiteral("'usuario'"), QStringLiteral("NULL"),
                                                   QStringLiteral("NULL"), QStringLiteral("NULL"),
                                                   QStringLiteral("NULL"), citar(kAhora))
                                        .arg(QStringLiteral("NULL"))),
                 QString());
        QCOMPARE(ejecutarSql(prov, insertarLog.arg(citar(uuid::generarCanonico()), citar(borrada.id),
                                                   QStringLiteral("'solicitud_creada'"), QStringLiteral("'usuario'"),
                                                   QStringLiteral("NULL"), QStringLiteral("NULL"),
                                                   QStringLiteral("NULL"), QStringLiteral("NULL"), citar(kAhora))
                                        .arg(citar(kAhora))),
                 QString());
        // Antes de 003 el catalogo no admite los eventos nuevos.
        QVERIFY(ejecutarSql(prov, insertarLog.arg(citar(uuid::generarCanonico()), citar(enviada.id),
                                                  QStringLiteral("'envio_no_iniciado'"), QStringLiteral("'worker'"),
                                                  QStringLiteral("NULL"), QStringLiteral("NULL"),
                                                  QStringLiteral("NULL"), QStringLiteral("NULL"), citar(kAhora))
                                       .arg(QStringLiteral("NULL")))
                    .contains(QStringLiteral("CHECK")));
        huellaAntes = columna(prov, huellaSql);
        QCOMPARE(huellaAntes.size(), 3);
        prov.cerrarConexionDelHiloActual();
    }

    auto migrada = inicializarBaseSqlite(ruta);
    QVERIFY2(migrada, migrada ? "" : qPrintable(migrada.error().mensaje));
    QCOMPARE(migrada.valor().migracion.versionInicial, 2);
    QCOMPARE(migrada.valor().migracion.aplicadas.first(), 3);
    QCOMPARE(migrada.valor().migracion.versionFinal, embebidas.valor().last().version);

    SqliteConnectionProvider prov(ruta);
    // Copia fiel de log_solicitud (incluidos eliminados), indice recreado y
    // sin tabla temporal.
    QCOMPARE(columna(prov, huellaSql), huellaAntes);
    QCOMPARE(columna(prov, QStringLiteral("SELECT tbl_name FROM sqlite_master WHERE name = 'ix_log_solicitud_detalle'")),
             QStringList{QStringLiteral("log_solicitud")});
    QCOMPARE(escalar(prov, QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name = 'log_solicitud_v3'")).toInt(),
             0);
    // Catalogo nuevo y CHECK vigente.
    const QString solicitudId =
        escalar(prov, QStringLiteral("SELECT id FROM solicitud_masiva WHERE eliminado_en IS NULL")).toString();
    for (const QString& tipo : {QStringLiteral("envio_no_iniciado"), QStringLiteral("verificacion_suspendida")}) {
        QCOMPARE(ejecutarSql(prov, QStringLiteral("INSERT INTO log_solicitud (id, solicitud_masiva_id, tipo_evento, "
                                                  "origen, creado_en) VALUES (%1, %2, %3, 'worker', %4)")
                                       .arg(citar(uuid::generarCanonico()), citar(solicitudId), citar(tipo),
                                            citar(kAhora))),
                 QString());
    }
    QVERIFY(ejecutarSql(prov, QStringLiteral("INSERT INTO log_solicitud (id, solicitud_masiva_id, tipo_evento, "
                                             "origen, creado_en) VALUES (%1, %2, 'inventado', 'worker', %3)")
                                  .arg(citar(uuid::generarCanonico()), citar(solicitudId), citar(kAhora)))
                .contains(QStringLiteral("CHECK")));
    // La FK sigue declarada en la tabla reconstruida.
    QVERIFY(ejecutarSql(prov, QStringLiteral("INSERT INTO log_solicitud (id, solicitud_masiva_id, tipo_evento, "
                                             "origen, creado_en) VALUES (%1, %2, 'solicitud_creada', 'worker', %3)")
                                  .arg(citar(uuid::generarCanonico()), citar(uuid::generarCanonico()), citar(kAhora)))
                .contains(QStringLiteral("FOREIGN KEY")));

    // Columnas de racha: filas previas con (NULL, 0) y CHECK de coherencia.
    QCOMPARE(columna(prov, QStringLiteral("SELECT (ultima_clave_falla_verificacion IS NULL) || ':' || "
                                          "fallas_verificacion_iguales FROM solicitud_masiva")),
             (QStringList{QStringLiteral("1:0"), QStringLiteral("1:0")}));
    const QString racha = QStringLiteral("UPDATE solicitud_masiva SET ultima_clave_falla_verificacion = %1, "
                                         "fallas_verificacion_iguales = %2 WHERE id = %3");
    QVERIFY(ejecutarSql(prov, racha.arg(QStringLiteral("NULL"), QStringLiteral("1"), citar(solicitudId)))
                .contains(QStringLiteral("CHECK")));
    QVERIFY(ejecutarSql(prov, racha.arg(QStringLiteral("'Preparacion'"), QStringLiteral("0"), citar(solicitudId)))
                .contains(QStringLiteral("CHECK")));
    QVERIFY(ejecutarSql(prov, racha.arg(QStringLiteral("'  '"), QStringLiteral("1"), citar(solicitudId)))
                .contains(QStringLiteral("CHECK")));
    QCOMPARE(ejecutarSql(prov, racha.arg(QStringLiteral("'RespuestaExplicita:300'"), QStringLiteral("2"),
                                         citar(solicitudId))),
             QString());
    QVERIFY(consistente(prov));

    // integrity_check detecta CHECKs violados: valida el criterio usado por
    // consistente() tras cada transicion.
    QCOMPARE(ejecutarSql(prov, QStringLiteral("PRAGMA ignore_check_constraints = ON")), QString());
    QCOMPARE(ejecutarSql(prov, racha.arg(QStringLiteral("NULL"), QStringLiteral("5"), citar(solicitudId))), QString());
    QCOMPARE(ejecutarSql(prov, QStringLiteral("PRAGMA ignore_check_constraints = OFF")), QString());
    QVERIFY(escalar(prov, QStringLiteral("PRAGMA integrity_check")).toString().contains(QStringLiteral("CHECK")));
    QCOMPARE(ejecutarSql(prov, racha.arg(QStringLiteral("NULL"), QStringLiteral("0"), citar(solicitudId))), QString());
    QVERIFY(consistente(prov));
    prov.cerrarConexionDelHiloActual();

    auto repetida = inicializarBaseSqlite(ruta);
    QVERIFY(repetida);
    QVERIFY(repetida.valor().migracion.aplicadas.isEmpty());
}

// T014.2: 004 sobre una base en 003 con datos: agrega reintento_pendiente_en
// (NULL en filas previas) e indice parcial sin tocar el resto; idempotente.
void TestSqliteOperaciones::migracion004SobreBase003ConDatos()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ruta = rutaBase(dir);
    auto embebidas = migracionesSqliteEmbebidas();
    QVERIFY(embebidas);
    QVERIFY(embebidas.valor().size() >= 4);
    QVERIFY(inicializarBaseSqlite(ruta, embebidas.valor().mid(0, 3)));

    const QString huellaSql = QStringLiteral(
        "SELECT quote(id) || quote(solicitud_masiva_id) || quote(id_paquete_sat) || quote(estado_descarga) || "
        "quote(ruta_local) || quote(disponible_en) || quote(descargado_en) || quote(codigo_descarga_sat) || "
        "quote(ultimo_error) || quote(eliminado_en) FROM paquete_solicitud ORDER BY id");
    const QString perfil = uuid::generarCanonico();
    QStringList huellaAntes;
    QString paqueteError;
    {
        SqliteConnectionProvider prov(ruta);
        QCOMPARE(insertarPerfilFixture(prov, perfil, kRfcPerfil), QString());
        const SolicitudFixture terminada = fixture(perfil, DedupKey::calcularV1("m004-a"), QStringLiteral("Enviada"),
                                                   QStringLiteral("Terminada"));
        QCOMPARE(insertarSolicitudFixture(prov, terminada), QString());
        paqueteError = uuid::generarCanonico();
        QCOMPARE(insertarPaqueteFixture(prov, paqueteError, terminada.id, QStringLiteral("PAQ_1"),
                                        QStringLiteral("Error")),
                 QString());
        QCOMPARE(insertarPaqueteFixture(prov, uuid::generarCanonico(), terminada.id, QStringLiteral("PAQ_2"),
                                        QStringLiteral("Descargado")),
                 QString());
        QCOMPARE(insertarPaqueteFixture(prov, uuid::generarCanonico(), terminada.id, QStringLiteral("PAQ_3"),
                                        QStringLiteral("Disponible"), true),
                 QString());
        // Antes de 004 la columna no existe.
        QVERIFY(!ejecutarSql(prov, QStringLiteral("SELECT reintento_pendiente_en FROM paquete_solicitud")).isEmpty());
        huellaAntes = columna(prov, huellaSql);
        QCOMPARE(huellaAntes.size(), 3);
        prov.cerrarConexionDelHiloActual();
    }

    auto migrada = inicializarBaseSqlite(ruta);
    QVERIFY2(migrada, migrada ? "" : qPrintable(migrada.error().mensaje));
    QCOMPARE(migrada.valor().migracion.versionInicial, 3);
    QCOMPARE(migrada.valor().migracion.aplicadas, QList<int>{4});
    QCOMPARE(migrada.valor().migracion.versionFinal, embebidas.valor().last().version);

    SqliteConnectionProvider prov(ruta);
    QCOMPARE(columna(prov, huellaSql), huellaAntes); // filas previas intactas
    QCOMPARE(columna(prov, QStringLiteral("SELECT count(*) FROM paquete_solicitud "
                                          "WHERE reintento_pendiente_en IS NOT NULL")),
             QStringList{QStringLiteral("0")});
    QCOMPARE(columna(prov, QStringLiteral("SELECT tbl_name FROM sqlite_master "
                                          "WHERE name = 'ix_paquete_solicitud_reintento_pendiente'")),
             QStringList{QStringLiteral("paquete_solicitud")});
    // La columna se escribe y su CHECK rechaza texto vacio.
    QCOMPARE(ejecutarSql(prov, QStringLiteral("UPDATE paquete_solicitud SET reintento_pendiente_en = %1 WHERE id = %2")
                                   .arg(citar(kAhora), citar(paqueteError))),
             QString());
    QVERIFY(ejecutarSql(prov, QStringLiteral("UPDATE paquete_solicitud SET reintento_pendiente_en = '  ' WHERE id = %1")
                                  .arg(citar(paqueteError)))
                .contains(QStringLiteral("CHECK")));
    QVERIFY(consistente(prov));
    prov.cerrarConexionDelHiloActual();

    // Idempotente: una segunda apertura no aplica nada y conserva el dato.
    auto repetida = inicializarBaseSqlite(ruta);
    QVERIFY(repetida);
    QVERIFY(repetida.valor().migracion.aplicadas.isEmpty());
    SqliteConnectionProvider otra(ruta);
    QCOMPARE(columna(otra, QStringLiteral("SELECT reintento_pendiente_en FROM paquete_solicitud WHERE id = %1")
                               .arg(citar(paqueteError))),
             QStringList{kAhora});
    otra.cerrarConexionDelHiloActual();
}

// --- Transaccion requerida --------------------------------------------------------

void TestSqliteOperaciones::escriturasExigenTransaccion()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString perfil = e.perfil(kRfcPerfil);
    const QString s = e.solicitud(perfil, QStringLiteral("Creada"));
    QVERIFY(!s.isEmpty());
    auto& o = e.ops();
    const auto esTx = [](const auto& r) { return !r && r.error().tipo == ErrorPersistencia::Tipo::Transaccion; };
    QVERIFY(esTx(o.marcarEnviando(sid(s), en(0))));
    AplicacionEnvio envio;
    envio.solicitudId = sid(s);
    QVERIFY(esTx(o.aplicarEnvio(envio)));
    AplicacionVerificacion verificacion;
    verificacion.solicitudId = sid(s);
    verificacion.verificadaEn = en(0);
    QVERIFY(esTx(o.aplicarVerificacion(verificacion)));
    AplicacionFallaVerificacion falla;
    falla.solicitudId = sid(s);
    falla.claveFalla = QStringLiteral("Preparacion");
    QVERIFY(esTx(o.aplicarFallaVerificacion(falla)));
    QVERIFY(esTx(o.marcarDescargando(uuid::generarCanonico(), en(0), true)));
    AplicacionDescarga descarga;
    descarga.paqueteId = uuid::generarCanonico();
    QVERIFY(esTx(o.aplicarDescarga(descarga)));
    QVERIFY(esTx(o.vencerPaqueteEstimado(uuid::generarCanonico(), en(0))));
    QVERIFY(esTx(o.registrarIntencion(sid(s), TipoIntencion::Verificacion, en(0))));
    QVERIFY(esTx(o.consumirIntencion(sid(s), TipoIntencion::Verificacion, en(0))));
    QCOMPARE(e.s(QStringLiteral("estado_local"), s), QStringLiteral("Creada"));
}

// --- Seleccion ------------------------------------------------------------------

void TestSqliteOperaciones::perfilesConTrabajo()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(QStringLiteral("AAA010101AAA"));
    const QString b = e.perfil(QStringLiteral("BBB010101BBB"));
    const QString c = e.perfil(QStringLiteral("CCC010101CCC"));
    const QString d = e.perfil(QStringLiteral("DDD010101DDD"));
    const QString f = e.perfil(QStringLiteral("FFF010101FFF"));

    // A: verificacion debida. B: intencion (aun en Creada). C: descarga
    // automatica (Terminada + Disponible).
    QCOMPARE(e.set(QStringLiteral("solicitud_masiva"), e.solicitud(a, QStringLiteral("Enviada")),
                   QStringLiteral("siguiente_verificacion_en = %1").arg(citar(ts(-1)))),
             QString());
    QCOMPARE(e.set(QStringLiteral("solicitud_masiva"), e.solicitud(b, QStringLiteral("Creada")),
                   QStringLiteral("verificacion_pendiente = 1, accion_pendiente_en = %1").arg(citar(ts(0)))),
             QString());
    QVERIFY(!e.paquete(e.solicitud(c, QStringLiteral("Enviada"), QStringLiteral("Terminada")),
                       QStringLiteral("Disponible"))
                 .isEmpty());
    // D: nada debido (futura, terminal, Error solo manual, Creada).
    QCOMPARE(e.set(QStringLiteral("solicitud_masiva"), e.solicitud(d, QStringLiteral("Enviada"), QStringLiteral("Aceptada")),
                   QStringLiteral("siguiente_verificacion_en = %1").arg(citar(ts(5)))),
             QString());
    QCOMPARE(e.set(QStringLiteral("solicitud_masiva"),
                   e.solicitud(d, QStringLiteral("Enviada"), QStringLiteral("Rechazada")),
                   QStringLiteral("siguiente_verificacion_en = %1").arg(citar(ts(-5)))),
             QString());
    QVERIFY(!e.paquete(e.solicitud(d, QStringLiteral("Enviada"), QStringLiteral("Terminada")), QStringLiteral("Error"))
                 .isEmpty());
    QVERIFY(!e.solicitud(d, QStringLiteral("Creada")).isEmpty());
    // F: todo eliminado (solicitud debida, intencion y paquete).
    const QString fDebida = e.solicitud(f, QStringLiteral("Enviada"), {}, true);
    QCOMPARE(e.set(QStringLiteral("solicitud_masiva"), fDebida,
                   QStringLiteral("siguiente_verificacion_en = %1, descarga_pendiente = 1, accion_pendiente_en = %1")
                       .arg(citar(ts(-1)))),
             QString());
    QVERIFY(!e.paquete(e.solicitud(f, QStringLiteral("Enviada"), QStringLiteral("Terminada")),
                       QStringLiteral("Disponible"), true)
                 .isEmpty());

    auto r = e.ops().listarPerfilesConTrabajo(en(0));
    QVERIFY(r);
    QStringList obtenidos;
    for (const PerfilId& p : r.valor()) {
        obtenidos.append(p.texto());
    }
    QCOMPARE(obtenidos, ordenados({a, b, c}));
}

void TestSqliteOperaciones::verificacionesDebidasAcotadas()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    const QString b = e.perfil(kRfcOtro);
    auto debida = [&](const QString& perfil, const QString& estadoSat, int minutos, bool eliminada = false) {
        const QString id = e.solicitud(perfil, QStringLiteral("Enviada"), estadoSat, eliminada);
        if (minutos != INT_MIN) {
            e.set(QStringLiteral("solicitud_masiva"), id,
                  QStringLiteral("siguiente_verificacion_en = %1").arg(citar(ts(minutos))));
        }
        return id;
    };
    const QString s1 = debida(a, {}, -10);
    const QString s2 = debida(a, QStringLiteral("Aceptada"), -5);
    const QString s3 = debida(a, QStringLiteral("EnProceso"), 0); // igual a ahora: incluida
    debida(a, QStringLiteral("EnProceso"), 1);                     // futura
    debida(a, QStringLiteral("Terminada"), -20);                   // terminal
    debida(a, QStringLiteral("Vencida"), -20);                     // terminal
    debida(a, {}, INT_MIN);                                         // NULL: suspendida o sin agenda
    debida(a, {}, -30, true);                                       // eliminada
    QVERIFY(!e.solicitud(a, QStringLiteral("Creada")).isEmpty());   // D4: nunca Creada
    const QString s8 = debida(b, {}, -40);

    auto ids = [](const QList<SolicitudPersistida>& l) {
        QStringList r;
        for (const SolicitudPersistida& s : l) {
            r.append(s.id.texto());
        }
        return r;
    };
    auto dos = e.ops().listarVerificacionesDebidas({pid(a)}, en(0), 2);
    QVERIFY(dos);
    QCOMPARE(ids(dos.valor()), (QStringList{s1, s2}));
    QCOMPARE(dos.valor().first().estadoLocal, EstadoLocal::Enviada);
    QCOMPARE(*dos.valor().first().siguienteVerificacionEn, en(-10));
    auto todas = e.ops().listarVerificacionesDebidas({pid(a)}, en(0), 50);
    QVERIFY(todas);
    QCOMPARE(ids(todas.valor()), (QStringList{s1, s2, s3}));
    auto ambos = e.ops().listarVerificacionesDebidas({pid(a), pid(b)}, en(0), 50);
    QVERIFY(ambos);
    QCOMPARE(ids(ambos.valor()), (QStringList{s8, s1, s2, s3}));
    auto vacia = e.ops().listarVerificacionesDebidas({}, en(0), 50);
    QVERIFY(vacia && vacia.valor().isEmpty());
    auto cero = e.ops().listarVerificacionesDebidas({pid(a)}, en(0), 0);
    QVERIFY(!cero);
}

void TestSqliteOperaciones::intencionesAcotadas()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    const QString b = e.perfil(kRfcOtro);
    auto intencion = [&](const QString& perfil, const QString& banderas, int minutos, bool eliminada = false) {
        const QString id = e.solicitud(perfil, QStringLiteral("Enviada"), QStringLiteral("Terminada"), eliminada);
        e.set(QStringLiteral("solicitud_masiva"), id,
              banderas + QStringLiteral(", accion_pendiente_en = %1").arg(citar(ts(minutos))));
        return id;
    };
    const QString i1 = intencion(a, QStringLiteral("verificacion_pendiente = 1"), 2);
    const QString i2 = intencion(a, QStringLiteral("descarga_pendiente = 1"), 1);
    const QString i3 = intencion(a, QStringLiteral("verificacion_pendiente = 1, descarga_pendiente = 1"), 3);
    intencion(a, QStringLiteral("verificacion_pendiente = 1"), 0, true);
    QVERIFY(!e.solicitud(a, QStringLiteral("Enviada")).isEmpty()); // sin intencion
    const QString i5 = intencion(b, QStringLiteral("verificacion_pendiente = 1"), 0);

    auto dos = e.ops().listarIntencionesPendientes({pid(a)}, 2);
    QVERIFY(dos);
    QCOMPARE(dos.valor().size(), 2);
    QCOMPARE(dos.valor().at(0).solicitudId.texto(), i2);
    QVERIFY(!dos.valor().at(0).verificacionPendiente && dos.valor().at(0).descargaPendiente);
    QCOMPARE(dos.valor().at(0).accionPendienteEn, en(1));
    QCOMPARE(dos.valor().at(0).perfilSatId.texto(), a);
    QCOMPARE(dos.valor().at(1).solicitudId.texto(), i1);
    QVERIFY(dos.valor().at(1).verificacionPendiente && !dos.valor().at(1).descargaPendiente);

    auto ambos = e.ops().listarIntencionesPendientes({pid(a), pid(b)}, 50);
    QVERIFY(ambos);
    QStringList ids;
    for (const IntencionPendiente& i : ambos.valor()) {
        ids.append(i.solicitudId.texto());
    }
    QCOMPARE(ids, (QStringList{i5, i2, i1, i3}));
    QVERIFY(ambos.valor().at(3).verificacionPendiente && ambos.valor().at(3).descargaPendiente);
    QVERIFY(e.ops().listarIntencionesPendientes({}, 5).valor().isEmpty());
    QVERIFY(!e.ops().listarIntencionesPendientes({pid(a)}, -1));
}

void TestSqliteOperaciones::descargasAutomaticasAcotadas()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    const QString b = e.perfil(kRfcOtro);
    const QString terminada = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    auto paquete = [&](const QString& sol, const QString& estado, int disponible, bool eliminado = false) {
        const QString id = e.paquete(sol, estado, eliminado);
        e.set(QStringLiteral("paquete_solicitud"), id,
              QStringLiteral("disponible_en = %1").arg(citar(ts(disponible))));
        return id;
    };
    const QString p1 = paquete(terminada, QStringLiteral("Disponible"), 2);
    const QString p2 = paquete(terminada, QStringLiteral("Disponible"), 1);
    paquete(terminada, QStringLiteral("Error"), 0);       // solo manual
    paquete(terminada, QStringLiteral("Descargado"), 0);
    paquete(terminada, QStringLiteral("Descargando"), 0);
    paquete(terminada, QStringLiteral("Disponible"), 0, true);
    paquete(e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("EnProceso")), QStringLiteral("Disponible"), 0);
    paquete(e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"), true), QStringLiteral("Disponible"),
            0);
    const QString p8 = paquete(e.solicitud(b, QStringLiteral("Enviada"), QStringLiteral("Terminada")),
                               QStringLiteral("Disponible"), 5);

    auto uno = e.ops().listarDescargasAutomaticas({pid(a)}, 1);
    QVERIFY(uno);
    QCOMPARE(idsPaquetes(uno.valor()), QStringList{p2});
    const PaqueteDescargable& d = uno.valor().first();
    QCOMPARE(d.perfilSatId.texto(), a);
    QCOMPARE(d.rfcSolicitante, kRfcPerfil);
    QCOMPARE(d.idSolicitudSat, terminada.toUpper());
    QCOMPARE(d.paquete.solicitudMasivaId.texto(), terminada);
    QCOMPARE(d.paquete.estadoDescarga, EstadoDescarga::Disponible);
    QCOMPARE(d.paquete.disponibleEn, en(1));
    auto todas = e.ops().listarDescargasAutomaticas({pid(a)}, 50);
    QVERIFY(todas);
    QCOMPARE(idsPaquetes(todas.valor()), (QStringList{p2, p1}));
    auto ambos = e.ops().listarDescargasAutomaticas({pid(b), pid(a)}, 50);
    QVERIFY(ambos);
    QCOMPARE(idsPaquetes(ambos.valor()), (QStringList{p2, p1, p8}));
    QVERIFY(e.ops().listarDescargasAutomaticas({}, 5).valor().isEmpty());
    QVERIFY(!e.ops().listarDescargasAutomaticas({pid(a)}, 0));
}

void TestSqliteOperaciones::vencimientosEstimadosAcotados()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    const QString sol = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    auto paquete = [&](const QString& s, const QString& estado, std::optional<int> venc, bool eliminado = false) {
        const QString id = e.paquete(s, estado, eliminado);
        if (venc) {
            e.set(QStringLiteral("paquete_solicitud"), id,
                  QStringLiteral("vencimiento_estimado_en = %1").arg(citar(ts(*venc))));
        }
        return id;
    };
    const QString v1 = paquete(sol, QStringLiteral("Disponible"), -1);
    const QString v2 = paquete(sol, QStringLiteral("Error"), -3);
    const QString v3 = paquete(sol, QStringLiteral("Descargando"), -2);
    paquete(sol, QStringLiteral("Descargado"), -10);
    paquete(sol, QStringLiteral("Disponible"), 1);
    paquete(sol, QStringLiteral("Disponible"), std::nullopt);
    paquete(sol, QStringLiteral("Disponible"), -5, true);
    paquete(e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"), true), QStringLiteral("Disponible"),
            -6);

    auto dos = e.ops().listarVencimientosEstimados(en(0), 2);
    QVERIFY(dos);
    QCOMPARE(idsPaquetes(dos.valor()), (QStringList{v2, v3}));
    QCOMPARE(*dos.valor().first().paquete.vencimientoEstimadoEn, en(-3));
    auto todas = e.ops().listarVencimientosEstimados(en(0), 50);
    QVERIFY(todas);
    QCOMPARE(idsPaquetes(todas.valor()), (QStringList{v2, v3, v1}));
    QVERIFY(!e.ops().listarVencimientosEstimados(en(0), 0));
}

void TestSqliteOperaciones::paquetesPorIdYReintentables()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    const QString sol = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    const QString disponible = e.paquete(sol, QStringLiteral("Disponible"));
    const QString error = e.paquete(sol, QStringLiteral("Error"));
    const QString descargado = e.paquete(sol, QStringLiteral("Descargado"));
    const QString eliminado = e.paquete(sol, QStringLiteral("Error"), true);
    QCOMPARE(e.set(QStringLiteral("paquete_solicitud"), error, QStringLiteral("disponible_en = %1").arg(citar(ts(-1)))),
             QString());
    const QString deBorrada =
        e.paquete(e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"), true), QStringLiteral("Disponible"));

    auto obtenido = e.ops().obtenerPaquete(descargado);
    QVERIFY(obtenido && obtenido.valor());
    QCOMPARE(obtenido.valor()->paquete.estadoDescarga, EstadoDescarga::Descargado);
    QCOMPARE(obtenido.valor()->paquete.rutaLocal, std::optional<QString>(QStringLiteral("paquetes/p.zip")));
    QCOMPARE(obtenido.valor()->idSolicitudSat, sol.toUpper());
    QCOMPARE(obtenido.valor()->perfilSatId.texto(), a);
    for (const QString& invisible : {eliminado, deBorrada, uuid::generarCanonico()}) {
        auto r = e.ops().obtenerPaquete(invisible);
        QVERIFY(r);
        QVERIFY(!r.valor());
    }

    auto reintentables = e.ops().listarPaquetesReintentables(sid(sol));
    QVERIFY(reintentables);
    QCOMPARE(idsPaquetes(reintentables.valor()), (QStringList{error, disponible}));
    auto deEliminada = e.ops().listarPaquetesReintentables(
        sid(columna(e.prov(), QStringLiteral("SELECT solicitud_masiva_id FROM paquete_solicitud WHERE id = %1")
                                  .arg(citar(deBorrada)))
                .value(0)));
    QVERIFY(deEliminada && deEliminada.valor().isEmpty());
}

void TestSqliteOperaciones::interrumpidosYRacha()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    const QString enviando = e.solicitud(a, QStringLiteral("Enviando"));
    e.solicitud(a, QStringLiteral("Enviando"), {}, true);
    e.solicitud(a, QStringLiteral("Creada"));
    const QString terminada = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    const QString descargando = e.paquete(terminada, QStringLiteral("Descargando"));
    e.paquete(terminada, QStringLiteral("Descargando"), true);
    e.paquete(e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"), true), QStringLiteral("Descargando"));

    auto t = e.ops().listarInterrumpidos();
    QVERIFY(t);
    QCOMPARE(t.valor().solicitudesEnviando.size(), 1);
    QCOMPARE(t.valor().solicitudesEnviando.first().texto(), enviando);
    QCOMPARE(idsPaquetes(t.valor().paquetesDescargando), QStringList{descargando});

    QCOMPARE(e.set(QStringLiteral("solicitud_masiva"), terminada,
                   QStringLiteral("verificaciones_sin_cambio = 2, ultima_clave_falla_verificacion = "
                                  "'RespuestaExplicita:300', fallas_verificacion_iguales = 2")),
             QString());
    auto racha = e.ops().leerRachaVerificacion(sid(terminada));
    QVERIFY(racha && racha.valor());
    QCOMPARE(racha.valor()->verificacionesSinCambio, 2);
    QCOMPARE(racha.valor()->ultimaClaveFalla, std::optional<QString>(QStringLiteral("RespuestaExplicita:300")));
    QCOMPARE(racha.valor()->fallasIguales, 2);
    auto limpia = e.ops().leerRachaVerificacion(sid(enviando));
    QVERIFY(limpia && limpia.valor());
    QCOMPARE(limpia.valor()->verificacionesSinCambio, 0);
    QVERIFY(!limpia.valor()->ultimaClaveFalla);
    QCOMPARE(limpia.valor()->fallasIguales, 0);
    QCOMPARE(e.set(QStringLiteral("solicitud_masiva"), terminada,
                   QStringLiteral("eliminado_en = %1").arg(citar(kAhora))),
             QString());
    auto eliminada = e.ops().leerRachaVerificacion(sid(terminada));
    QVERIFY(eliminada && !eliminada.valor());
    auto inexistente = e.ops().leerRachaVerificacion(SolicitudId::generar());
    QVERIFY(inexistente && !inexistente.valor());
}

void TestSqliteOperaciones::planDeConsultaUsaIndices()
{
    Entorno e;
    QVERIFY(e.preparar());
    using namespace satcfdi::sqlite::operaciones;
    // Cada seleccion se apoya en un indice existente de 001; ninguna recorre
    // completas solicitud_masiva o paquete_solicitud. Sin ANALYZE, para
    // intenciones y vencimientos estimados SQLite puede elegir el indice por
    // perfil o por estado_descarga (busquedas acotadas) en lugar del parcial
    // especifico; ambos se aceptan. No se propone indice nuevo: el volumen
    // esperado (~10,000 CFDI, cientos de solicitudes y paquetes) lo cubre.
    struct Caso {
        const char* nombre;
        QString sql;
        QStringList indices;
    };
    const QList<Caso> casos = {
        {"perfiles_con_trabajo", sqlPerfilesConTrabajo(),
         {QStringLiteral("ix_solicitud_masiva_monitoreo"), QStringLiteral("ix_solicitud_masiva_accion_pendiente"),
          QStringLiteral("ix_paquete_solicitud_descarga")}},
        {"verificaciones_debidas", sqlVerificacionesDebidas(), {QStringLiteral("ix_solicitud_masiva_monitoreo")}},
        // Sin ANALYZE el planificador puede preferir buscar por perfil
        // (perfil_sat_id=?) y filtrar; ambas son busquedas indexadas.
        {"intenciones", sqlIntenciones(),
         {QStringLiteral("ix_solicitud_masiva_accion_pendiente|ix_solicitud_masiva_perfil")}},
        {"vencimientos_estimados", sqlVencimientosEstimados(),
         {QStringLiteral("ix_paquete_solicitud_vencimiento_estimado|ix_paquete_solicitud_descarga")}},
        {"descargas_automaticas", sqlDescargasAutomaticas(), {QStringLiteral("ix_paquete_solicitud_descarga")}},
        {"obtener_paquete", sqlObtenerPaquete(), {QStringLiteral("sqlite_autoindex_paquete_solicitud_1")}},
        {"reintentables", sqlPaquetesReintentables(), {QStringLiteral("_paquete_solicitud_")}},
        {"enviando", sqlSolicitudesEnviando(), {QStringLiteral("ix_solicitud_masiva_monitoreo")}},
        {"descargando", sqlPaquetesDescargando(), {QStringLiteral("ix_paquete_solicitud_descarga")}},
    };
    auto conexion = e.prov().conexion();
    QVERIFY(conexion);
    QSqlDatabase db = conexion.valor();
    for (const Caso& caso : casos) {
        QSqlQuery q(db);
        QVERIFY2(q.prepare(QStringLiteral("EXPLAIN QUERY PLAN ") + caso.sql), caso.nombre);
        const QList<std::pair<QString, QVariant>> parametros = {
            {QStringLiteral(":ahora"), ts(0)},
            {QStringLiteral(":limite"), 10},
            {QStringLiteral(":perfiles"), QStringLiteral("[]")},
            {QStringLiteral(":id"), uuid::generarCanonico()},
        };
        for (const auto& [nombre, valor] : parametros) {
            if (caso.sql.contains(nombre)) {
                q.bindValue(nombre, valor);
            }
        }
        QVERIFY2(q.exec(), caso.nombre);
        QStringList plan;
        while (q.next()) {
            plan.append(q.value(3).toString());
        }
        const QString texto = plan.join(QStringLiteral(" / "));
        for (const QString& alternativas : caso.indices) { // "a|b": basta una
            const QStringList opciones = alternativas.split(QLatin1Char('|'));
            const bool usado = std::any_of(opciones.cbegin(), opciones.cend(),
                                           [&](const QString& indice) { return texto.contains(indice); });
            QVERIFY2(usado, qPrintable(QString::fromLatin1(caso.nombre) + QStringLiteral(": ") + texto));
        }
        for (const QString& paso : plan) {
            // Recorrido completo de una tabla real (con o sin alias), sin indice.
            static const QRegularExpression kRecorrido(
                QStringLiteral("^SCAN (solicitud_masiva|paquete_solicitud|s|p)$"));
            const bool recorridoCompleto = kRecorrido.match(paso).hasMatch();
            QVERIFY2(!recorridoCompleto,
                     qPrintable(QString::fromLatin1(caso.nombre) + QStringLiteral(": ") + texto));
        }
    }
}

// --- Envio ------------------------------------------------------------------------

void TestSqliteOperaciones::envioTransiciones()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    auto& o = e.ops();

    // Creada -> Enviando -> Enviada.
    const QString s1 = e.solicitud(a, QStringLiteral("Creada"));
    auto marcado = e.tx([&] { return o.marcarEnviando(sid(s1), en(0)); });
    QVERIFY(marcado && marcado.valor());
    QCOMPARE(e.s(QStringLiteral("estado_local"), s1), QStringLiteral("Enviando"));
    QCOMPARE(e.s(QStringLiteral("envio_iniciado_en"), s1), ts(0));
    QVERIFY(consistente(e.prov()));
    AplicacionEnvio enviada;
    enviada.solicitudId = sid(s1);
    enviada.destino = EstadoLocal::Enviada;
    enviada.idSolicitudSat = QStringLiteral("4E3A-ID-SAT");
    enviada.codEstatus = QStringLiteral("5000");
    enviada.mensaje = QStringLiteral("Solicitud Aceptada");
    enviada.enviadaEn = en(1);
    enviada.siguienteVerificacionEn = en(11);
    auto r1 = e.tx([&] { return o.aplicarEnvio(enviada); });
    QVERIFY(r1 && r1.valor());
    QCOMPARE(e.s(QStringLiteral("estado_local || '|' || id_solicitud_sat || '|' || cod_estatus_solicitud || '|' || "
                                "mensaje_solicitud_sat || '|' || enviada_en || '|' || siguiente_verificacion_en"),
                 s1),
             QStringLiteral("Enviada|4E3A-ID-SAT|5000|Solicitud Aceptada|%1|%2").arg(ts(1), ts(11)));
    QCOMPARE(e.s(QStringLiteral("envio_iniciado_en"), s1), ts(0));
    QCOMPARE(e.s(QStringLiteral("ultimo_error"), s1), kNulo);
    QVERIFY(consistente(e.prov()));

    // Enviando -> Creada (D7, ADR 0017): limpia intento, codigo y mensaje.
    const QString s2 = e.solicitud(a, QStringLiteral("Creada"));
    QVERIFY(e.tx([&] { return o.marcarEnviando(sid(s2), en(2)); }).valor());
    AplicacionEnvio creada;
    creada.solicitudId = sid(s2);
    creada.destino = EstadoLocal::Creada;
    creada.ultimoError = QStringLiteral("Preparacion: credencial no lista");
    creada.codEstatus = QStringLiteral("ignorado");
    creada.mensaje = QStringLiteral("ignorado");
    auto r2 = e.tx([&] { return o.aplicarEnvio(creada); });
    QVERIFY(r2 && r2.valor());
    QCOMPARE(e.s(QStringLiteral("estado_local"), s2), QStringLiteral("Creada"));
    QCOMPARE(e.s(QStringLiteral("envio_iniciado_en"), s2), kNulo);
    QCOMPARE(e.s(QStringLiteral("cod_estatus_solicitud"), s2), kNulo);
    QCOMPARE(e.s(QStringLiteral("mensaje_solicitud_sat"), s2), kNulo);
    QCOMPARE(e.s(QStringLiteral("ultimo_error"), s2), QStringLiteral("Preparacion: credencial no lista"));
    QVERIFY(consistente(e.prov()));
    // Reenvio manual posible.
    QVERIFY(e.tx([&] { return o.marcarEnviando(sid(s2), en(3)); }).valor());
    QCOMPARE(e.s(QStringLiteral("envio_iniciado_en"), s2), ts(3));

    // Enviando -> EnvioFallido y EnvioIncierto: conservan envio_iniciado_en.
    const QString s3 = e.solicitud(a, QStringLiteral("Enviando"));
    AplicacionEnvio fallido;
    fallido.solicitudId = sid(s3);
    fallido.destino = EstadoLocal::EnvioFallido;
    fallido.codEstatus = QStringLiteral("301");
    fallido.mensaje = QStringLiteral("XML Mal Formado");
    fallido.ultimoError = QStringLiteral("RespuestaExplicita:301");
    QVERIFY(e.tx([&] { return o.aplicarEnvio(fallido); }).valor());
    QCOMPARE(e.s(QStringLiteral("estado_local || '|' || cod_estatus_solicitud || '|' || mensaje_solicitud_sat || '|' "
                                "|| envio_iniciado_en || '|' || ultimo_error"),
                 s3),
             QStringLiteral("EnvioFallido|301|XML Mal Formado|%1|RespuestaExplicita:301").arg(kAhora));
    const QString s4 = e.solicitud(a, QStringLiteral("Enviando"));
    AplicacionEnvio incierto;
    incierto.solicitudId = sid(s4);
    incierto.destino = EstadoLocal::EnvioIncierto;
    incierto.ultimoError = QStringLiteral("DespuesDeEnvio");
    QVERIFY(e.tx([&] { return o.aplicarEnvio(incierto); }).valor());
    QCOMPARE(e.s(QStringLiteral("estado_local"), s4), QStringLiteral("EnvioIncierto"));
    QCOMPARE(e.s(QStringLiteral("cod_estatus_solicitud"), s4), kNulo);
    QCOMPARE(e.s(QStringLiteral("envio_iniciado_en"), s4), kAhora);
    QVERIFY(consistente(e.prov()));

    // Invariantes del destino validados antes de escribir.
    const QString s5 = e.solicitud(a, QStringLiteral("Enviando"));
    const QString antes = fila(e.prov(), QStringLiteral("solicitud_masiva"), s5);
    AplicacionEnvio incompleta = enviada;
    incompleta.solicitudId = sid(s5);
    incompleta.idSolicitudSat.reset();
    QVERIFY(!e.tx([&] { return o.aplicarEnvio(incompleta); }));
    AplicacionEnvio aEnviando;
    aEnviando.solicitudId = sid(s5);
    aEnviando.destino = EstadoLocal::Enviando;
    QVERIFY(!e.tx([&] { return o.aplicarEnvio(aEnviando); }));
    QCOMPARE(fila(e.prov(), QStringLiteral("solicitud_masiva"), s5), antes);
}

void TestSqliteOperaciones::envioNoAplicaPorEstadoOEliminacion()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    auto& o = e.ops();
    const QString enviada = e.solicitud(a, QStringLiteral("Enviada"));
    const QString creadaBorrada = e.solicitud(a, QStringLiteral("Creada"), {}, true);
    const QString creada = e.solicitud(a, QStringLiteral("Creada"));
    const QString enviandoBorrada = e.solicitud(a, QStringLiteral("Enviando"), {}, true);
    const QString incierta = e.solicitud(a, QStringLiteral("EnvioIncierto"));
    QStringList antes;
    for (const QString& id : {enviada, creadaBorrada, creada, enviandoBorrada, incierta}) {
        antes.append(fila(e.prov(), QStringLiteral("solicitud_masiva"), id));
    }

    for (const QString& id : {enviada, creadaBorrada, enviandoBorrada, incierta}) {
        auto r = e.tx([&] { return o.marcarEnviando(sid(id), en(5)); });
        QVERIFY(r);
        QVERIFY(!r.valor());
    }
    AplicacionEnvio incierto;
    incierto.destino = EstadoLocal::EnvioIncierto;
    for (const QString& id : {creada, enviandoBorrada, incierta, enviada}) {
        incierto.solicitudId = sid(id);
        auto r = e.tx([&] { return o.aplicarEnvio(incierto); });
        QVERIFY(r);
        QVERIFY(!r.valor());
    }
    QStringList despues;
    for (const QString& id : {enviada, creadaBorrada, creada, enviandoBorrada, incierta}) {
        despues.append(fila(e.prov(), QStringLiteral("solicitud_masiva"), id));
    }
    QCOMPARE(despues, antes);
}

// --- Verificacion ------------------------------------------------------------------

void TestSqliteOperaciones::verificacionRegistraPaquetesSinDuplicar()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    auto& o = e.ops();
    const QString s = e.solicitud(a, QStringLiteral("Enviada"));
    QCOMPARE(e.set(QStringLiteral("solicitud_masiva"), s,
                   QStringLiteral("ultimo_error = 'x', ultima_clave_falla_verificacion = 'RespuestaExplicita:5004', "
                                  "fallas_verificacion_iguales = 2")),
             QString());
    const QString existente = uuid::generarCanonico();
    QCOMPARE(insertarPaqueteFixture(e.prov(), existente, s, QStringLiteral("PAQ_01"), QStringLiteral("Disponible")),
             QString());
    QCOMPARE(e.set(QStringLiteral("paquete_solicitud"), existente,
                   QStringLiteral("vencimiento_estimado_en = %1").arg(citar(ts(100)))),
             QString());

    const QDateTime vence = en(5).addSecs(72 * 3600);
    auto nuevo = [&](const QString& idSat) {
        return PaqueteNuevo{uuid::generarCanonico(), idSat, en(5), vence};
    };
    AplicacionVerificacion v;
    v.solicitudId = sid(s);
    v.verificadaEn = en(5);
    v.estadoSolicitudSat = EstadoSolicitudSat::Terminada;
    v.codigoEstadoSolicitud = QStringLiteral("5000");
    v.mensajeVerificacion = QStringLiteral("Solicitud Aceptada");
    v.numeroCfdi = 12;
    v.siguienteVerificacionEn = std::nullopt;
    v.verificacionesSinCambio = 0;
    const PaqueteNuevo repetido = nuevo(QStringLiteral("PAQ_01"));
    const PaqueteNuevo n2 = nuevo(QStringLiteral("PAQ_02"));
    const PaqueteNuevo n3 = nuevo(QStringLiteral("PAQ_03"));
    const PaqueteNuevo n2bis = nuevo(QStringLiteral("PAQ_02")); // repetido en la misma respuesta
    v.paquetesNuevos = {repetido, n2, n3, n2bis};

    auto r = e.tx([&] { return o.aplicarVerificacion(v); });
    QVERIFY2(r, r ? "" : qPrintable(r.error().mensaje));
    QVERIFY(r.valor().aplicada);
    QCOMPARE(r.valor().paquetesInsertados, (QList<QString>{n2.id, n3.id}));
    QVERIFY(r.valor().paquetesVencidos.isEmpty());
    QCOMPARE(e.s(QStringLiteral("estado_solicitud_sat || '|' || codigo_estado_solicitud || '|' || "
                                "mensaje_verificacion_sat || '|' || numero_cfdi || '|' || ultima_verificacion_en || '|' "
                                "|| verificaciones_sin_cambio || '|' || fallas_verificacion_iguales"),
                 s),
             QStringLiteral("Terminada|5000|Solicitud Aceptada|12|%1|0|0").arg(ts(5)));
    QCOMPARE(e.s(QStringLiteral("siguiente_verificacion_en"), s), kNulo);
    QCOMPARE(e.s(QStringLiteral("ultimo_error"), s), kNulo);
    QCOMPARE(e.s(QStringLiteral("ultima_clave_falla_verificacion"), s), kNulo);
    // D10: el existente conserva id y vencimiento; los nuevos nacen Disponible.
    QCOMPARE(e.pq(QStringLiteral("vencimiento_estimado_en"), existente), ts(100));
    QCOMPARE(columna(e.prov(), QStringLiteral("SELECT id_paquete_sat || '|' || estado_descarga || '|' || disponible_en "
                                              "|| '|' || vencimiento_estimado_en FROM paquete_solicitud WHERE "
                                              "solicitud_masiva_id = %1 AND id <> %2 ORDER BY id_paquete_sat")
                                  .arg(citar(s), citar(existente))),
             (QStringList{QStringLiteral("PAQ_02|Disponible|%1|%2").arg(ts(5), timestamp::aTexto(vence)),
                          QStringLiteral("PAQ_03|Disponible|%1|%2").arg(ts(5), timestamp::aTexto(vence))}));
    QVERIFY(consistente(e.prov()));

    // Repetir la misma observacion no duplica ni desplaza vencimientos.
    v.verificadaEn = en(9);
    auto otra = e.tx([&] { return o.aplicarVerificacion(v); });
    QVERIFY(otra && otra.valor().aplicada);
    QVERIFY(otra.valor().paquetesInsertados.isEmpty());
    QCOMPARE(escalar(e.prov(), QStringLiteral("SELECT count(*) FROM paquete_solicitud WHERE solicitud_masiva_id = %1")
                                   .arg(citar(s)))
                 .toInt(),
             3);
    QCOMPARE(e.pq(QStringLiteral("vencimiento_estimado_en"), n2.id), timestamp::aTexto(vence));

    // Verificacion no terminal: agenda y contador.
    const QString s2 = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Aceptada"));
    AplicacionVerificacion enProceso;
    enProceso.solicitudId = sid(s2);
    enProceso.verificadaEn = en(20);
    enProceso.estadoSolicitudSat = EstadoSolicitudSat::EnProceso;
    enProceso.siguienteVerificacionEn = en(50);
    enProceso.verificacionesSinCambio = 3;
    QVERIFY(e.tx([&] { return o.aplicarVerificacion(enProceso); }).valor().aplicada);
    QCOMPARE(e.s(QStringLiteral("estado_solicitud_sat || '|' || siguiente_verificacion_en || '|' || "
                                "verificaciones_sin_cambio || '|' || (numero_cfdi IS NULL)"),
                 s2),
             QStringLiteral("EnProceso|%1|3|1").arg(ts(50)));
    QVERIFY(consistente(e.prov()));
}

void TestSqliteOperaciones::verificacionVencidaVenceNoDescargados()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    const QString s = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    const QString disponible = e.paquete(s, QStringLiteral("Disponible"));
    const QString error = e.paquete(s, QStringLiteral("Error"));
    const QString descargando = e.paquete(s, QStringLiteral("Descargando"));
    const QString descargado = e.paquete(s, QStringLiteral("Descargado"));
    const QString vencido = e.paquete(s, QStringLiteral("Vencido"));
    const QString eliminado = e.paquete(s, QStringLiteral("Disponible"), true);

    AplicacionVerificacion v;
    v.solicitudId = sid(s);
    v.verificadaEn = en(9);
    v.estadoSolicitudSat = EstadoSolicitudSat::Vencida;
    v.codigoEstadoSolicitud = QStringLiteral("5000");
    v.vencerNoDescargados = true;
    auto r = e.tx([&] { return e.ops().aplicarVerificacion(v); });
    QVERIFY(r && r.valor().aplicada);
    QCOMPARE(ordenados(r.valor().paquetesVencidos), ordenados({disponible, error, descargando}));
    for (const QString& id : {disponible, error, descargando}) {
        QCOMPARE(e.pq(QStringLiteral("estado_descarga || '|' || motivo_vencimiento || '|' || origen_vencimiento || '|' "
                                     "|| vencido_en"),
                      id),
                 QStringLiteral("Vencido|solicitud_expirada|SAT|%1").arg(ts(9)));
    }
    QCOMPARE(e.pq(QStringLiteral("estado_descarga"), descargado), QStringLiteral("Descargado"));
    QCOMPARE(e.pq(QStringLiteral("motivo_vencimiento"), vencido), QStringLiteral("paquete_expirado"));
    QCOMPARE(e.pq(QStringLiteral("estado_descarga"), eliminado), QStringLiteral("Disponible"));
    QCOMPARE(e.s(QStringLiteral("estado_solicitud_sat"), s), QStringLiteral("Vencida"));
    QVERIFY(consistente(e.prov()));
}

void TestSqliteOperaciones::verificacionNoAplicaYRevierte()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    auto& o = e.ops();
    AplicacionVerificacion v;
    v.verificadaEn = en(5);
    v.estadoSolicitudSat = EstadoSolicitudSat::Terminada;
    v.paquetesNuevos = {PaqueteNuevo{uuid::generarCanonico(), QStringLiteral("PAQ_X"), en(5), en(5)}};

    // Eliminada, Creada o EnvioIncierto: no aplica y no inserta paquetes.
    for (const QString& id : {e.solicitud(a, QStringLiteral("Enviada"), {}, true), e.solicitud(a, QStringLiteral("Creada")),
                              e.solicitud(a, QStringLiteral("EnvioIncierto"))}) {
        const QString antes = fila(e.prov(), QStringLiteral("solicitud_masiva"), id);
        v.solicitudId = sid(id);
        auto r = e.tx([&] { return o.aplicarVerificacion(v); });
        QVERIFY(r);
        QVERIFY(!r.valor().aplicada);
        QVERIFY(r.valor().paquetesInsertados.isEmpty());
        QCOMPARE(fila(e.prov(), QStringLiteral("solicitud_masiva"), id), antes);
    }
    QCOMPARE(escalar(e.prov(), QStringLiteral("SELECT count(*) FROM paquete_solicitud")).toInt(), 0);

    // Atomicidad: un paquete invalido (CHECK) revierte TODO, incluido el
    // estado SAT ya escrito en la misma transaccion.
    const QString s = e.solicitud(a, QStringLiteral("Enviada"));
    const QString antes = fila(e.prov(), QStringLiteral("solicitud_masiva"), s);
    v.solicitudId = sid(s);
    v.paquetesNuevos = {PaqueteNuevo{uuid::generarCanonico(), QStringLiteral("PAQ_OK"), en(5), en(5)},
                        PaqueteNuevo{uuid::generarCanonico(), QStringLiteral("   "), en(5), en(5)}};
    auto r = e.tx([&] { return o.aplicarVerificacion(v); });
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QCOMPARE(fila(e.prov(), QStringLiteral("solicitud_masiva"), s), antes);
    QCOMPARE(escalar(e.prov(), QStringLiteral("SELECT count(*) FROM paquete_solicitud")).toInt(), 0);
    QVERIFY(!e.prov().transaccionActiva());
    QVERIFY(consistente(e.prov()));
}

void TestSqliteOperaciones::fallaVerificacionRacha()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    auto& o = e.ops();
    const QString s = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Aceptada"));
    QCOMPARE(e.set(QStringLiteral("solicitud_masiva"), s,
                   QStringLiteral("verificaciones_sin_cambio = 2, siguiente_verificacion_en = %1").arg(citar(ts(10)))),
             QString());

    AplicacionFallaVerificacion f;
    f.solicitudId = sid(s);
    f.falladaEn = en(10);
    f.ultimoError = QStringLiteral("RespuestaExplicita:300 Usuario no valido");
    f.claveFalla = QStringLiteral("RespuestaExplicita:300");
    f.fallasIguales = 1;
    f.siguienteVerificacionEn = en(40);
    QVERIFY(e.tx([&] { return o.aplicarFallaVerificacion(f); }).valor());
    QCOMPARE(e.s(QStringLiteral("ultima_clave_falla_verificacion || '|' || fallas_verificacion_iguales || '|' || "
                                "siguiente_verificacion_en || '|' || ultimo_error || '|' || estado_solicitud_sat || '|' "
                                "|| verificaciones_sin_cambio || '|' || ultima_verificacion_en"),
                 s),
             QStringLiteral("RespuestaExplicita:300|1|%1|RespuestaExplicita:300 Usuario no valido|Aceptada|2|%2")
                 .arg(ts(40), kAhora));
    QVERIFY(consistente(e.prov()));

    // Tercera falla igual: suspendida (siguiente NULL), fuera de la agenda.
    f.fallasIguales = 3;
    f.siguienteVerificacionEn = std::nullopt;
    QVERIFY(e.tx([&] { return o.aplicarFallaVerificacion(f); }).valor());
    QCOMPARE(e.s(QStringLiteral("siguiente_verificacion_en"), s), kNulo);
    QCOMPARE(e.s(QStringLiteral("fallas_verificacion_iguales"), s), QStringLiteral("3"));
    QVERIFY(e.ops().listarVerificacionesDebidas({pid(a)}, en(1000), 10).valor().isEmpty());
    auto racha = o.leerRachaVerificacion(sid(s));
    QVERIFY(racha && racha.valor());
    QCOMPARE(racha.valor()->fallasIguales, 3);
    QVERIFY(consistente(e.prov()));

    // CHECKs de 003: contador y clave coherentes.
    const QString antes = fila(e.prov(), QStringLiteral("solicitud_masiva"), s);
    AplicacionFallaVerificacion cero = f;
    cero.fallasIguales = 0;
    auto rc = e.tx([&] { return o.aplicarFallaVerificacion(cero); });
    QVERIFY(!rc);
    QCOMPARE(rc.error().tipo, ErrorPersistencia::Tipo::Integridad);
    AplicacionFallaVerificacion sinClave = f;
    sinClave.claveFalla = QString();
    auto rv = e.tx([&] { return o.aplicarFallaVerificacion(sinClave); });
    QVERIFY(!rv);
    QCOMPARE(rv.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QCOMPARE(fila(e.prov(), QStringLiteral("solicitud_masiva"), s), antes);

    // No aplica: eliminada o no Enviada.
    for (const QString& id : {e.solicitud(a, QStringLiteral("Enviada"), {}, true), e.solicitud(a, QStringLiteral("Creada"))}) {
        const QString previo = fila(e.prov(), QStringLiteral("solicitud_masiva"), id);
        f.solicitudId = sid(id);
        auto r = e.tx([&] { return o.aplicarFallaVerificacion(f); });
        QVERIFY(r);
        QVERIFY(!r.valor());
        QCOMPARE(fila(e.prov(), QStringLiteral("solicitud_masiva"), id), previo);
    }
}

// --- Descarga ------------------------------------------------------------------------

void TestSqliteOperaciones::descargaReclamoYDesenlaces()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    auto& o = e.ops();
    const QString s = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"));

    // Disponible -> Descargando -> Descargado.
    const QString p1 = e.paquete(s, QStringLiteral("Disponible"));
    QVERIFY(e.tx([&] { return o.marcarDescargando(p1, en(1), false); }).valor());
    QCOMPARE(e.pq(QStringLiteral("estado_descarga || '|' || descarga_iniciada_en"), p1),
             QStringLiteral("Descargando|%1").arg(ts(1)));
    QVERIFY(consistente(e.prov()));
    AplicacionDescarga ok;
    ok.paqueteId = p1;
    ok.destino = EstadoDescarga::Descargado;
    ok.aplicadaEn = en(2);
    ok.rutaFinal = QStringLiteral("AAA010101AAA/2026/PAQ_01.zip");
    ok.codigoDescargaSat = QStringLiteral("5000");
    ok.mensajeDescargaSat = QStringLiteral("Solicitud Aceptada");
    QVERIFY(e.tx([&] { return o.aplicarDescarga(ok); }).valor());
    QCOMPARE(e.pq(QStringLiteral("estado_descarga || '|' || ruta_local || '|' || descargado_en || '|' || "
                                 "codigo_descarga_sat || '|' || (reconciliado_en IS NULL)"),
                  p1),
             QStringLiteral("Descargado|AAA010101AAA/2026/PAQ_01.zip|%1|5000|1").arg(ts(2)));
    QVERIFY(consistente(e.prov()));

    // Error solo se reclama con permitirError (accion manual); -> Error 5008.
    const QString p2 = e.paquete(s, QStringLiteral("Error"));
    auto sinPermiso = e.tx([&] { return o.marcarDescargando(p2, en(3), false); });
    QVERIFY(sinPermiso && !sinPermiso.valor());
    QCOMPARE(e.pq(QStringLiteral("estado_descarga"), p2), QStringLiteral("Error"));
    QVERIFY(e.tx([&] { return o.marcarDescargando(p2, en(3), true); }).valor());
    AplicacionDescarga error;
    error.paqueteId = p2;
    error.destino = EstadoDescarga::Error;
    error.aplicadaEn = en(4);
    error.codigoDescargaSat = QStringLiteral("5008");
    error.mensajeDescargaSat = QStringLiteral("Maximo de descargas permitidas");
    error.ultimoError = QStringLiteral("RespuestaExplicita:5008");
    QVERIFY(e.tx([&] { return o.aplicarDescarga(error); }).valor());
    QCOMPARE(e.pq(QStringLiteral("estado_descarga || '|' || codigo_descarga_sat || '|' || ultimo_error || '|' || "
                                 "(descargado_en IS NULL)"),
                  p2),
             QStringLiteral("Error|5008|RespuestaExplicita:5008|1"));
    QVERIFY(consistente(e.prov()));

    // 5007 -> Vencido (SAT, paquete_expirado).
    const QString p3 = e.paquete(s, QStringLiteral("Disponible"));
    QVERIFY(e.tx([&] { return o.marcarDescargando(p3, en(5), false); }).valor());
    AplicacionDescarga vencido;
    vencido.paqueteId = p3;
    vencido.destino = EstadoDescarga::Vencido;
    vencido.aplicadaEn = en(6);
    vencido.codigoDescargaSat = QStringLiteral("5007");
    vencido.motivoVencimiento = MotivoVencimiento::PaqueteExpirado;
    vencido.origenVencimiento = OrigenVencimiento::Sat;
    QVERIFY(e.tx([&] { return o.aplicarDescarga(vencido); }).valor());
    QCOMPARE(e.pq(QStringLiteral("estado_descarga || '|' || motivo_vencimiento || '|' || origen_vencimiento || '|' || "
                                 "vencido_en || '|' || codigo_descarga_sat"),
                  p3),
             QStringLiteral("Vencido|paquete_expirado|SAT|%1|5007").arg(ts(6)));
    QVERIFY(consistente(e.prov()));

    // Recuperacion: Descargando -> Disponible libera el reclamo.
    const QString p4 = e.paquete(s, QStringLiteral("Descargando"));
    AplicacionDescarga liberar;
    liberar.paqueteId = p4;
    liberar.destino = EstadoDescarga::Disponible;
    liberar.aplicadaEn = en(7);
    QVERIFY(e.tx([&] { return o.aplicarDescarga(liberar); }).valor());
    QCOMPARE(e.pq(QStringLiteral("estado_descarga || '|' || (descarga_iniciada_en IS NULL)"), p4),
             QStringLiteral("Disponible|1"));
    // Recuperacion: archivo final confirmado -> Descargado reconciliado.
    const QString p5 = e.paquete(s, QStringLiteral("Descargando"));
    AplicacionDescarga reconciliado = ok;
    reconciliado.paqueteId = p5;
    reconciliado.aplicadaEn = en(8);
    reconciliado.reconciliado = true;
    reconciliado.codigoDescargaSat.reset();
    reconciliado.mensajeDescargaSat.reset();
    QVERIFY(e.tx([&] { return o.aplicarDescarga(reconciliado); }).valor());
    QCOMPARE(e.pq(QStringLiteral("estado_descarga || '|' || reconciliado_en || '|' || descargado_en"), p5),
             QStringLiteral("Descargado|%1|%1").arg(ts(8)));
    QVERIFY(consistente(e.prov()));

    // Desenlaces invalidos: validados o rechazados por CHECK, sin cambios.
    const QString p6 = e.paquete(s, QStringLiteral("Descargando"));
    const QString antes = fila(e.prov(), QStringLiteral("paquete_solicitud"), p6);
    AplicacionDescarga sinRuta = ok;
    sinRuta.paqueteId = p6;
    sinRuta.rutaFinal.reset();
    QVERIFY(!e.tx([&] { return o.aplicarDescarga(sinRuta); }));
    AplicacionDescarga sinMotivo = vencido;
    sinMotivo.paqueteId = p6;
    sinMotivo.motivoVencimiento.reset();
    QVERIFY(!e.tx([&] { return o.aplicarDescarga(sinMotivo); }));
    AplicacionDescarga aDescargando = liberar;
    aDescargando.paqueteId = p6;
    aDescargando.destino = EstadoDescarga::Descargando;
    QVERIFY(!e.tx([&] { return o.aplicarDescarga(aDescargando); }));
    AplicacionDescarga incoherente = vencido;
    incoherente.paqueteId = p6;
    incoherente.origenVencimiento = OrigenVencimiento::EstimacionLocal;
    auto ri = e.tx([&] { return o.aplicarDescarga(incoherente); });
    QVERIFY(!ri);
    QCOMPARE(ri.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QCOMPARE(ri.error().restriccion, QStringLiteral("ck_paquete_motivo_origen"));
    QCOMPARE(fila(e.prov(), QStringLiteral("paquete_solicitud"), p6), antes);
    // La solicitud no cambia su estado SAT por descargar.
    QCOMPARE(e.s(QStringLiteral("estado_solicitud_sat"), s), QStringLiteral("Terminada"));
}

void TestSqliteOperaciones::descargaNoAplica()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    auto& o = e.ops();
    const QString s = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    const QString borrada = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"), true);
    const QStringList noReclamables = {
        e.paquete(s, QStringLiteral("Descargado")),  e.paquete(s, QStringLiteral("Vencido")),
        e.paquete(s, QStringLiteral("Descargando")), e.paquete(s, QStringLiteral("Disponible"), true),
        e.paquete(borrada, QStringLiteral("Disponible")), e.paquete(borrada, QStringLiteral("Error")),
    };
    const QStringList noAplicables = {
        e.paquete(s, QStringLiteral("Disponible")), e.paquete(s, QStringLiteral("Error")),
        e.paquete(s, QStringLiteral("Descargando"), true), e.paquete(borrada, QStringLiteral("Descargando")),
    };
    QStringList antes;
    for (const QString& id : noReclamables + noAplicables) {
        antes.append(fila(e.prov(), QStringLiteral("paquete_solicitud"), id));
    }
    for (const QString& id : noReclamables) {
        auto r = e.tx([&] { return o.marcarDescargando(id, en(1), true); });
        QVERIFY(r);
        QVERIFY2(!r.valor(), qPrintable(id));
    }
    AplicacionDescarga error;
    error.destino = EstadoDescarga::Error;
    error.aplicadaEn = en(2);
    error.ultimoError = QStringLiteral("x");
    for (const QString& id : noAplicables + QStringList{uuid::generarCanonico()}) {
        error.paqueteId = id;
        auto r = e.tx([&] { return o.aplicarDescarga(error); });
        QVERIFY(r);
        QVERIFY(!r.valor());
    }
    QStringList despues;
    for (const QString& id : noReclamables + noAplicables) {
        despues.append(fila(e.prov(), QStringLiteral("paquete_solicitud"), id));
    }
    QCOMPARE(despues, antes);
}

void TestSqliteOperaciones::vencimientoEstimado()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    auto& o = e.ops();
    const QString s = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    const QString borrada = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"), true);
    auto paquete = [&](const QString& sol, const QString& estado, std::optional<int> venc) {
        const QString id = e.paquete(sol, estado);
        if (venc) {
            e.set(QStringLiteral("paquete_solicitud"), id,
                  QStringLiteral("vencimiento_estimado_en = %1").arg(citar(ts(*venc))));
        }
        return id;
    };
    const QStringList vencen = {paquete(s, QStringLiteral("Disponible"), -1), paquete(s, QStringLiteral("Error"), 0),
                                paquete(s, QStringLiteral("Descargando"), -1)};
    const QStringList noVencen = {paquete(s, QStringLiteral("Disponible"), 1),
                                  paquete(s, QStringLiteral("Disponible"), std::nullopt),
                                  paquete(s, QStringLiteral("Descargado"), -1),
                                  paquete(s, QStringLiteral("Vencido"), -1),
                                  paquete(borrada, QStringLiteral("Disponible"), -1)};
    QStringList antes;
    for (const QString& id : noVencen) {
        antes.append(fila(e.prov(), QStringLiteral("paquete_solicitud"), id));
    }
    for (const QString& id : vencen) {
        QVERIFY(e.tx([&] { return o.vencerPaqueteEstimado(id, en(0)); }).valor());
        QCOMPARE(e.pq(QStringLiteral("estado_descarga || '|' || motivo_vencimiento || '|' || origen_vencimiento || '|' "
                                     "|| vencido_en"),
                      id),
                 QStringLiteral("Vencido|vencimiento_estimado|estimacion_local|%1").arg(ts(0)));
    }
    for (const QString& id : noVencen) {
        auto r = e.tx([&] { return o.vencerPaqueteEstimado(id, en(0)); });
        QVERIFY(r);
        QVERIFY(!r.valor());
    }
    QStringList despues;
    for (const QString& id : noVencen) {
        despues.append(fila(e.prov(), QStringLiteral("paquete_solicitud"), id));
    }
    QCOMPARE(despues, antes);
    QVERIFY(consistente(e.prov()));
}

// --- Intenciones por paquete (T014.2) ----------------------------------------------

void TestSqliteOperaciones::intencionesPorPaquete()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    const QString b = e.perfil(QStringLiteral("BBB010101BBB"));
    auto& o = e.ops();
    const QString s = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    const QString error = e.paquete(s, QStringLiteral("Error"));
    const QString maximo = e.paquete(s, QStringLiteral("Error"));
    QCOMPARE(e.set(QStringLiteral("paquete_solicitud"), maximo, QStringLiteral("codigo_descarga_sat = '5008'")),
             QString());
    const QString descargado = e.paquete(s, QStringLiteral("Descargado"));
    const QString eliminado = e.paquete(s, QStringLiteral("Error"), true);
    const QString otra = e.solicitud(b, QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    const QString deB = e.paquete(otra, QStringLiteral("Error"));
    auto pendiente = [&](const QString& id) { return e.pq(QStringLiteral("ifnull(reintento_pendiente_en, '-')"), id); };

    // Registro: solo Error/Disponible visibles sin 5008.
    QVERIFY(e.tx([&] { return o.registrarIntencionPaquete(error, en(2)); }).valor());
    QCOMPARE(pendiente(error), ts(2));
    for (const QString& id : {maximo, descargado, eliminado}) {
        auto r = e.tx([&] { return o.registrarIntencionPaquete(id, en(2)); });
        QVERIFY(r && !r.valor());
        QCOMPARE(pendiente(id), QStringLiteral("-"));
    }
    QVERIFY(e.tx([&] { return o.registrarIntencionPaquete(deB, en(1)); }).valor());

    // Perfiles con trabajo incluyen los de intenciones por paquete (B solo
    // tiene un paquete en Error con intencion).
    auto perfiles = o.listarPerfilesConTrabajo(en(0));
    QVERIFY(perfiles);
    QVERIFY(perfiles.valor().contains(pid(b)));

    // Listado en orden de registro y filtrado por perfil.
    auto todas = o.listarIntencionesPaquete({pid(a), pid(b)}, 10);
    QVERIFY(todas);
    QCOMPARE(todas.valor().size(), 2);
    QCOMPARE(todas.valor().at(0).paqueteId, deB);
    QCOMPARE(todas.valor().at(1).paqueteId, error);
    QCOMPARE(todas.valor().at(1).reintentoPendienteEn, en(2));
    QCOMPARE(todas.valor().at(1).solicitudId, sid(s));
    QCOMPARE(todas.valor().at(1).perfilSatId, pid(a));
    QCOMPARE(o.listarIntencionesPaquete({pid(a)}, 10).valor().size(), 1);
    QCOMPARE(o.listarIntencionesPaquete({pid(a), pid(b)}, 1).valor().size(), 1);

    // Consumo condicional (D13): la mas reciente prevalece.
    QVERIFY(e.tx([&] { return o.registrarIntencionPaquete(error, en(3)); }).valor());
    auto vieja = e.tx([&] { return o.consumirIntencionPaquete(error, en(2)); });
    QVERIFY(vieja && !vieja.valor());
    QCOMPARE(pendiente(error), ts(3));
    QVERIFY(e.tx([&] { return o.consumirIntencionPaquete(error, en(3)); }).valor());
    QCOMPARE(pendiente(error), QStringLiteral("-"));
    const auto esTx = [](const auto& r) { return !r && r.error().tipo == ErrorPersistencia::Tipo::Transaccion; };
    QVERIFY(esTx(o.registrarIntencionPaquete(deB, en(4))));
    QVERIFY(esTx(o.consumirIntencionPaquete(deB, en(1))));
    QVERIFY(consistente(e.prov()));

    // obtenerPaquete expone la columna.
    auto leido = o.obtenerPaquete(deB);
    QVERIFY(leido && leido.valor());
    QCOMPARE(leido.valor()->paquete.reintentoPendienteEn, std::optional<QDateTime>(en(1)));
}

// --- Intenciones (D13) ----------------------------------------------------------------

void TestSqliteOperaciones::intencionesLimpiezaCondicional()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    auto& o = e.ops();
    const QString s = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    const auto estado = [&] {
        return e.s(QStringLiteral("verificacion_pendiente || '|' || descarga_pendiente || '|' || "
                                  "ifnull(accion_pendiente_en, '-')"),
                   s);
    };
    const SolicitudId id = sid(s);

    QVERIFY(e.tx([&] { return o.registrarIntencion(id, TipoIntencion::Verificacion, en(1)); }).valor());
    QCOMPARE(estado(), QStringLiteral("1|0|%1").arg(ts(1)));
    QVERIFY(e.tx([&] { return o.registrarIntencion(id, TipoIntencion::Descarga, en(2)); }).valor());
    QCOMPARE(estado(), QStringLiteral("1|1|%1").arg(ts(2))); // ambas coexisten, la mas reciente prevalece
    QVERIFY(consistente(e.prov()));

    // Capturada en t1 pero se registro otra en t2: no se limpia nada.
    auto vieja = e.tx([&] { return o.consumirIntencion(id, TipoIntencion::Verificacion, en(1)); });
    QVERIFY(vieja && !vieja.valor());
    QCOMPARE(estado(), QStringLiteral("1|1|%1").arg(ts(2)));
    // Con el valor vigente se limpia solo su bandera; accion_pendiente_en
    // se conserva mientras quede otra.
    QVERIFY(e.tx([&] { return o.consumirIntencion(id, TipoIntencion::Verificacion, en(2)); }).valor());
    QCOMPARE(estado(), QStringLiteral("0|1|%1").arg(ts(2)));
    // Una intencion nueva mientras se procesa la descarga capturada en t2.
    QVERIFY(e.tx([&] { return o.registrarIntencion(id, TipoIntencion::Verificacion, en(3)); }).valor());
    auto perdida = e.tx([&] { return o.consumirIntencion(id, TipoIntencion::Descarga, en(2)); });
    QVERIFY(perdida && !perdida.valor());
    QCOMPARE(estado(), QStringLiteral("1|1|%1").arg(ts(3)));
    QVERIFY(e.tx([&] { return o.consumirIntencion(id, TipoIntencion::Descarga, en(3)); }).valor());
    QCOMPARE(estado(), QStringLiteral("1|0|%1").arg(ts(3)));
    QVERIFY(e.tx([&] { return o.consumirIntencion(id, TipoIntencion::Verificacion, en(3)); }).valor());
    QCOMPARE(estado(), QStringLiteral("0|0|-"));
    QVERIFY(consistente(e.prov()));
    // Sin intencion vigente no hay nada que consumir.
    auto nada = e.tx([&] { return o.consumirIntencion(id, TipoIntencion::Verificacion, en(3)); });
    QVERIFY(nada && !nada.valor());

    // Eliminada: no registra ni consume.
    const QString borrada = e.solicitud(a, QStringLiteral("Enviada"), {}, true);
    QCOMPARE(e.set(QStringLiteral("solicitud_masiva"), borrada,
                   QStringLiteral("descarga_pendiente = 1, accion_pendiente_en = %1").arg(citar(ts(1)))),
             QString());
    const QString antes = fila(e.prov(), QStringLiteral("solicitud_masiva"), borrada);
    auto r1 = e.tx([&] { return o.registrarIntencion(sid(borrada), TipoIntencion::Verificacion, en(4)); });
    QVERIFY(r1 && !r1.valor());
    auto r2 = e.tx([&] { return o.consumirIntencion(sid(borrada), TipoIntencion::Descarga, en(1)); });
    QVERIFY(r2 && !r2.valor());
    QCOMPARE(fila(e.prov(), QStringLiteral("solicitud_masiva"), borrada), antes);
}

// --- Concurrencia ejecutor / dispatcher ------------------------------------------------

void TestSqliteOperaciones::concurrenciaEjecutorYDispatcher()
{
    // Criterio de T007: escrituras simultaneas del OperacionExecutor (su hilo y
    // su conexion) y del PersistenceDispatcher (otro hilo y otra conexion)
    // sobre la MISMA base, con BEGIN IMMEDIATE y busy_timeout: sin
    // "database is locked" no manejado y base consistente.
    Entorno e;
    QVERIFY(e.preparar());
    const QString a = e.perfil(kRfcPerfil);
    const QString s = e.solicitud(a, QStringLiteral("Creada"));
    const QString t = e.solicitud(a, QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    QVERIFY(!s.isEmpty() && !t.isEmpty());
    const QString nombrePrincipal = e.prov().nombreConexionHiloActual();
    constexpr int kIteraciones = 150;
    std::atomic<int> listos{0};
    const auto arrancarJuntos = [&] {
        listos.fetch_add(1);
        while (listos.load() < 2) {
            QThread::yieldCurrentThread();
        }
    };

    struct Informe {
        QStringList errores;
        QString conexion;
        int busyTimeout = 0;
        int confirmadas = 0;
    };
    const auto log = [](const QString& solicitud, TipoEventoLog tipo, OrigenLog origen, const QDateTime& en) {
        LogEntradaSaneada l;
        l.id = uuid::generarCanonico();
        l.solicitudMasivaId = sid(solicitud);
        l.tipoEvento = tipo;
        l.origen = origen;
        l.creadoEn = en;
        return l;
    };

    // "Ejecutor": ciclo envio -> regreso a Creada + intencion + log, en una
    // transaccion breve que retiene el bloqueo de escritura un instante.
    Informe ejecutor;
    std::unique_ptr<QThread> hilo(QThread::create([&] {
        ejecutor.busyTimeout = escalar(e.prov(), QStringLiteral("PRAGMA busy_timeout")).toInt();
        ejecutor.conexion = e.prov().nombreConexionHiloActual();
        arrancarJuntos();
        for (int i = 0; i < kIteraciones; ++i) {
            const QDateTime instante = en(i);
            auto& o = e.p->operaciones();
            UnitOfWork& uow = e.p->unidadDeTrabajo();
            auto inicio = uow.begin();
            if (!inicio) {
                ejecutor.errores.append(inicio.error().mensaje);
                continue;
            }
            AplicacionEnvio regreso;
            regreso.solicitudId = sid(s);
            regreso.destino = EstadoLocal::Creada;
            regreso.ultimoError = QStringLiteral("Preparacion");
            auto m = o.marcarEnviando(sid(s), instante);
            auto r = m && m.valor() ? o.aplicarEnvio(regreso) : m;
            auto intencion = o.registrarIntencion(sid(t), TipoIntencion::Verificacion, instante);
            auto l = e.p->logs().agregar(log(s, TipoEventoLog::EnvioNoIniciado, OrigenLog::Worker, instante));
            QThread::usleep(300);
            if (!m || !m.valor() || !r || !r.valor() || !intencion || !l) {
                ejecutor.errores.append(!m ? m.error().mensaje
                                        : !r ? r.error().mensaje
                                        : !intencion ? intencion.error().mensaje
                                        : !l ? l.error().mensaje
                                             : QStringLiteral("transicion no aplicada"));
                (void)uow.rollback();
                continue;
            }
            auto c = uow.commit();
            if (!c) {
                ejecutor.errores.append(c.error().mensaje);
            } else {
                ++ejecutor.confirmadas;
            }
        }
        e.p->cerrarConexionDelHiloActual();
    }));

    // "Dispatcher": persistencia general de la UI (perfiles y logs).
    PersistenceDispatcher dispatcher;
    Informe despachado;
    QFuture<void> futuro = dispatcher.despachar<void>([&] {
        despachado.busyTimeout = escalar(e.prov(), QStringLiteral("PRAGMA busy_timeout")).toInt();
        despachado.conexion = e.prov().nombreConexionHiloActual();
        arrancarJuntos();
        for (int i = 0; i < kIteraciones; ++i) {
            UnitOfWork& uow = e.p->unidadDeTrabajo();
            auto inicio = uow.begin();
            if (!inicio) {
                despachado.errores.append(inicio.error().mensaje);
                continue;
            }
            NuevoPerfilSat n;
            n.id = PerfilId::generar();
            n.rfc = QStringLiteral("XAX%1").arg(i, 9, 10, QLatin1Char('0'));
            n.nombre = QStringLiteral("Perfil concurrente");
            n.creadoEn = en(i);
            n.actualizadoEn = n.creadoEn;
            auto p = e.p->perfiles().insertar(n);
            auto l = e.p->logs().agregar(log(t, TipoEventoLog::AccionPendienteRegistrada, OrigenLog::Usuario, en(i)));
            if (!p || !l) {
                despachado.errores.append(!p ? p.error().mensaje : l.error().mensaje);
                (void)uow.rollback();
                continue;
            }
            auto c = uow.commit();
            if (!c) {
                despachado.errores.append(c.error().mensaje);
            } else {
                ++despachado.confirmadas;
            }
        }
        e.p->cerrarConexionDelHiloActual();
    });
    hilo->start();
    QVERIFY(hilo->wait(60000));
    futuro.waitForFinished();
    dispatcher.cerrar();

    QVERIFY2(ejecutor.errores.isEmpty(), qPrintable(ejecutor.errores.join(QStringLiteral("; "))));
    QVERIFY2(despachado.errores.isEmpty(), qPrintable(despachado.errores.join(QStringLiteral("; "))));
    QCOMPARE(ejecutor.confirmadas, kIteraciones);
    QCOMPARE(despachado.confirmadas, kIteraciones);
    // Conexiones distintas, propias de cada hilo y con busy_timeout activo.
    QVERIFY(!ejecutor.conexion.isEmpty() && !despachado.conexion.isEmpty());
    QVERIFY(ejecutor.conexion != despachado.conexion);
    QVERIFY(ejecutor.conexion != nombrePrincipal && despachado.conexion != nombrePrincipal);
    QVERIFY(ejecutor.busyTimeout > 0);
    QVERIFY(despachado.busyTimeout > 0);
    QCOMPARE(e.p->conexionesAbiertas(), 1); // solo la del hilo de la prueba

    // Base consistente: todas las escrituras confirmadas, sin perdidas.
    QCOMPARE(escalar(e.prov(), QStringLiteral("SELECT count(*) FROM perfil_sat")).toInt(), kIteraciones + 1);
    QCOMPARE(escalar(e.prov(), QStringLiteral("SELECT count(*) FROM log_solicitud WHERE tipo_evento = "
                                              "'envio_no_iniciado'"))
                 .toInt(),
             kIteraciones);
    QCOMPARE(escalar(e.prov(), QStringLiteral("SELECT count(*) FROM log_solicitud WHERE tipo_evento = "
                                              "'accion_pendiente_registrada'"))
                 .toInt(),
             kIteraciones);
    QCOMPARE(e.s(QStringLiteral("estado_local || '|' || (envio_iniciado_en IS NULL)"), s), QStringLiteral("Creada|1"));
    QCOMPARE(e.s(QStringLiteral("verificacion_pendiente || '|' || accion_pendiente_en"), t),
             QStringLiteral("1|%1").arg(ts(kIteraciones - 1)));
    QVERIFY(consistente(e.prov()));
}
