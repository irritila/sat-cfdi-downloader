#pragma once

#include "ports/respaldo/FuenteRespaldo.h"

namespace satcfdi {

class SqlitePersistencia;

// T025.2 (D10, ADR 0016): copia (SqliteCopiaRespaldo) y lectura
// (SqliteLectorCopiaRespaldo) de la base para el respaldo. Sin tipos QSql* en
// el header.
//
// - copiarA(): `VACUUM INTO ?` (destino como parametro) con la conexion del
//   HILO QUE LLAMA, que debe ser el del OperacionExecutor (su conexion y su
//   busy_timeout). En WAL es una transaccion de lectura: los escritores de
//   otras conexiones siguen. Rechaza llamarse con una transaccion abierta en
//   ese hilo. Deja la copia con permisos 0600.
// - leer(): conexion PROPIA de nombre unico, creada, usada, cerrada y retirada
//   dentro de la llamada (cualquier hilo de trabajo). Fija journal_mode=DELETE
//   en la copia, exige integrity_check = "ok" y lee schema_migrations y los
//   paquetes Descargado con ruta (no eliminados, de solicitudes no
//   eliminadas).
class SqliteCopiaRespaldo final : public respaldo::CopiaBaseDatos {
public:
    explicit SqliteCopiaRespaldo(SqlitePersistencia& persistencia) : m_persistencia(persistencia) {}

    Resultado<Exito, respaldo::ErrorCaptura> copiarA(const QString& rutaDestino) override;

private:
    SqlitePersistencia& m_persistencia;
};

// Sin estado ni referencias: se comparte (shared_ptr) con las continuaciones
// de captura en curso, que pueden terminar despues del composition root.
class SqliteLectorCopiaRespaldo final : public respaldo::LectorCopiaBaseDatos {
public:
    Resultado<respaldo::ContenidoCopia, respaldo::ErrorCaptura> leer(const QString& rutaCopia) override;
};

} // namespace satcfdi
