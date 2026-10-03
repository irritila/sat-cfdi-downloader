#pragma once

// Utilidades de pruebas de infraestructura SQLite (T003). Solo para
// satcfdi_infrastructure_tests: usan el proveedor interno y QSql directamente
// para fixtures y verificaciones que los puertos no exponen.

#include "domain/common/TimestampUtc.h"
#include "domain/common/UuidCanonico.h"
#include "domain/solicitudes/DedupKey.h"
#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>

#include <optional>

namespace satcfdi::pruebas {

inline const QString kRfcPerfil = QStringLiteral("AAA010101AAA");
inline const QString kRfcOtro = QStringLiteral("BBB020202BBB");
inline const QString kAhora = QStringLiteral("2026-10-03T12:00:00.000Z");

inline QString rutaBase(const QTemporaryDir& dir)
{
    return dir.filePath(QStringLiteral("satcfdi.sqlite3"));
}

inline QByteArray huellaArchivo(const QString& ruta)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
}

inline QByteArray leerArchivo(const QString& ruta)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}

// Ejecuta SQL crudo en la conexion del hilo actual del proveedor. Devuelve el
// texto de error vacio si tuvo exito.
inline QString ejecutarSql(SqliteConnectionProvider& proveedor, const QString& sql)
{
    auto conexion = proveedor.conexion();
    if (!conexion) {
        return QStringLiteral("sin conexion: ") + conexion.error().mensaje;
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        return q.lastError().text();
    }
    return {};
}

// Primer valor de la primera fila (QVariant invalido si no hay filas o falla).
inline QVariant escalar(SqliteConnectionProvider& proveedor, const QString& sql)
{
    auto conexion = proveedor.conexion();
    if (!conexion) {
        return {};
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) {
        return {};
    }
    return q.value(0);
}

// Todas las filas de la primera columna como texto.
inline QStringList columna(SqliteConnectionProvider& proveedor, const QString& sql)
{
    QStringList valores;
    auto conexion = proveedor.conexion();
    if (!conexion) {
        return valores;
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        return valores;
    }
    while (q.next()) {
        valores.append(q.value(0).isNull() ? QStringLiteral("<NULL>") : q.value(0).toString());
    }
    return valores;
}

inline QString citar(const QString& valor)
{
    QString escapado = valor;
    escapado.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + escapado + QLatin1Char('\'');
}

inline QString citarOpcional(const std::optional<QString>& valor)
{
    return valor ? citar(*valor) : QStringLiteral("NULL");
}

inline QString insertarPerfilFixture(SqliteConnectionProvider& proveedor, const QString& id,
                                     const QString& rfc)
{
    return ejecutarSql(
        proveedor,
        QStringLiteral("INSERT INTO perfil_sat (id, rfc, nombre, activo, creado_en, actualizado_en) "
                       "VALUES (%1, %2, 'Perfil fixture', 1, %3, %3)")
            .arg(citar(id), citar(rfc), citar(kAhora)));
}

// Solicitud fixture (emitidos) en el estado indicado, coherente con los CHECK
// de T001. `estadoSat` vacio = NULL.
struct SolicitudFixture {
    QString id;
    QString perfilId;
    DedupKey clave;
    QString estadoLocal = QStringLiteral("Creada");
    QString estadoSat;
    bool eliminada = false;
    QString creadaEn = kAhora;
};

inline SolicitudFixture fixture(const QString& perfilId, const DedupKey& clave,
                                const QString& estadoLocal = QStringLiteral("Creada"),
                                const QString& estadoSat = {}, bool eliminada = false)
{
    SolicitudFixture s;
    s.id = uuid::generarCanonico();
    s.perfilId = perfilId;
    s.clave = clave;
    s.estadoLocal = estadoLocal;
    s.estadoSat = estadoSat;
    s.eliminada = eliminada;
    return s;
}

inline QString insertarSolicitudFixture(SqliteConnectionProvider& proveedor,
                                        const SolicitudFixture& s)
{
    const bool enviada = s.estadoLocal == QStringLiteral("Enviada");
    const bool conIntento = s.estadoLocal != QStringLiteral("Creada")
                            && s.estadoLocal != QStringLiteral("EnvioFallido");
    const bool conEstadoSat = !s.estadoSat.isEmpty();
    const QString idSat = enviada ? citar(s.id.toUpper()) : QStringLiteral("NULL");
    return ejecutarSql(
        proveedor,
        QStringLiteral(
            "INSERT INTO solicitud_masiva (id, perfil_sat_id, id_solicitud_sat, tipo_cfdi, "
            "operacion_sat, rfc_solicitante, rfc_emisor, fecha_inicial_sat, fecha_final_sat, "
            "dedup_key, estado_local, cod_estatus_solicitud, estado_solicitud_sat, creada_en, "
            "envio_iniciado_en, enviada_en, ultima_verificacion_en, eliminado_en) VALUES "
            "(%1, %2, %3, 'emitidos', 'SolicitaDescargaEmitidos', %4, %4, "
            "'2026-01-01T00:00:00', '2026-01-31T23:59:59', %5, %6, %7, %8, %9, %10, %11, "
            "%12, %13)")
            .arg(citar(s.id), citar(s.perfilId), idSat, citar(kRfcPerfil), citar(s.clave.texto()),
                 citar(s.estadoLocal), enviada ? QStringLiteral("'5000'") : QStringLiteral("NULL"),
                 conEstadoSat ? citar(s.estadoSat) : QStringLiteral("NULL"), citar(s.creadaEn))
            .arg(conIntento ? citar(kAhora) : QStringLiteral("NULL"),
                 enviada ? citar(kAhora) : QStringLiteral("NULL"),
                 conEstadoSat ? citar(kAhora) : QStringLiteral("NULL"),
                 s.eliminada ? citar(kAhora) : QStringLiteral("NULL")));
}

// Paquete fixture en el estado indicado, coherente con los CHECK de T001.
inline QString insertarPaqueteFixture(SqliteConnectionProvider& proveedor, const QString& id,
                                      const QString& solicitudId, const QString& idPaqueteSat,
                                      const QString& estado, bool eliminado = false)
{
    const bool descargado = estado == QStringLiteral("Descargado");
    const bool vencido = estado == QStringLiteral("Vencido");
    const bool descargando = estado == QStringLiteral("Descargando");
    return ejecutarSql(
        proveedor,
        QStringLiteral(
            "INSERT INTO paquete_solicitud (id, solicitud_masiva_id, id_paquete_sat, "
            "estado_descarga, ruta_local, disponible_en, descarga_iniciada_en, descargado_en, "
            "vencido_en, motivo_vencimiento, origen_vencimiento, eliminado_en) VALUES "
            "(%1, %2, %3, %4, %5, %6, %7, %8, %9, %10, %11, %12)")
            .arg(citar(id), citar(solicitudId), citar(idPaqueteSat), citar(estado),
                 descargado ? QStringLiteral("'paquetes/p.zip'") : QStringLiteral("NULL"),
                 citar(kAhora),
                 descargando || descargado ? citar(kAhora) : QStringLiteral("NULL"),
                 descargado ? citar(kAhora) : QStringLiteral("NULL"),
                 vencido ? citar(kAhora) : QStringLiteral("NULL"))
            .arg(vencido ? QStringLiteral("'paquete_expirado'") : QStringLiteral("NULL"),
                 vencido ? QStringLiteral("'SAT'") : QStringLiteral("NULL"),
                 eliminado ? citar(kAhora) : QStringLiteral("NULL")));
}

} // namespace satcfdi::pruebas
