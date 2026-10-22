#include "infrastructure/persistence/sqlite/SqliteBaseRestauracion.h"

#include "domain/common/TimestampUtc.h"
#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteMigrationRunner.h"

#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

namespace satcfdi {

using respaldo::ErrorRestauracion;
using respaldo::InformeBaseRestauracion;
using C = ErrorRestauracion::Categoria;
using R = Resultado<InformeBaseRestauracion, ErrorRestauracion>;

namespace {

ErrorRestauracion error(C c, const char* diagnostico)
{
    return ErrorRestauracion::de(c, QString::fromLatin1(diagnostico));
}

bool escalar(QSqlDatabase& db, const QString& sql, QVariant& valor)
{
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) {
        return false;
    }
    valor = q.value(0);
    return true;
}

bool integridadOk(QSqlDatabase& db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA integrity_check"))) {
        return false;
    }
    QStringList filas;
    while (q.next()) {
        filas.append(q.value(0).toString());
    }
    return filas == QStringList{QStringLiteral("ok")};
}

bool clavesForaneasOk(QSqlDatabase& db)
{
    QSqlQuery q(db);
    return q.exec(QStringLiteral("PRAGMA foreign_key_check")) && !q.next();
}

std::optional<int> cambios(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        return std::nullopt;
    }
    return q.numRowsAffected();
}

// Objetos de esquema (type, name, tbl_name y sql con espacios normalizados),
// sin los internos `sqlite_*`, en orden estable.
std::optional<QStringList> esquemaDe(QSqlDatabase& db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT type, name, tbl_name, sql FROM sqlite_master "
                               "WHERE name NOT LIKE 'sqlite\\_%' ESCAPE '\\' ORDER BY type, name"))) {
        return std::nullopt;
    }
    QStringList r;
    while (q.next()) {
        r.append(QStringLiteral("%1|%2|%3|%4")
                     .arg(q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),
                          q.value(3).toString().simplified()));
    }
    return r;
}

// Esquema que producen las migraciones conocidas hasta `version`, en una base
// en memoria con conexion propia (mismo hilo).
std::optional<QStringList> esquemaEsperado(const QList<MigracionSql>& migraciones, int version)
{
    QList<MigracionSql> hasta;
    for (const MigracionSql& m : migraciones) {
        if (m.version <= version) {
            hasta.append(m);
        }
    }
    OpcionesConexionSqlite opciones;
    opciones.exigirWal = false;
    SqliteConnectionProvider proveedor(QStringLiteral(":memory:"), opciones);
    std::optional<QStringList> r;
    {
        auto conexion = proveedor.conexion();
        if (conexion) {
            SqliteMigrationRunner runner(proveedor, hasta);
            auto migrado = runner.migrar();
            if (migrado && migrado.valor().versionFinal == version) {
                QSqlDatabase db = conexion.valor();
                r = esquemaDe(db);
            }
        }
    }
    proveedor.cerrarConexionDelHiloActual();
    return r;
}

// Seguridad 2: la base solo puede tener los objetos de sus migraciones (un
// trigger que reinserte credencial_sat o una vista extra se rechazan).
bool esquemaCoincide(QSqlDatabase& db, const QList<MigracionSql>& migraciones, int version)
{
    const auto actual = esquemaDe(db);
    const auto esperado = esquemaEsperado(migraciones, version);
    return actual && esperado && *actual == *esperado;
}

// Todo lo que toca la base, con la conexion del hilo actual del proveedor.
R prepararCon(SqliteConnectionProvider& proveedor, const QList<MigracionSql>& migraciones, int maxima)
{
    auto conexion = proveedor.conexion();
    if (!conexion) {
        return R::fallo(error(C::Base, "base.abrir"));
    }
    QSqlDatabase db = conexion.valor();
    InformeBaseRestauracion informe;

    QVariant v;
    if (!escalar(db, QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = "
                                    "'schema_migrations'"),
                 v)
        || v.toInt() != 1) {
        return R::fallo(error(C::EsquemaInvalido, "esquema.sin_tabla"));
    }
    if (!escalar(db, QStringLiteral("SELECT COALESCE(MAX(version), 0) FROM schema_migrations"), v)) {
        return R::fallo(error(C::EsquemaInvalido, "esquema.version"));
    }
    informe.versionInicial = v.toInt();
    if (informe.versionInicial > maxima) {
        return R::fallo(error(C::EsquemaMasNuevo, "esquema.mas_nuevo"));
    }
    if (informe.versionInicial <= 0) {
        return R::fallo(error(C::EsquemaInvalido, "esquema.vacio"));
    }
    if (!integridadOk(db)) {
        return R::fallo(error(C::Integridad, "integridad.inicial"));
    }
    if (!esquemaCoincide(db, migraciones, informe.versionInicial)) {
        return R::fallo(error(C::EsquemaInvalido, "esquema.objetos_ajenos"));
    }

    SqliteMigrationRunner runner(proveedor, migraciones);
    auto migracion = runner.migrar();
    if (!migracion) {
        return R::fallo(error(C::Base, "base.migrar"));
    }
    informe.versionFinal = migracion.valor().versionFinal;

    // D3: la e.firma no se restaura. Sin fila en credencial_sat el perfil
    // queda SinCredencial ("requiere registrar e.firma") y el ejecutor no
    // llama a SAT por el (ADR 0018, gate por perfil).
    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
            return R::fallo(error(C::Base, "base.begin"));
        }
        const auto avisos = cambios(db, QStringLiteral("DELETE FROM aviso_vencimiento_efirma"));
        const auto credenciales = cambios(db, QStringLiteral("DELETE FROM credencial_sat"));
        if (!avisos || !credenciales) {
            QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
            return R::fallo(error(C::Base, "base.borrar_credenciales"));
        }
        if (!QSqlQuery(db).exec(QStringLiteral("COMMIT"))) {
            QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
            return R::fallo(error(C::Base, "base.commit"));
        }
        informe.avisosBorrados = std::max(0, *avisos);
        informe.credencialesBorradas = std::max(0, *credenciales);
    }

    // Defensa en profundidad: nada volvio a escribir credenciales.
    if (!escalar(db, QStringLiteral("SELECT (SELECT COUNT(*) FROM credencial_sat) + "
                                    "(SELECT COUNT(*) FROM aviso_vencimiento_efirma)"),
                 v)
        || v.toInt() != 0) {
        return R::fallo(error(C::Integridad, "integridad.credenciales"));
    }
    if (!esquemaCoincide(db, migraciones, informe.versionFinal)) {
        return R::fallo(error(C::EsquemaInvalido, "esquema.objetos_ajenos"));
    }
    if (!integridadOk(db)) {
        return R::fallo(error(C::Integridad, "integridad.final"));
    }
    if (!clavesForaneasOk(db)) {
        return R::fallo(error(C::Integridad, "integridad.claves_foraneas"));
    }

    if (escalar(db, QStringLiteral("SELECT COUNT(*) FROM perfil_sat WHERE eliminado_en IS NULL"), v)) {
        informe.perfiles = v.toInt();
    }
    if (escalar(db, QStringLiteral("SELECT COUNT(*) FROM solicitud_masiva WHERE eliminado_en IS NULL"), v)) {
        informe.solicitudes = v.toInt();
    }
    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral(
                "SELECT p.ruta_local, i.sha256, i.tamano_bytes, i.modificado_en_ms, p.descargado_en "
                "FROM paquete_solicitud p LEFT JOIN indice_cfdi_paquete i ON i.paquete_solicitud_id = p.id "
                "WHERE p.estado_descarga = 'Descargado' AND p.ruta_local IS NOT NULL ORDER BY p.ruta_local, p.id"))) {
            return R::fallo(error(C::Base, "base.paquetes"));
        }
        while (q.next()) {
            respaldo::MtimePaquete m;
            m.rutaLocal = q.value(0).toString();
            if (!respaldo::esRutaRelativaValida(m.rutaLocal)) {
                continue; // la captura tampoco la copio (T025.2)
            }
            if (!q.value(1).isNull()) {
                m.sha256 = QByteArray::fromHex(q.value(1).toString().toLatin1());
                m.tamano = q.value(2).toLongLong();
                m.modificadoMs = q.value(3).toLongLong();
            }
            if (!q.value(4).isNull()) {
                if (const auto t = timestamp::desdeTexto(q.value(4).toString())) {
                    m.descargadoMs = t->toMSecsSinceEpoch();
                }
            }
            informe.paquetes.append(m);
        }
        informe.paquetesDescargados = static_cast<int>(informe.paquetes.size());
    }

    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("PRAGMA journal_mode=DELETE")) || !q.next()
            || q.value(0).toString().compare(QLatin1String("delete"), Qt::CaseInsensitive) != 0) {
            return R::fallo(error(C::Base, "base.journal"));
        }
    }
    return R::exito(std::move(informe));
}

} // namespace

SqliteBaseRestauracion::SqliteBaseRestauracion(std::optional<QList<MigracionSql>> migraciones)
{
    if (migraciones) {
        m_migraciones = std::move(*migraciones);
        m_migracionesValidas = true;
    } else if (auto embebidas = SqliteMigrationRunner::migracionesEmbebidas()) {
        m_migraciones = std::move(embebidas).valor();
        m_migracionesValidas = true;
    }
}

int SqliteBaseRestauracion::versionMaximaConocida() const
{
    int maxima = 0;
    for (const MigracionSql& m : m_migraciones) {
        maxima = std::max(maxima, m.version);
    }
    return maxima;
}

R SqliteBaseRestauracion::preparar(const QString& rutaBase)
{
    if (!m_migracionesValidas || m_migraciones.isEmpty()) {
        return R::fallo(error(C::Interno, "base.migraciones"));
    }
    if (!QFileInfo(rutaBase).isFile()) {
        return R::fallo(error(C::Base, "base.no_existe"));
    }
    OpcionesConexionSqlite opciones;
    opciones.exigirWal = false;
    SqliteConnectionProvider proveedor(rutaBase, opciones);
    R r = prepararCon(proveedor, m_migraciones, versionMaximaConocida());
    proveedor.cerrarConexionDelHiloActual();
    return r;
}

} // namespace satcfdi
