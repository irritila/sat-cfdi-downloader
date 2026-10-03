#pragma once

#include <QString>

#include <optional>
#include <utility>

namespace satcfdi {

// Error tipado de los puertos de persistencia (DA3). Infraestructura traduce
// QSqlError/codigos SQLite a esta forma; ningun tipo QSql* cruza la frontera.
//
// `mensaje`: texto tecnico breve y saneado. NUNCA contiene SQL, valores de
// fila, rutas de usuario ni secretos; puede contener nombres de tabla.
// `restriccion`: nombre del indice/constraint violado cuando se identifica
// (p. ej. "ux_perfil_sat_rfc_vigente", "ck_solicitud_rfcs"), sin valores.
// `codigoNativo`: codigo extendido de SQLite si existe, solo diagnostico.
struct ErrorPersistencia {
    enum class Tipo {
        // Apertura, E/S, disco lleno, driver ausente, base corrupta.
        Almacenamiento,
        // SQLITE_BUSY/LOCKED tras agotar busy_timeout.
        Ocupado,
        // CHECK, FOREIGN KEY o NOT NULL violado.
        Integridad,
        // UNIQUE/PRIMARY KEY violado en un indice distinto de dedup.
        // Ej.: ux_perfil_sat_rfc_vigente -> RFC vigente duplicado.
        Unicidad,
        // UNIQUE violado en ux_solicitud_masiva_dedup_bloqueante (DM6).
        // Aplicacion lo traduce a ErrorCrear::DedupBloqueado.
        DedupBloqueado,
        // La fila esperada no existe (solo donde el contrato lo indique; las
        // lecturas por id devuelven std::optional vacio, no este error).
        NoEncontrado,
        // Migracion fallida, version futura o SQLite/JSON1 insuficiente (DM4).
        Migracion,
        // Uso invalido de UnitOfWork: begin anidado, commit sin begin, etc.
        Transaccion,
        // Dato persistido ilegible (enum desconocido, timestamp invalido) o
        // fallo inesperado.
        Interno,
    };

    Tipo tipo = Tipo::Interno;
    QString mensaje;
    QString restriccion;
    std::optional<int> codigoNativo;

    static ErrorPersistencia de(Tipo tipo, QString mensaje, QString restriccion = {})
    {
        ErrorPersistencia e;
        e.tipo = tipo;
        e.mensaje = std::move(mensaje);
        e.restriccion = std::move(restriccion);
        return e;
    }
};

} // namespace satcfdi
