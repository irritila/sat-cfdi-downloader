#include "infrastructure/persistence/sqlite/SqliteMigrationRunner.h"

#include "domain/common/TimestampUtc.h"
#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QFile>
#include <QSet>
#include <QSqlQuery>

#include <algorithm>
#include <utility>

namespace satcfdi {

namespace {

using ResultadoMig = Resultado<ResultadoMigracion, ErrorPersistencia>;

const QString kRecurso001 = QStringLiteral(":/migrations/001_initial_schema.sql");

ErrorPersistencia errorMigracion(const QString& mensaje)
{
    return ErrorPersistencia::de(ErrorPersistencia::Tipo::Migracion, mensaje);
}

// Convierte un error SQL en error de migracion conservando codigo y restriccion.
ErrorPersistencia comoMigracion(ErrorPersistencia e, const QString& mensaje)
{
    e.tipo = ErrorPersistencia::Tipo::Migracion;
    e.mensaje = mensaje;
    return e;
}

bool esComentarioOVacia(QStringView linea)
{
    const QStringView t = linea.trimmed();
    return t.isEmpty() || t.startsWith(u"--");
}

Resultado<bool, ErrorPersistencia> existeHistorial(QSqlDatabase& db)
{
    QSqlQuery q(db);
    if (auto r = sqlite::ejecutarDirecto(
            q,
            QStringLiteral("SELECT count(*) FROM sqlite_master "
                           "WHERE type = 'table' AND name = 'schema_migrations'"),
            u"migracion.historial");
        !r) {
        return Resultado<bool, ErrorPersistencia>::fallo(std::move(r).error());
    }
    q.next();
    return Resultado<bool, ErrorPersistencia>::exito(q.value(0).toInt() > 0);
}

Resultado<int, ErrorPersistencia> contarObjetosUsuario(QSqlDatabase& db)
{
    QSqlQuery q(db);
    if (auto r = sqlite::ejecutarDirecto(
            q,
            QStringLiteral("SELECT count(*) FROM sqlite_master "
                           "WHERE name NOT LIKE 'sqlite_%' AND name <> 'schema_migrations'"),
            u"migracion.objetos");
        !r) {
        return Resultado<int, ErrorPersistencia>::fallo(std::move(r).error());
    }
    q.next();
    return Resultado<int, ErrorPersistencia>::exito(q.value(0).toInt());
}

Resultado<int, ErrorPersistencia> versionActual(QSqlDatabase& db)
{
    QSqlQuery q(db);
    if (auto r = sqlite::ejecutarDirecto(
            q, QStringLiteral("SELECT COALESCE(MAX(version), 0) FROM schema_migrations"),
            u"migracion.version");
        !r) {
        return Resultado<int, ErrorPersistencia>::fallo(std::move(r).error());
    }
    q.next();
    return Resultado<int, ErrorPersistencia>::exito(q.value(0).toInt());
}

void revertir(QSqlDatabase& db)
{
    QSqlQuery q(db);
    q.exec(QStringLiteral("ROLLBACK"));
}

} // namespace

SqliteMigrationRunner::SqliteMigrationRunner(SqliteConnectionProvider& proveedor,
                                             QList<MigracionSql> migraciones, Reloj reloj)
    : m_proveedor(proveedor)
    , m_migraciones(std::move(migraciones))
    , m_reloj(reloj ? std::move(reloj) : Reloj(&timestamp::ahoraUtc))
{
    std::sort(m_migraciones.begin(), m_migraciones.end(),
              [](const MigracionSql& a, const MigracionSql& b) { return a.version < b.version; });
}

Resultado<QStringList, ErrorPersistencia> SqliteMigrationRunner::dividirSentencias(QStringView sql)
{
    using R = Resultado<QStringList, ErrorPersistencia>;
    if (sql.startsWith(QChar(0xFEFF))) {
        return R::fallo(errorMigracion(QStringLiteral("migracion con BOM (DM2)")));
    }
    QStringList sentencias;
    QStringList actual;
    bool tieneCodigo = false;
    for (QStringView linea : sql.split(u'\n')) {
        if (linea.endsWith(u'\r')) {
            linea.chop(1);
        }
        if (linea.trimmed() == u";") {
            if (tieneCodigo) {
                sentencias.append(actual.join(u'\n').trimmed());
            }
            actual.clear();
            tieneCodigo = false;
            continue;
        }
        actual.append(linea.toString());
        if (!esComentarioOVacia(linea)) {
            tieneCodigo = true;
        }
    }
    if (tieneCodigo) {
        return R::fallo(errorMigracion(
            QStringLiteral("sentencia sin linea terminadora ';' al final de la migracion (DM2)")));
    }
    return R::exito(sentencias);
}

Resultado<QList<MigracionSql>, ErrorPersistencia> SqliteMigrationRunner::migracionesEmbebidas()
{
    using R = Resultado<QList<MigracionSql>, ErrorPersistencia>;
    QFile archivo(kRecurso001);
    if (!archivo.open(QIODevice::ReadOnly)) {
        return R::fallo(errorMigracion(QStringLiteral("recurso de migracion 001 no disponible")));
    }
    const QByteArray bytes = archivo.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        return R::fallo(errorMigracion(QStringLiteral("migracion 001 con BOM (DM2)")));
    }
    return R::exito({MigracionSql{1, QStringLiteral("001_initial_schema"),
                                  QString::fromUtf8(bytes)}});
}

Resultado<ResultadoMigracion, ErrorPersistencia> SqliteMigrationRunner::migrar()
{
    // Validacion de la lista inyectada.
    QSet<int> versiones;
    for (const MigracionSql& m : std::as_const(m_migraciones)) {
        if (m.version <= 0 || versiones.contains(m.version)) {
            return ResultadoMig::fallo(errorMigracion(
                QStringLiteral("lista de migraciones invalida (version %1)").arg(m.version)));
        }
        versiones.insert(m.version);
    }
    const int ultimaConocida = m_migraciones.isEmpty() ? 0 : m_migraciones.constLast().version;

    if (m_proveedor.transaccionActiva()) {
        return ResultadoMig::fallo(ErrorPersistencia::de(
            ErrorPersistencia::Tipo::Transaccion,
            QStringLiteral("migracion con transaccion activa en la conexion")));
    }
    auto conexion = m_proveedor.conexion();
    if (!conexion) {
        return ResultadoMig::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();

    // Pasos 1 y 2: solo lecturas; nada se escribe si fallan.
    auto historial = existeHistorial(db);
    if (!historial) {
        return ResultadoMig::fallo(std::move(historial).error());
    }
    int actual = 0;
    if (!historial.valor()) {
        auto objetos = contarObjetosUsuario(db);
        if (!objetos) {
            return ResultadoMig::fallo(std::move(objetos).error());
        }
        if (objetos.valor() > 0) {
            return ResultadoMig::fallo(errorMigracion(
                QStringLiteral("la base tiene objetos pero no schema_migrations")));
        }
    } else {
        auto version = versionActual(db);
        if (!version) {
            return ResultadoMig::fallo(std::move(version).error());
        }
        actual = version.valor();
    }
    if (actual > ultimaConocida) {
        return ResultadoMig::fallo(errorMigracion(
            QStringLiteral("version de esquema futura %1 (ultima conocida %2)")
                .arg(actual)
                .arg(ultimaConocida)));
    }

    ResultadoMigracion resultado;
    resultado.versionInicial = actual;
    resultado.versionFinal = actual;

    for (const MigracionSql& migracion : std::as_const(m_migraciones)) {
        if (migracion.version <= resultado.versionFinal) {
            continue;
        }
        auto sentencias = dividirSentencias(migracion.sql);
        if (!sentencias) {
            ErrorPersistencia e = std::move(sentencias).error();
            e.mensaje = QStringLiteral("migracion %1: %2").arg(migracion.version).arg(e.mensaje);
            return ResultadoMig::fallo(std::move(e));
        }
        const QString etiqueta =
            QStringLiteral("migracion %1 (%2)").arg(migracion.version).arg(migracion.nombre);

        {
            QSqlQuery q(db);
            if (auto r = sqlite::ejecutarDirecto(q, QStringLiteral("BEGIN IMMEDIATE"),
                                                 u"migracion.begin");
                !r) {
                return ResultadoMig::fallo(
                    comoMigracion(std::move(r).error(), etiqueta + QStringLiteral(": BEGIN fallo")));
            }
        }

        auto fallar = [&](ErrorPersistencia e, const QString& mensaje) {
            revertir(db);
            return ResultadoMig::fallo(comoMigracion(std::move(e), mensaje));
        };

        {
            QSqlQuery q(db);
            if (auto r = sqlite::ejecutarDirecto(
                    q,
                    QStringLiteral("CREATE TABLE IF NOT EXISTS schema_migrations ("
                                   "version INTEGER PRIMARY KEY, aplicada_en TEXT NOT NULL)"),
                    u"migracion.schema_migrations");
                !r) {
                return fallar(std::move(r).error(),
                              etiqueta + QStringLiteral(": no se pudo crear schema_migrations"));
            }
        }
        // Relectura bajo el bloqueo de escritura.
        auto relectura = versionActual(db);
        if (!relectura) {
            return fallar(std::move(relectura).error(),
                          etiqueta + QStringLiteral(": no se pudo leer la version"));
        }
        if (relectura.valor() > ultimaConocida) {
            revertir(db);
            return ResultadoMig::fallo(errorMigracion(
                QStringLiteral("version de esquema futura %1").arg(relectura.valor())));
        }
        if (relectura.valor() >= migracion.version) {
            revertir(db);
            resultado.versionFinal = relectura.valor();
            continue;
        }

        const QStringList& lista = sentencias.valor();
        for (qsizetype i = 0; i < lista.size(); ++i) {
            QSqlQuery q(db);
            if (auto r = sqlite::ejecutarDirecto(q, lista.at(i), u"migracion.sentencia"); !r) {
                return fallar(std::move(r).error(),
                              etiqueta
                                  + QStringLiteral(": fallo la sentencia %1 de %2")
                                        .arg(i + 1)
                                        .arg(lista.size()));
            }
        }

        {
            QSqlQuery q(db);
            if (auto r = sqlite::preparar(
                    q,
                    QStringLiteral("INSERT INTO schema_migrations (version, aplicada_en) "
                                   "VALUES (:version, :aplicada_en)"),
                    u"migracion.registro");
                !r) {
                return fallar(std::move(r).error(),
                              etiqueta + QStringLiteral(": no se pudo registrar la version"));
            }
            q.bindValue(QStringLiteral(":version"), sqlite::entero(migracion.version));
            q.bindValue(QStringLiteral(":aplicada_en"), sqlite::instante(m_reloj()));
            if (auto r = sqlite::ejecutar(q, u"migracion.registro"); !r) {
                return fallar(std::move(r).error(),
                              etiqueta + QStringLiteral(": no se pudo registrar la version"));
            }
        }

        {
            QSqlQuery q(db);
            if (auto r = sqlite::ejecutarDirecto(q, QStringLiteral("COMMIT"), u"migracion.commit");
                !r) {
                return fallar(std::move(r).error(), etiqueta + QStringLiteral(": COMMIT fallo"));
            }
        }
        resultado.aplicadas.append(migracion.version);
        resultado.versionFinal = migracion.version;
    }
    return ResultadoMig::exito(std::move(resultado));
}

} // namespace satcfdi
