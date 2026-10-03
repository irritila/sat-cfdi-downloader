#pragma once

#include <QList>
#include <QString>

namespace satcfdi {

// Tipos PUBLICOS de la infraestructura SQLite (ADR 0016). Sin tipos QSql*:
// los pueden incluir app_core y pruebas de integracion.

// Configuracion de cada conexion SQLite.
struct OpcionesConexionSqlite {
    // PRAGMA busy_timeout (ms), verificado por lectura tras fijarlo.
    int busyTimeoutMs = 5000;
    // Si es true, una conexion nueva falla (Almacenamiento) cuando la base no
    // esta en journal_mode=WAL. inicializarBaseSqlite() habilita WAL; las
    // conexiones de trabajo (dispatcher) lo exigen.
    bool exigirWal = true;
};

// Migracion inyectable: version > 0, nombre para diagnostico y SQL con la
// convencion DM2 (sentencias terminadas por una linea que solo contiene `;`).
struct MigracionSql {
    int version = 0;
    QString nombre;
    QString sql;
};

// Resultado de SqliteMigrationRunner::migrar().
struct ResultadoMigracion {
    int versionInicial = 0;   // max(version) antes de migrar (0 = base vacia)
    int versionFinal = 0;     // max(version) despues de migrar
    QList<int> aplicadas;     // versiones aplicadas en esta ejecucion, en orden
};

// Resultado de inicializarBaseSqlite().
struct InformeInicializacionSqlite {
    QString versionSqlite;    // sqlite_version()
    QString journalMode;      // "wal" tras inicializar
    ResultadoMigracion migracion;
};

} // namespace satcfdi
