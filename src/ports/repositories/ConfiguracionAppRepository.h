#pragma once

#include "domain/common/Resultado.h"
#include "domain/configuracion/ConfiguracionApp.h"
#include "ports/persistence/ErrorPersistencia.h"

namespace satcfdi {

// Repositorio de la configuracion unica de la aplicacion (configuracion_app,
// id = 1). Sincrono; solo lectura en T003.
//
// Hilo: solo desde tareas de PersistenceDispatcher.
class ConfiguracionAppRepository {
public:
    virtual ~ConfiguracionAppRepository() = default;

    // Fila id = 1. Tx: opcional. Errores: NoEncontrado si la fila no existe
    // (base no migrada); Almacenamiento; Interno.
    virtual Resultado<ConfiguracionApp, ErrorPersistencia> obtener() = 0;
};

} // namespace satcfdi
