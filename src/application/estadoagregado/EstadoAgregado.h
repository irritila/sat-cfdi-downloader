#pragma once

#include "application/profiles/PerfilConPreparacion.h"
#include "domain/paquetes/EstadoDescarga.h"
#include "domain/solicitudes/EstadoResumen.h"

#include <QString>

namespace satcfdi {

// T014.4 D2: estado visual agregado de la app (icono del menu bar). Es un
// estado de PRESENTACION derivado: no es un estado del worker ni se persiste.
// Vocabulario de application; app_core lo traduce a OSIntegration::EstadoIcono.
enum class EstadoAgregado { Normal, Trabajando, Pausado, Atencion };

// Entradas ya resueltas del calculo.
// - solicitudesRequierenAtencion: alguna solicitud visible con estado resumen
//   ErrorSat, Rechazada, EnvioFallido o EnvioIncierto, o con algun paquete
//   visible en Error.
// - perfilesRequierenAtencion: algun perfil activo con e.firma no Lista
//   (perfilRequiereAtencion).
// - trabajando: fase Ejecutando del worker.
// - pausado: monitoreo en pausa (fase Pausado del worker).
struct EntradasEstadoAgregado {
    bool solicitudesRequierenAtencion = false;
    bool perfilesRequierenAtencion = false;
    bool trabajando = false;
    bool pausado = false;

    friend bool operator==(const EntradasEstadoAgregado&, const EntradasEstadoAgregado&) = default;
};

// Prioridad Atencion > Trabajando > Pausado > Normal. Funcion pura.
EstadoAgregado calcularEstadoAgregado(const EntradasEstadoAgregado& entradas) noexcept;

// ErrorSat, Rechazada, EnvioFallido, EnvioIncierto. Vencida NO (es un fin
// normal del ciclo SAT, no una falla accionable).
bool solicitudRequiereAtencion(EstadoResumen estado) noexcept;
bool paqueteRequiereAtencion(EstadoDescarga estado) noexcept;

// Perfil ACTIVO con e.firma no Lista. Verificando es transitorio (aun sin
// respuesta) y no cuenta, para no parpadear Atencion en cada reverificacion.
// EstadoNoDisponible si cuenta: el monitoreo de ese perfil no puede avanzar.
bool perfilRequiereAtencion(const PerfilConPreparacion& perfil) noexcept;

// Clave estable ("Normal", "Trabajando", "Pausado", "Atencion").
QString claveEstable(EstadoAgregado estado);

} // namespace satcfdi
