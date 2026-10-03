#pragma once

#include "domain/common/Resultado.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/perfiles/PerfilSat.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QList>
#include <QStringView>

#include <optional>

namespace satcfdi {

// Repositorio de perfiles SAT (perfil_sat). Sincrono.
//
// Hilo: solo desde tareas de PersistenceDispatcher; usa la conexion del hilo
// actual. No es QObject y no guarda estado entre llamadas salvo la conexion.
// Transaccion: las lecturas pueden ejecutarse dentro o fuera de UnitOfWork;
// las escrituras deben ir dentro de begin()/commit().
// "Visible" = eliminado_en IS NULL.
class PerfilSatRepository {
public:
    virtual ~PerfilSatRepository() = default;

    // INSERT con eliminado_en NULL. Devuelve la fila insertada.
    // Tx: requerida. Errores: Unicidad (restriccion ux_perfil_sat_rfc_vigente)
    // si el RFC ya esta vigente; Integridad (CHECK de id/rfc/nombre);
    // Ocupado; Almacenamiento.
    virtual Resultado<PerfilSat, ErrorPersistencia> insertar(const NuevoPerfilSat& perfil) = 0;

    // Perfil visible por id, activo o inactivo. nullopt si no existe o esta
    // eliminado. Tx: opcional. Errores: Almacenamiento, Interno.
    virtual Resultado<std::optional<PerfilSat>, ErrorPersistencia>
    obtener(const PerfilId& id) = 0;

    // Perfil visible (activo o inactivo) con ese RFC normalizado. Tx: opcional.
    // Errores: Almacenamiento, Interno.
    virtual Resultado<std::optional<PerfilSat>, ErrorPersistencia>
    obtenerVigentePorRfc(QStringView rfcNormalizado) = 0;

    // Perfiles visibles con activo = 1, ordenados por rfc ASC. Tx: opcional.
    // Errores: Almacenamiento, Interno.
    virtual Resultado<QList<PerfilSat>, ErrorPersistencia> listarActivosVisibles() = 0;
};

} // namespace satcfdi
