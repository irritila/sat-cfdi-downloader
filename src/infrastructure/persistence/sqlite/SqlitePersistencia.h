#pragma once

#include "domain/common/Resultado.h"
#include "infrastructure/persistence/sqlite/SqliteTipos.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QList>
#include <QString>

#include <memory>

namespace satcfdi {

class SqliteConnectionProvider;
class PerfilSatRepository;
class SolicitudMasivaRepository;
class PaqueteSolicitudRepository;
class LogSolicitudRepository;
class ConfiguracionAppRepository;
class CredencialSatRepository;
class OperacionesSolicitudRepository;
class UnitOfWork;

// API PUBLICA de persistencia SQLite para bootstrap y composition root
// (ADR 0016). No expone tipos QSql*: quien la incluye no necesita Qt6::Sql.

// Inicializa la base en `rutaBase` (archivo, p. ej. <dir>/satcfdi.sqlite3) en
// el hilo actual con una conexion de bootstrap propia:
// 1. Abre la conexion: DM4 (sqlite_version >= 3.9.0 y json_valid),
//    foreign_keys=ON y busy_timeout verificados.
// 2. Migra con `migraciones` (DM1/DM2). Version futura o base con tablas sin
//    schema_migrations: error Migracion SIN modificar el archivo.
// 3. Habilita journal_mode=WAL.
// 4. Cierra y retira la conexion en este mismo hilo antes de volver (tambien
//    ante error).
// No crea el directorio padre. Errores: Almacenamiento, Migracion, Ocupado.
Resultado<InformeInicializacionSqlite, ErrorPersistencia>
inicializarBaseSqlite(const QString& rutaBase, const QList<MigracionSql>& migraciones,
                      OpcionesConexionSqlite opciones = {});

// Igual, con las migraciones embebidas (`:/migrations/001_initial_schema.sql`,
// `:/migrations/002_credencial_metadata.sql` y
// `:/migrations/003_worker_ejecutor.sql`).
Resultado<InformeInicializacionSqlite, ErrorPersistencia>
inicializarBaseSqlite(const QString& rutaBase);

// Migraciones embebidas como recurso Qt, en orden (001, 002 y 003).
Resultado<QList<MigracionSql>, ErrorPersistencia> migracionesSqliteEmbebidas();

// Grafo de persistencia SQLite de trabajo: proveedor de conexiones por hilo +
// repositorios + UnitOfWork, todos sobre la MISMA base ya inicializada.
//
// Ownership: el composition root crea y destruye esta instancia; los puertos
// devueltos son referencias no propietarias validas mientras viva.
// Hilo: se puede construir en el hilo grafico (no abre conexiones). Los
// puertos solo se usan desde tareas de PersistenceDispatcher; la conexion se
// crea perezosamente en el primer uso, en el hilo de la tarea.
// Cierre: antes de PersistenceDispatcher::cerrar(), despachar una tarea que
// llame cerrarConexionDelHiloActual(); despues destruir esta instancia.
// Por defecto exige WAL (la base debe venir de inicializarBaseSqlite()).
class SqlitePersistencia {
public:
    explicit SqlitePersistencia(QString rutaBase, OpcionesConexionSqlite opciones = {});
    ~SqlitePersistencia();

    SqlitePersistencia(const SqlitePersistencia&) = delete;
    SqlitePersistencia& operator=(const SqlitePersistencia&) = delete;

    PerfilSatRepository& perfiles() noexcept;
    SolicitudMasivaRepository& solicitudes() noexcept;
    PaqueteSolicitudRepository& paquetes() noexcept;
    LogSolicitudRepository& logs() noexcept;
    ConfiguracionAppRepository& configuracion() noexcept;
    CredencialSatRepository& credenciales() noexcept; // T005
    // T007: operaciones del OperacionExecutor. Mismo proveedor (conexion por
    // hilo): usado desde el hilo del ejecutor obtiene SU propia conexion,
    // distinta de la del PersistenceDispatcher. Sus escrituras usan
    // unidadDeTrabajo() en ese mismo hilo. Antes de terminar el hilo del
    // ejecutor, llamar cerrarConexionDelHiloActual() desde ese hilo.
    OperacionesSolicitudRepository& operaciones() noexcept;
    UnitOfWork& unidadDeTrabajo() noexcept;

    // Cierra y retira la conexion del hilo actual (revierte una transaccion
    // abierta). Llamar en el hilo propietario sin consultas en curso.
    // Idempotente.
    void cerrarConexionDelHiloActual();

    // Conexiones abiertas en todos los hilos (diagnostico y pruebas).
    int conexionesAbiertas() const;

    // Solo infraestructura y sus pruebas.
    SqliteConnectionProvider& proveedor() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace satcfdi
