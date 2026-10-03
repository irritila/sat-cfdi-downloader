#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"

#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QMutexLocker>
#include <QSqlQuery>
#include <QThread>
#include <QVersionNumber>
#include <QtLogging>

#include <atomic>
#include <utility>

namespace satcfdi {

namespace {

const QString kDriver = QStringLiteral("QSQLITE");

std::atomic<quint64> g_siguienteInstancia{1};

using ResultadoExito = Resultado<Exito, ErrorPersistencia>;

ResultadoExito falloAlmacenamiento(const QString& mensaje)
{
    return ResultadoExito::fallo(
        ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, mensaje));
}

// Lee un PRAGMA escalar.
Resultado<QVariant, ErrorPersistencia> leerPragma(QSqlDatabase& db, const QString& pragma)
{
    QSqlQuery q(db);
    const QString contexto = QStringLiteral("pragma ") + pragma;
    if (auto r = sqlite::ejecutarDirecto(q, QStringLiteral("PRAGMA ") + pragma, contexto); !r) {
        return Resultado<QVariant, ErrorPersistencia>::fallo(std::move(r).error());
    }
    if (!q.next()) {
        return Resultado<QVariant, ErrorPersistencia>::fallo(ErrorPersistencia::de(
            ErrorPersistencia::Tipo::Almacenamiento, contexto + QStringLiteral(": sin valor")));
    }
    return Resultado<QVariant, ErrorPersistencia>::exito(q.value(0));
}

} // namespace

SqliteConnectionProvider::SqliteConnectionProvider(QString rutaBase, OpcionesConexionSqlite opciones)
    : m_rutaBase(std::move(rutaBase))
    , m_opciones(opciones)
    , m_instancia(g_siguienteInstancia.fetch_add(1))
{
}

SqliteConnectionProvider::~SqliteConnectionProvider()
{
    cerrarConexionDelHiloActual();
    QMutexLocker lock(&m_mutex);
    if (!m_conexiones.isEmpty()) {
        qWarning("SqliteConnectionProvider destruido con %lld conexion(es) abiertas en otros hilos",
                 static_cast<long long>(m_conexiones.size()));
    }
}

Resultado<QSqlDatabase, ErrorPersistencia> SqliteConnectionProvider::conexion()
{
    using R = Resultado<QSqlDatabase, ErrorPersistencia>;
    const QThread* hilo = QThread::currentThread();
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_conexiones.constFind(hilo);
        if (it != m_conexiones.constEnd()) {
            return R::exito(QSqlDatabase::database(it->nombre, false));
        }
    }

    if (!QSqlDatabase::isDriverAvailable(kDriver)) {
        return R::fallo(ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento,
                                              QStringLiteral("driver QSQLITE no disponible")));
    }

    const QString nombre = QStringLiteral("satcfdi-sqlite-%1-%2")
                               .arg(m_instancia)
                               .arg(reinterpret_cast<quintptr>(hilo), 0, 16);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(kDriver, nombre);
        db.setDatabaseName(m_rutaBase);
        if (!db.open()) {
            ErrorPersistencia e = sqlite::traducirError(db.lastError(), u"conexion.abrir");
            e.tipo = ErrorPersistencia::Tipo::Almacenamiento;
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(nombre);
            return R::fallo(std::move(e));
        }
        if (auto r = configurar(db); !r) {
            db = QSqlDatabase();
            cerrarYRetirar(nombre);
            return R::fallo(std::move(r).error());
        }
    }

    {
        QMutexLocker lock(&m_mutex);
        m_conexiones.insert(hilo, Entrada{nombre, false});
    }
    return R::exito(QSqlDatabase::database(nombre, false));
}

Resultado<Exito, ErrorPersistencia> SqliteConnectionProvider::configurar(QSqlDatabase& db)
{
    // DM4: version minima y JSON1 antes de cualquier otra cosa.
    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("SELECT sqlite_version(), json_valid('{}')")) || !q.next()) {
            return ResultadoExito::fallo(ErrorPersistencia::de(
                ErrorPersistencia::Tipo::Migracion,
                QStringLiteral("SQLite sin JSON1 o version no verificable (DM4)")));
        }
        const QVersionNumber version = QVersionNumber::fromString(q.value(0).toString());
        if (version < QVersionNumber(3, 9, 0) || q.value(1).toInt() != 1) {
            return ResultadoExito::fallo(ErrorPersistencia::de(
                ErrorPersistencia::Tipo::Migracion,
                QStringLiteral("SQLite >= 3.9.0 con JSON1 requerido (DM4)")));
        }
    }

    // foreign_keys: se activa por conexion y se verifica por lectura.
    {
        QSqlQuery q(db);
        if (auto r = sqlite::ejecutarDirecto(q, QStringLiteral("PRAGMA foreign_keys = ON"),
                                             u"pragma foreign_keys");
            !r) {
            return r;
        }
    }
    auto fk = leerPragma(db, QStringLiteral("foreign_keys"));
    if (!fk) {
        return ResultadoExito::fallo(std::move(fk).error());
    }
    if (fk.valor().toInt() != 1) {
        return falloAlmacenamiento(QStringLiteral("PRAGMA foreign_keys no quedo activo"));
    }

    // busy_timeout.
    {
        QSqlQuery q(db);
        if (auto r = sqlite::ejecutarDirecto(
                q, QStringLiteral("PRAGMA busy_timeout = %1").arg(m_opciones.busyTimeoutMs),
                u"pragma busy_timeout");
            !r) {
            return r;
        }
    }
    auto busy = leerPragma(db, QStringLiteral("busy_timeout"));
    if (!busy) {
        return ResultadoExito::fallo(std::move(busy).error());
    }
    if (busy.valor().toInt() != m_opciones.busyTimeoutMs) {
        return falloAlmacenamiento(QStringLiteral("PRAGMA busy_timeout no quedo aplicado"));
    }

    // WAL: solo se verifica; se habilita en inicializarBaseSqlite().
    if (m_opciones.exigirWal) {
        auto modo = leerPragma(db, QStringLiteral("journal_mode"));
        if (!modo) {
            return ResultadoExito::fallo(std::move(modo).error());
        }
        if (modo.valor().toString().toLower() != QStringLiteral("wal")) {
            return falloAlmacenamiento(
                QStringLiteral("la base no esta en journal_mode=WAL (no inicializada)"));
        }
    }
    return ResultadoExito::exito(Exito{});
}

void SqliteConnectionProvider::cerrarConexionDelHiloActual()
{
    QString nombre;
    bool transaccion = false;
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_conexiones.constFind(QThread::currentThread());
        if (it == m_conexiones.constEnd()) {
            return;
        }
        nombre = it->nombre;
        transaccion = it->transaccionActiva;
        m_conexiones.erase(it);
    }
    if (transaccion) {
        QSqlDatabase db = QSqlDatabase::database(nombre, false);
        QSqlQuery q(db);
        q.exec(QStringLiteral("ROLLBACK"));
    }
    cerrarYRetirar(nombre);
}

void SqliteConnectionProvider::cerrarYRetirar(const QString& nombre)
{
    {
        QSqlDatabase db = QSqlDatabase::database(nombre, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(nombre);
}

bool SqliteConnectionProvider::tieneConexionEnHiloActual() const
{
    QMutexLocker lock(&m_mutex);
    return m_conexiones.contains(QThread::currentThread());
}

QString SqliteConnectionProvider::nombreConexionHiloActual() const
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_conexiones.constFind(QThread::currentThread());
    return it == m_conexiones.constEnd() ? QString() : it->nombre;
}

int SqliteConnectionProvider::conexionesAbiertas() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<int>(m_conexiones.size());
}

bool SqliteConnectionProvider::transaccionActiva() const
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_conexiones.constFind(QThread::currentThread());
    return it != m_conexiones.constEnd() && it->transaccionActiva;
}

void SqliteConnectionProvider::marcarTransaccion(bool activa)
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_conexiones.find(QThread::currentThread());
    if (it != m_conexiones.end()) {
        it->transaccionActiva = activa;
    }
}

Resultado<QString, ErrorPersistencia> SqliteConnectionProvider::habilitarWal()
{
    using R = Resultado<QString, ErrorPersistencia>;
    auto db = conexion();
    if (!db) {
        return R::fallo(std::move(db).error());
    }
    QSqlDatabase handle = db.valor();
    auto modo = leerPragma(handle, QStringLiteral("journal_mode = WAL"));
    if (!modo) {
        return R::fallo(std::move(modo).error());
    }
    const QString resultado = modo.valor().toString().toLower();
    if (resultado != QStringLiteral("wal")) {
        return R::fallo(ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento,
                                              QStringLiteral("no se pudo habilitar WAL")));
    }
    return R::exito(resultado);
}

Resultado<QString, ErrorPersistencia> SqliteConnectionProvider::versionSqlite()
{
    using R = Resultado<QString, ErrorPersistencia>;
    auto db = conexion();
    if (!db) {
        return R::fallo(std::move(db).error());
    }
    QSqlDatabase handle = db.valor();
    QSqlQuery q(handle);
    if (auto r = sqlite::ejecutarDirecto(q, QStringLiteral("SELECT sqlite_version()"),
                                         u"sqlite_version");
        !r) {
        return R::fallo(std::move(r).error());
    }
    if (!q.next()) {
        return R::fallo(ErrorPersistencia::de(ErrorPersistencia::Tipo::Interno,
                                              QStringLiteral("sqlite_version sin valor")));
    }
    return R::exito(q.value(0).toString());
}

} // namespace satcfdi
