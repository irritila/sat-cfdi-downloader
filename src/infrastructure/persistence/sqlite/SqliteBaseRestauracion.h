#pragma once

#include "infrastructure/persistence/sqlite/SqliteTipos.h"
#include "ports/respaldo/Restauracion.h"

#include <QList>

#include <optional>

namespace satcfdi {

// T025.5 (D3, D13, ADR 0016, ADR 0022): prepara la copia de SQLite extraida a
// un staging de restauracion. Conexion PROPIA (SqliteConnectionProvider sin
// WAL) creada, usada, cerrada y retirada dentro de preparar(), en el hilo que
// llama (nunca el grafico). Orden:
//   1. schema_migrations presente; MAX(version) > ultima conocida ->
//      EsquemaMasNuevo SIN escribir nada (el runner tambien lo rechazaria).
//   2. integrity_check = "ok" antes de tocarla.
//   3. SqliteMigrationRunner con las migraciones conocidas (un esquema
//      anterior se migra; cada migracion en su transaccion).
//   4. BEGIN IMMEDIATE: DELETE aviso_vencimiento_efirma y credencial_sat
//      (D3: la e.firma no se restaura; el perfil queda SinCredencial).
//   5. integrity_check = "ok" y foreign_key_check sin filas.
//   6. Conteos, paquetes Descargado con su huella del indice (para el mtime)
//      y journal_mode=DELETE.
class SqliteBaseRestauracion final : public respaldo::BaseRestauracion {
public:
    // Vacio = migraciones embebidas (produccion). Las pruebas inyectan la
    // lista para simular una app con menos versiones.
    explicit SqliteBaseRestauracion(std::optional<QList<MigracionSql>> migraciones = std::nullopt);

    int versionMaximaConocida() const override;
    Resultado<respaldo::InformeBaseRestauracion, respaldo::ErrorRestauracion>
    preparar(const QString& rutaBase) override;

private:
    QList<MigracionSql> m_migraciones;
    bool m_migracionesValidas = false;
};

} // namespace satcfdi
