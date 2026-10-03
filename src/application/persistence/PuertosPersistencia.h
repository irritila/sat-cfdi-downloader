#pragma once

#include "domain/common/TimestampUtc.h"

#include <QDateTime>

#include <functional>

namespace satcfdi {

class PerfilSatRepository;
class SolicitudMasivaRepository;
class PaqueteSolicitudRepository;
class LogSolicitudRepository;
class ConfiguracionAppRepository;
class UnitOfWork;
class LogSanitizer;

// Puertos que usan los servicios persistidos. Referencias NO propietarias: el
// composition root crea y destruye las implementaciones (despues de
// PersistenceDispatcher::cerrar()). Solo se usan dentro de tareas del
// dispatcher, salvo `sanitizer` (puro, cualquier hilo).
struct PuertosPersistencia {
    PerfilSatRepository& perfiles;
    SolicitudMasivaRepository& solicitudes;
    PaqueteSolicitudRepository& paquetes;
    LogSolicitudRepository& logs;
    UnitOfWork& unidadDeTrabajo;
    const LogSanitizer& sanitizer;
    // T004. Puntero (no referencia) solo para mantener compatible la
    // inicializacion agregada existente de 6 campos mientras app_core lo
    // asigna; el composition root DEBE pasar &persistencia.configuracion().
    // Ningun servicio actual lo desreferencia: ConfiguracionAppServicePersistido
    // recibe el repositorio por referencia explicita.
    ConfiguracionAppRepository* configuracion = nullptr;
};

// Reloj inyectable (UTC, milisegundos) para pruebas deterministas.
using RelojUtc = std::function<QDateTime()>;

inline RelojUtc relojSistema()
{
    return &timestamp::ahoraUtc;
}

} // namespace satcfdi
