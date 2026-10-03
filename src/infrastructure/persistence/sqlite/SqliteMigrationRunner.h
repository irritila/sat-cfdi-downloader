#pragma once

#include "domain/common/Resultado.h"
#include "infrastructure/persistence/sqlite/SqliteTipos.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QDateTime>
#include <QList>
#include <QStringList>
#include <QStringView>

#include <functional>

namespace satcfdi {

class SqliteConnectionProvider;

// Runner de migraciones (docs/design/sqlite-physical-model.md, DM1/DM2).
// INTERNO de infraestructura; capas superiores usan inicializarBaseSqlite().
//
// Algoritmo (sobre la conexion del hilo actual del proveedor):
// 1. Si `schema_migrations` no existe y hay otros objetos de usuario en
//    sqlite_master: falla (Migracion) sin escribir.
// 2. max(version) > ultima conocida: falla (Migracion) antes de abrir
//    transaccion, sin modificar el archivo.
// 3. Por cada migracion pendiente, en orden: BEGIN IMMEDIATE; crea
//    schema_migrations si falta; relee la version (otra conexion pudo
//    migrar); ejecuta cada sentencia (DM2); inserta (version, aplicada_en);
//    COMMIT. Ante cualquier error: ROLLBACK total (incluido el bootstrap de
//    schema_migrations) y error Migracion con el codigo nativo.
// 4. Repetir no ejecuta nada.
//
// El runner es propietario de schema_migrations; las migraciones no la tocan.
class SqliteMigrationRunner {
public:
    using Reloj = std::function<QDateTime()>;

    // `migraciones`: version > 0 y unicas (se ordenan). Se validan en migrar().
    SqliteMigrationRunner(SqliteConnectionProvider& proveedor, QList<MigracionSql> migraciones,
                          Reloj reloj = {});

    Resultado<ResultadoMigracion, ErrorPersistencia> migrar();

    // DM2: acumula lineas hasta una linea cuyo contenido sin espacios es `;`.
    // Omite bloques solo de comentarios (`--` de linea completa) o espacios.
    // Error (Migracion) si queda contenido no comentario sin terminador o si el
    // texto empieza con BOM.
    static Resultado<QStringList, ErrorPersistencia> dividirSentencias(QStringView sql);

    // Migraciones embebidas como recurso Qt (`:/migrations/001_initial_schema.sql`).
    static Resultado<QList<MigracionSql>, ErrorPersistencia> migracionesEmbebidas();

private:
    SqliteConnectionProvider& m_proveedor;
    QList<MigracionSql> m_migraciones;
    Reloj m_reloj;
};

} // namespace satcfdi
