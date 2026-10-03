#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include "domain/common/TimestampUtc.h"
#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"

#include <QHash>
#include <QMetaType>
#include <QRegularExpression>
#include <QStringList>

namespace satcfdi::sqlite {

namespace {

// Codigos primarios de SQLite (sqlite3.h) usados en la traduccion.
constexpr int kSqliteError = 1;
constexpr int kSqlitePerm = 3;
constexpr int kSqliteBusy = 5;
constexpr int kSqliteLocked = 6;
constexpr int kSqliteReadOnly = 8;
constexpr int kSqliteIoErr = 10;
constexpr int kSqliteCorrupt = 11;
constexpr int kSqliteFull = 13;
constexpr int kSqliteCantOpen = 14;
constexpr int kSqliteProtocol = 15;
constexpr int kSqliteConstraint = 19;
constexpr int kSqliteNotADb = 26;

// Extendidos de SQLITE_CONSTRAINT.
constexpr int kConstraintCheck = 275;
constexpr int kConstraintForeignKey = 787;
constexpr int kConstraintNotNull = 1299;
constexpr int kConstraintPrimaryKey = 1555;
constexpr int kConstraintUnique = 2067;

const QString kIndiceDedup = QStringLiteral("ux_solicitud_masiva_dedup_bloqueante");

// Columnas de un UNIQUE fallido (texto de sqlite3_errmsg) -> indice con nombre.
QString indicePorColumnas(const QString& columnas)
{
    static const QHash<QString, QString> kMapa = {
        {QStringLiteral("perfil_sat.rfc"), QStringLiteral("ux_perfil_sat_rfc_vigente")},
        {QStringLiteral("credencial_sat.perfil_sat_id"), QStringLiteral("ux_credencial_sat_perfil")},
        {QStringLiteral("solicitud_masiva.id_solicitud_sat"),
         QStringLiteral("ux_solicitud_masiva_id_solicitud_sat")},
        {QStringLiteral("solicitud_masiva.dedup_key"), kIndiceDedup},
        {QStringLiteral("paquete_solicitud.solicitud_masiva_id, paquete_solicitud.id_paquete_sat"),
         QStringLiteral("ux_paquete_solicitud_id_paquete_sat")},
        {QStringLiteral("schema_migrations.version"), QStringLiteral("pk_schema_migrations")},
    };
    const auto it = kMapa.constFind(columnas);
    if (it != kMapa.constEnd()) {
        return it.value();
    }
    // Clave primaria TEXT `id` (o INTEGER id de configuracion_app).
    static const QRegularExpression kPk(QStringLiteral("^([a-z_]+)\\.id$"));
    const QRegularExpressionMatch m = kPk.match(columnas);
    if (m.hasMatch()) {
        return QStringLiteral("pk_") + m.captured(1);
    }
    // Forma `index 'nombre'` (indices de expresion).
    static const QRegularExpression kIndice(QStringLiteral("^index '([A-Za-z0-9_]+)'$"));
    const QRegularExpressionMatch mi = kIndice.match(columnas);
    if (mi.hasMatch()) {
        return mi.captured(1);
    }
    return {};
}

ErrorPersistencia errorRestriccion(int extendido, const QString& texto, QStringView contexto)
{
    static const QString kUnique = QStringLiteral("UNIQUE constraint failed: ");
    static const QString kCheck = QStringLiteral("CHECK constraint failed: ");
    static const QString kNotNull = QStringLiteral("NOT NULL constraint failed: ");

    const qsizetype posUnique = texto.indexOf(kUnique);
    if (posUnique >= 0 || extendido == kConstraintUnique || extendido == kConstraintPrimaryKey) {
        const QString columnas =
            posUnique >= 0 ? texto.mid(posUnique + kUnique.size()).trimmed() : QString();
        const QString indice = indicePorColumnas(columnas);
        if (indice == kIndiceDedup) {
            return ErrorPersistencia::de(ErrorPersistencia::Tipo::DedupBloqueado,
                                         contexto.toString() + QStringLiteral(": duplicado bloqueante"),
                                         indice);
        }
        return ErrorPersistencia::de(ErrorPersistencia::Tipo::Unicidad,
                                     contexto.toString() + QStringLiteral(": restriccion de unicidad violada"),
                                     indice);
    }

    const qsizetype posCheck = texto.indexOf(kCheck);
    if (posCheck >= 0 || extendido == kConstraintCheck) {
        QString nombre;
        if (posCheck >= 0) {
            const QString resto = texto.mid(posCheck + kCheck.size()).trimmed();
            static const QRegularExpression kNombre(QStringLiteral("^ck_[a-z0-9_]+$"));
            if (kNombre.match(resto).hasMatch()) {
                nombre = resto;
            }
        }
        return ErrorPersistencia::de(ErrorPersistencia::Tipo::Integridad,
                                     contexto.toString() + QStringLiteral(": restriccion CHECK violada"),
                                     nombre);
    }

    if (texto.contains(QStringLiteral("FOREIGN KEY constraint failed"))
        || extendido == kConstraintForeignKey) {
        return ErrorPersistencia::de(ErrorPersistencia::Tipo::Integridad,
                                     contexto.toString() + QStringLiteral(": clave foranea violada"),
                                     QStringLiteral("foreign_key"));
    }

    const qsizetype posNotNull = texto.indexOf(kNotNull);
    if (posNotNull >= 0 || extendido == kConstraintNotNull) {
        QString columna;
        if (posNotNull >= 0) {
            const QString resto = texto.mid(posNotNull + kNotNull.size()).trimmed();
            static const QRegularExpression kCol(QStringLiteral("^[a-z_]+\\.[a-z_]+$"));
            if (kCol.match(resto).hasMatch()) {
                columna = QStringLiteral("not_null:") + resto;
            }
        }
        return ErrorPersistencia::de(ErrorPersistencia::Tipo::Integridad,
                                     contexto.toString() + QStringLiteral(": columna obligatoria nula"),
                                     columna);
    }

    return ErrorPersistencia::de(ErrorPersistencia::Tipo::Integridad,
                                 contexto.toString() + QStringLiteral(": restriccion violada"));
}

} // namespace

ErrorPersistencia traducirError(const QSqlError& error, QStringView contexto)
{
    bool ok = false;
    const int nativo = error.nativeErrorCode().toInt(&ok);
    const int primario = ok ? (nativo & 0xFF) : -1;
    const QString texto = error.databaseText();

    ErrorPersistencia resultado;
    const bool pareceRestriccion = texto.contains(QStringLiteral("constraint failed"));
    if (primario == kSqliteConstraint || (!ok && pareceRestriccion)) {
        resultado = errorRestriccion(nativo, texto, contexto);
    } else {
        switch (primario) {
        case kSqliteBusy:
        case kSqliteLocked:
            resultado = ErrorPersistencia::de(ErrorPersistencia::Tipo::Ocupado,
                                              contexto.toString() + QStringLiteral(": base ocupada"));
            break;
        case kSqlitePerm:
        case kSqliteReadOnly:
        case kSqliteIoErr:
        case kSqliteCorrupt:
        case kSqliteFull:
        case kSqliteCantOpen:
        case kSqliteProtocol:
        case kSqliteNotADb:
            resultado = ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento,
                                              contexto.toString() + QStringLiteral(": error de almacenamiento"));
            break;
        case kSqliteError:
        default:
            if (error.type() == QSqlError::ConnectionError) {
                resultado = ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento,
                                                  contexto.toString() + QStringLiteral(": error de conexion"));
            } else {
                resultado = ErrorPersistencia::de(ErrorPersistencia::Tipo::Interno,
                                                  contexto.toString() + QStringLiteral(": error SQL inesperado"));
            }
            break;
        }
    }
    if (ok) {
        resultado.codigoNativo = nativo;
    }
    return resultado;
}

Resultado<QSqlDatabase, ErrorPersistencia> conexionLectura(SqliteConnectionProvider& proveedor)
{
    return proveedor.conexion();
}

Resultado<QSqlDatabase, ErrorPersistencia> conexionEscritura(SqliteConnectionProvider& proveedor,
                                                             QStringView contexto)
{
    if (!proveedor.transaccionActiva()) {
        return Resultado<QSqlDatabase, ErrorPersistencia>::fallo(escrituraSinTransaccion(contexto));
    }
    return proveedor.conexion();
}

ErrorPersistencia filaIlegible(QStringView tabla, QStringView columna)
{
    return ErrorPersistencia::de(ErrorPersistencia::Tipo::Interno,
                                 QStringLiteral("valor ilegible en %1.%2").arg(tabla, columna));
}

ErrorPersistencia escrituraSinTransaccion(QStringView contexto)
{
    return ErrorPersistencia::de(ErrorPersistencia::Tipo::Transaccion,
                                 contexto.toString() + QStringLiteral(": escritura sin UnitOfWork activo"));
}

Resultado<Exito, ErrorPersistencia> preparar(QSqlQuery& query, const QString& sql,
                                             QStringView contexto)
{
    if (!query.prepare(sql)) {
        return Resultado<Exito, ErrorPersistencia>::fallo(traducirError(query.lastError(), contexto));
    }
    return Resultado<Exito, ErrorPersistencia>::exito(Exito{});
}

Resultado<Exito, ErrorPersistencia> ejecutar(QSqlQuery& query, QStringView contexto)
{
    if (!query.exec()) {
        return Resultado<Exito, ErrorPersistencia>::fallo(traducirError(query.lastError(), contexto));
    }
    return Resultado<Exito, ErrorPersistencia>::exito(Exito{});
}

Resultado<Exito, ErrorPersistencia> ejecutarDirecto(QSqlQuery& query, const QString& sql,
                                                    QStringView contexto)
{
    if (!query.exec(sql)) {
        return Resultado<Exito, ErrorPersistencia>::fallo(traducirError(query.lastError(), contexto));
    }
    return Resultado<Exito, ErrorPersistencia>::exito(Exito{});
}

QVariant texto(const QString& valor)
{
    return QVariant(valor);
}

QVariant textoOpcional(const std::optional<QString>& valor)
{
    return valor ? QVariant(*valor) : QVariant(QMetaType::fromType<QString>());
}

QVariant instante(const QDateTime& valor)
{
    return QVariant(timestamp::aTexto(valor));
}

QVariant instanteOpcional(const std::optional<QDateTime>& valor)
{
    return valor ? instante(*valor) : QVariant(QMetaType::fromType<QString>());
}

QVariant entero(qint64 valor)
{
    return QVariant(valor);
}

QVariant booleano(bool valor)
{
    return QVariant(valor ? 1 : 0);
}

std::optional<QString> leerTextoOpcional(const QVariant& valor)
{
    if (valor.isNull()) {
        return std::nullopt;
    }
    return valor.toString();
}

bool leerInstante(const QVariant& valor, QDateTime& destino)
{
    if (valor.isNull()) {
        return false;
    }
    const std::optional<QDateTime> leido = timestamp::desdeTexto(valor.toString());
    if (!leido) {
        return false;
    }
    destino = *leido;
    return true;
}

bool leerInstanteOpcional(const QVariant& valor, std::optional<QDateTime>& destino)
{
    if (valor.isNull()) {
        destino.reset();
        return true;
    }
    QDateTime leido;
    if (!leerInstante(valor, leido)) {
        return false;
    }
    destino = leido;
    return true;
}

} // namespace satcfdi::sqlite
