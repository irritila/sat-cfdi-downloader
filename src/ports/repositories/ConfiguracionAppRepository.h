#pragma once

#include "domain/common/Resultado.h"
#include "domain/configuracion/ConfiguracionApp.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QDateTime>

namespace satcfdi {

// Repositorio de la configuracion unica de la aplicacion (configuracion_app,
// id = 1). Sincrono.
//
// Hilo: solo desde tareas de PersistenceDispatcher.
// Escrituras (T004): una operacion por campo para no sobrescribir preferencias
// concurrentes. Exigen un UnitOfWork activo en el hilo (error Transaccion si
// no lo hay); fijan actualizada_en = `en` y devuelven la fila resultante leida
// dentro de la misma transaccion (confirmada solo tras commit del llamador).
// `en` debe ser un instante UTC valido (Interno si no lo es).
class ConfiguracionAppRepository {
public:
    virtual ~ConfiguracionAppRepository() = default;

    // Fila id = 1. Tx: opcional. Errores: NoEncontrado si la fila no existe
    // (base no migrada); Almacenamiento; Interno.
    virtual Resultado<ConfiguracionApp, ErrorPersistencia> obtener() = 0;

    // Preferencia local de inicio automatico (inicio_automatico_habilitado).
    // No es el estado efectivo del Login Item. Errores: Transaccion;
    // NoEncontrado; Ocupado; Almacenamiento; Integridad; Interno.
    virtual Resultado<ConfiguracionApp, ErrorPersistencia>
    actualizarInicioAutomatico(bool habilitado, const QDateTime& en) = 0;

    // monitoreo_pausado. Mismos errores.
    virtual Resultado<ConfiguracionApp, ErrorPersistencia>
    actualizarMonitoreoPausado(bool pausado, const QDateTime& en) = 0;

    // ultimo_cierre_en = actualizada_en = `en`. Mismos errores.
    virtual Resultado<ConfiguracionApp, ErrorPersistencia> registrarUltimoCierre(const QDateTime& en) = 0;
};

} // namespace satcfdi
