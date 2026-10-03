#pragma once

#include "domain/common/Resultado.h"
#include "domain/solicitudes/DedupKey.h"
#include "domain/solicitudes/Duplicados.h"
#include "domain/solicitudes/SolicitudId.h"
#include "domain/solicitudes/SolicitudPersistida.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/persistence/Exito.h"

#include <QDateTime>
#include <QList>

#include <optional>

namespace satcfdi {

// Repositorio de solicitudes masivas locales (solicitud_masiva). Sincrono.
//
// Hilo: solo desde tareas de PersistenceDispatcher. "Visible" =
// eliminado_en IS NULL (escrito literal en SQL para usar indices parciales).
class SolicitudMasivaRepository {
public:
    virtual ~SolicitudMasivaRepository() = default;

    // Solicitudes visibles, creada_en DESC, id DESC. Tx: opcional.
    // Errores: Almacenamiento, Interno (fila ilegible).
    virtual Resultado<QList<SolicitudPersistida>, ErrorPersistencia> listarVisibles() = 0;

    // Solicitud visible por id; nullopt si no existe o esta eliminada.
    // Tx: opcional. Errores: Almacenamiento, Interno.
    virtual Resultado<std::optional<SolicitudPersistida>, ErrorPersistencia>
    obtenerVisible(const SolicitudId& id) = 0;

    // Clasifica duplicados de `clave` INCLUYENDO filas eliminadas: lee las
    // coincidencias (creada_en DESC, id DESC) y, para las no eliminadas con
    // estado SAT Terminada, el estado de sus paquetes no eliminados; aplica
    // domain clasificarCoincidencias(). No reimplementa la matriz en SQL.
    // Tx: requerida (BEGIN IMMEDIATE) cuando precede a insertarCreada();
    // opcional para evaluarDuplicado() previo a la UI.
    // Errores: Almacenamiento, Ocupado, Interno.
    virtual Resultado<EvaluacionDuplicado, ErrorPersistencia>
    clasificarDuplicado(const DedupKey& clave) = 0;

    // INSERT de la solicitud con estado_local = 'Creada' (ver
    // SolicitudNuevaPersistida). Tx: requerida.
    // Errores: DedupBloqueado (ux_solicitud_masiva_dedup_bloqueante);
    // Unicidad (id duplicado); Integridad (CHECK/FK, p. ej. perfil inexistente);
    // Ocupado; Almacenamiento.
    virtual Resultado<Exito, ErrorPersistencia>
    insertarCreada(const SolicitudNuevaPersistida& solicitud) = 0;

    // UPDATE eliminado_en = `eliminadoEn` WHERE id = ? AND eliminado_en IS NULL.
    // true si cambio una fila; false si no existe o ya estaba eliminada
    // (idempotente). Tx: requerida (junto con paquetes y logs).
    // Errores: Ocupado, Almacenamiento.
    virtual Resultado<bool, ErrorPersistencia>
    marcarEliminadaVisible(const SolicitudId& id, const QDateTime& eliminadoEn) = 0;
};

} // namespace satcfdi
