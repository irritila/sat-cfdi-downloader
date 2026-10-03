#pragma once

#include "domain/common/Resultado.h"
#include "domain/paquetes/PaquetePersistido.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QDateTime>
#include <QHash>
#include <QList>

namespace satcfdi {

// Repositorio de paquetes por solicitud (paquete_solicitud). Sincrono.
// Sin insercion productiva en T003 (T007/T009); las pruebas usan fixtures.
//
// Hilo: solo desde tareas de PersistenceDispatcher. "Visible" =
// eliminado_en IS NULL.
class PaqueteSolicitudRepository {
public:
    virtual ~PaqueteSolicitudRepository() = default;

    // Paquetes visibles de la solicitud, disponible_en ASC, id_paquete_sat ASC.
    // Tx: opcional. Errores: Almacenamiento, Interno.
    virtual Resultado<QList<PaquetePersistido>, ErrorPersistencia>
    listarVisiblesPorSolicitud(const SolicitudId& solicitudId) = 0;

    // Conteo de paquetes visibles agrupado por solicitud (una consulta; para
    // SolicitudResumen::totalPaquetes en la lista). Las solicitudes sin
    // paquetes no aparecen. Tx: opcional. Errores: Almacenamiento, Interno.
    virtual Resultado<QHash<SolicitudId, int>, ErrorPersistencia>
    contarVisiblesPorSolicitud() = 0;

    // UPDATE eliminado_en = `eliminadoEn` de los paquetes visibles de la
    // solicitud. Devuelve filas afectadas (0 es valido). Tx: requerida.
    // Errores: Ocupado, Almacenamiento.
    virtual Resultado<int, ErrorPersistencia>
    marcarEliminadosPorSolicitud(const SolicitudId& solicitudId, const QDateTime& eliminadoEn) = 0;
};

} // namespace satcfdi
