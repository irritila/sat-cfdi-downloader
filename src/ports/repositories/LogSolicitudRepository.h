#pragma once

#include "domain/common/Resultado.h"
#include "domain/logs/LogSolicitud.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/persistence/Exito.h"

#include <QDateTime>
#include <QList>

namespace satcfdi {

// Repositorio de logs saneados por solicitud (log_solicitud). Sincrono.
// Solo acepta LogEntradaSaneada; no sanea ni filtra secretos.
//
// Hilo: solo desde tareas de PersistenceDispatcher. "Visible" =
// eliminado_en IS NULL.
class LogSolicitudRepository {
public:
    virtual ~LogSolicitudRepository() = default;

    // Logs visibles de la solicitud, creado_en ASC, id ASC. Tx: opcional.
    // Errores: Almacenamiento, Interno.
    virtual Resultado<QList<LogPersistido>, ErrorPersistencia>
    listarVisiblesPorSolicitud(const SolicitudId& solicitudId) = 0;

    // INSERT con eliminado_en NULL. Tx: requerida (misma transaccion que el
    // cambio que registra). Errores: Integridad (FK solicitud, CHECK de
    // catalogos, ck_log_codigo_sat, JSON no objeto); Unicidad (id);
    // Ocupado; Almacenamiento.
    virtual Resultado<Exito, ErrorPersistencia> agregar(const LogEntradaSaneada& entrada) = 0;

    // UPDATE eliminado_en = `eliminadoEn` de los logs visibles de la
    // solicitud. Devuelve filas afectadas. Tx: requerida.
    // Errores: Ocupado, Almacenamiento.
    virtual Resultado<int, ErrorPersistencia>
    marcarEliminadosPorSolicitud(const SolicitudId& solicitudId, const QDateTime& eliminadoEn) = 0;
};

} // namespace satcfdi
