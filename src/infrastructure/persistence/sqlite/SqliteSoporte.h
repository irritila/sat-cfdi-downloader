#pragma once

#include "domain/common/Resultado.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/persistence/Exito.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringView>
#include <QVariant>

#include <optional>

// Utilidades INTERNAS de infraestructura SQLite: traduccion de errores,
// enlace de valores y lectura de columnas. No se exponen fuera de
// src/infrastructure.
namespace satcfdi {
class SqliteConnectionProvider;
}

namespace satcfdi::sqlite {

// Conexion del hilo actual para lecturas (con o sin transaccion).
Resultado<QSqlDatabase, ErrorPersistencia> conexionLectura(SqliteConnectionProvider& proveedor);
// Conexion del hilo actual para escrituras: exige UnitOfWork activo
// (Transaccion si no lo hay).
Resultado<QSqlDatabase, ErrorPersistencia> conexionEscritura(SqliteConnectionProvider& proveedor,
                                                             QStringView contexto);

// Traduce un QSqlError de QSQLITE a ErrorPersistencia sin exponer SQL ni
// valores de fila. `contexto` es una etiqueta tecnica (p. ej.
// "solicitud_masiva.insertar").
//
// - SQLITE_BUSY/LOCKED -> Ocupado.
// - UNIQUE sobre ux_solicitud_masiva_dedup_bloqueante -> DedupBloqueado.
// - Otros UNIQUE/PRIMARY KEY -> Unicidad (restriccion = indice ux_* o pk_<tabla>).
// - CHECK, FOREIGN KEY, NOT NULL -> Integridad (restriccion = ck_* si tiene
//   nombre, "foreign_key", o "not_null:<tabla>.<columna>").
// - E/S, disco lleno, corrupcion, apertura -> Almacenamiento.
// - Resto -> Interno.
ErrorPersistencia traducirError(const QSqlError& error, QStringView contexto);

// Error Interno para una fila con valores no reconocidos (enum/timestamp).
ErrorPersistencia filaIlegible(QStringView tabla, QStringView columna);

// Error Transaccion para una escritura sin UnitOfWork activo.
ErrorPersistencia escrituraSinTransaccion(QStringView contexto);

// prepare() + error traducido.
Resultado<Exito, ErrorPersistencia> preparar(QSqlQuery& query, const QString& sql,
                                             QStringView contexto);
// exec() de una consulta preparada + error traducido.
Resultado<Exito, ErrorPersistencia> ejecutar(QSqlQuery& query, QStringView contexto);
// exec(sql) directo (sin parametros) + error traducido.
Resultado<Exito, ErrorPersistencia> ejecutarDirecto(QSqlQuery& query, const QString& sql,
                                                    QStringView contexto);

// Valores para bindValue con el tipo declarado (texto/entero) y NULL tipado.
QVariant texto(const QString& valor);
QVariant textoOpcional(const std::optional<QString>& valor);
QVariant instante(const QDateTime& valor);                       // ISO Z con ms
QVariant instanteOpcional(const std::optional<QDateTime>& valor);
QVariant entero(qint64 valor);
QVariant booleano(bool valor);

// Lectura de columnas.
std::optional<QString> leerTextoOpcional(const QVariant& valor);
// false si el texto no tiene el formato yyyy-MM-ddTHH:mm:ss.zzzZ.
bool leerInstante(const QVariant& valor, QDateTime& destino);
bool leerInstanteOpcional(const QVariant& valor, std::optional<QDateTime>& destino);

} // namespace satcfdi::sqlite
