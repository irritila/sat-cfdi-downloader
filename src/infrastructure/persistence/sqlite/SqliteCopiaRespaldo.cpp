#include "infrastructure/persistence/sqlite/SqliteCopiaRespaldo.h"

#include "domain/common/UuidCanonico.h"
#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

namespace satcfdi {

using respaldo::ContenidoCopia;
using respaldo::ErrorCaptura;
using C = ErrorCaptura::Categoria;

namespace {

ErrorCaptura error(C c, const char* diagnostico)
{
    return ErrorCaptura::de(c, QString::fromLatin1(diagnostico));
}

constexpr QFileDevice::Permissions kSoloDueno = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

} // namespace

Resultado<Exito, ErrorCaptura> SqliteCopiaRespaldo::copiarA(const QString& rutaDestino)
{
    using R = Resultado<Exito, ErrorCaptura>;
    if (!QDir::isAbsolutePath(rutaDestino) || QFileInfo::exists(rutaDestino)) {
        return R::fallo(error(C::Copia, "copia.destino"));
    }
    SqliteConnectionProvider& proveedor = m_persistencia.proveedor();
    if (proveedor.transaccionActiva()) {
        return R::fallo(error(C::Copia, "copia.transaccion_abierta"));
    }
    auto conexion = proveedor.conexion();
    if (!conexion) {
        return R::fallo(error(C::Copia, "copia.conexion"));
    }
    {
        QSqlQuery q(conexion.valor());
        if (!q.prepare(QStringLiteral("VACUUM INTO ?"))) {
            return R::fallo(error(C::Copia, "copia.preparar"));
        }
        q.addBindValue(QDir::toNativeSeparators(rutaDestino));
        if (!q.exec()) {
            QFile::remove(rutaDestino);
            return R::fallo(error(C::Copia, "copia.vacuum"));
        }
    }
    if (!QFile::setPermissions(rutaDestino, kSoloDueno)) {
        QFile::remove(rutaDestino);
        return R::fallo(error(C::Copia, "copia.permisos"));
    }
    return R::exito(Exito{});
}

Resultado<ContenidoCopia, ErrorCaptura> SqliteLectorCopiaRespaldo::leer(const QString& rutaCopia)
{
    using R = Resultado<ContenidoCopia, ErrorCaptura>;
    if (!QFileInfo(rutaCopia).isFile()) {
        return R::fallo(error(C::LecturaCopia, "copia.no_existe"));
    }
    const QString nombre = QStringLiteral("satcfdi-respaldo-copia-") + uuid::generarCanonico();
    std::optional<R> resultado;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), nombre);
        db.setDatabaseName(rutaCopia);
        if (!db.open()) {
            resultado = R::fallo(error(C::LecturaCopia, "copia.abrir"));
        } else {
            const auto ejecutar = [&db](const QString& sql, QSqlQuery& q) {
                q = QSqlQuery(db);
                return q.exec(sql);
            };
            QSqlQuery q(db);
            ContenidoCopia contenido;
            QStringList integridad;
            if (!ejecutar(QStringLiteral("PRAGMA journal_mode=DELETE"), q) || !q.next()
                || q.value(0).toString().compare(QLatin1String("delete"), Qt::CaseInsensitive) != 0) {
                resultado = R::fallo(error(C::LecturaCopia, "copia.journal"));
            } else if (!ejecutar(QStringLiteral("PRAGMA integrity_check"), q)) {
                resultado = R::fallo(error(C::LecturaCopia, "copia.integridad"));
            } else {
                while (q.next()) {
                    integridad.append(q.value(0).toString());
                }
                if (integridad != QStringList{QStringLiteral("ok")}) {
                    resultado = R::fallo(error(C::Integridad, "copia.integrity_check"));
                } else if (!ejecutar(QStringLiteral("SELECT COALESCE(MAX(version), 0) FROM schema_migrations"), q)
                           || !q.next()) {
                    resultado = R::fallo(error(C::LecturaCopia, "copia.esquema"));
                } else {
                    contenido.versionEsquema = q.value(0).toInt();
                    if (!ejecutar(QStringLiteral(
                                      "SELECT p.ruta_local FROM paquete_solicitud p "
                                      "JOIN solicitud_masiva s ON s.id = p.solicitud_masiva_id "
                                      "WHERE p.estado_descarga = 'Descargado' AND p.ruta_local IS NOT NULL "
                                      "AND p.eliminado_en IS NULL AND s.eliminado_en IS NULL "
                                      "ORDER BY p.ruta_local, p.id"),
                                  q)) {
                        resultado = R::fallo(error(C::LecturaCopia, "copia.paquetes"));
                    } else {
                        while (q.next()) {
                            contenido.rutasPaquetes.append(q.value(0).toString());
                        }
                        resultado = R::exito(std::move(contenido));
                    }
                }
            }
            q = QSqlQuery();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(nombre);
    return std::move(*resultado);
}

} // namespace satcfdi
