#pragma once

#include "domain/common/Resultado.h"
#include "infrastructure/persistence/sqlite/SqliteTipos.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/persistence/Exito.h"

#include <QHash>
#include <QMutex>
#include <QSqlDatabase>
#include <QString>

class QThread;

namespace satcfdi {

// Proveedor de conexiones SQLite por hilo (ADR 0016, DA4). INTERNO de
// infraestructura: expone QSqlDatabase y solo lo usan repositorios, UnitOfWork,
// runner e inicializador. Capas superiores usan SqlitePersistencia.
//
// Reglas:
// - Una conexion por (proveedor, hilo), con nombre unico
//   `satcfdi-sqlite-<instancia>-<hilo>`. Se crea perezosamente en conexion()
//   y se configura en el hilo que la usara: driver QSQLITE, DM4
//   (sqlite_version() >= 3.9.0 y json_valid('{}')), PRAGMA foreign_keys=ON y
//   busy_timeout verificados por lectura, journal_mode=WAL si exigirWal.
// - La conexion nunca cruza hilos. cerrarConexionDelHiloActual() hace
//   close() + QSqlDatabase::removeDatabase() en el hilo propietario; el
//   llamador garantiza que no quedan QSqlQuery/QSqlDatabase vivos de esa
//   conexion (los repositorios no retienen handles entre llamadas).
// - Las conexiones de otros hilos deben cerrarse en su hilo antes de destruir
//   el proveedor (p. ej., tarea final despachada en PersistenceDispatcher).
//   El destructor cierra la del hilo actual y avisa si quedan otras.
// - Thread-safe: el mapa de conexiones esta protegido por mutex.
class SqliteConnectionProvider {
public:
    explicit SqliteConnectionProvider(QString rutaBase, OpcionesConexionSqlite opciones = {});
    ~SqliteConnectionProvider();

    SqliteConnectionProvider(const SqliteConnectionProvider&) = delete;
    SqliteConnectionProvider& operator=(const SqliteConnectionProvider&) = delete;

    // Conexion abierta y configurada del hilo actual (la crea si no existe).
    // Si la configuracion falla, la conexion se cierra y retira antes de
    // devolver el error. Errores: Almacenamiento (driver ausente, apertura,
    // PRAGMA no aplicado, WAL exigido y ausente), Migracion (DM4).
    Resultado<QSqlDatabase, ErrorPersistencia> conexion();

    // close() + removeDatabase() de la conexion del hilo actual. Si habia una
    // transaccion activa, la revierte antes. Idempotente.
    void cerrarConexionDelHiloActual();

    bool tieneConexionEnHiloActual() const;
    QString nombreConexionHiloActual() const; // vacio si no hay conexion
    int conexionesAbiertas() const;           // todos los hilos

    // Estado transaccional de la conexion del hilo actual (lo gestiona
    // SqliteUnitOfWork; los repositorios lo consultan para escrituras).
    bool transaccionActiva() const;
    void marcarTransaccion(bool activa);

    // `PRAGMA journal_mode=WAL` sobre la conexion del hilo actual; devuelve el
    // modo resultante en minusculas. Error si no queda en "wal".
    Resultado<QString, ErrorPersistencia> habilitarWal();

    // sqlite_version() de la conexion del hilo actual (tras conexion()).
    Resultado<QString, ErrorPersistencia> versionSqlite();

    const QString& rutaBase() const noexcept { return m_rutaBase; }
    const OpcionesConexionSqlite& opciones() const noexcept { return m_opciones; }

private:
    struct Entrada {
        QString nombre;
        bool transaccionActiva = false;
    };

    Resultado<Exito, ErrorPersistencia> configurar(QSqlDatabase& db);
    void cerrarYRetirar(const QString& nombre);

    const QString m_rutaBase;
    const OpcionesConexionSqlite m_opciones;
    const quint64 m_instancia;

    mutable QMutex m_mutex;
    QHash<const QThread*, Entrada> m_conexiones;
};

} // namespace satcfdi
