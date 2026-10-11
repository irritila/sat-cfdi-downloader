#include "application/estadoagregado/EstadoAgregado.h"

namespace satcfdi {

EstadoAgregado calcularEstadoAgregado(const EntradasEstadoAgregado& e) noexcept
{
    if (e.solicitudesRequierenAtencion || e.perfilesRequierenAtencion) {
        return EstadoAgregado::Atencion;
    }
    if (e.trabajando) {
        return EstadoAgregado::Trabajando;
    }
    if (e.pausado) {
        return EstadoAgregado::Pausado;
    }
    return EstadoAgregado::Normal;
}

bool solicitudRequiereAtencion(EstadoResumen estado) noexcept
{
    switch (estado) {
    case EstadoResumen::ErrorSat:
    case EstadoResumen::Rechazada:
    case EstadoResumen::EnvioFallido:
    case EstadoResumen::EnvioIncierto:
        return true;
    case EstadoResumen::Creada:
    case EstadoResumen::Enviando:
    case EstadoResumen::Enviada:
    case EstadoResumen::Aceptada:
    case EstadoResumen::EnProceso:
    case EstadoResumen::Terminada:
    case EstadoResumen::Vencida:
        break;
    }
    return false;
}

bool paqueteRequiereAtencion(EstadoDescarga estado) noexcept
{
    return estado == EstadoDescarga::Error;
}

bool perfilRequiereAtencion(const PerfilConPreparacion& p) noexcept
{
    return p.perfil.activo && p.preparacion != PreparacionPerfil::Lista
           && p.preparacion != PreparacionPerfil::Verificando;
}

QString claveEstable(EstadoAgregado estado)
{
    switch (estado) {
    case EstadoAgregado::Normal: return QStringLiteral("Normal");
    case EstadoAgregado::Trabajando: return QStringLiteral("Trabajando");
    case EstadoAgregado::Pausado: return QStringLiteral("Pausado");
    case EstadoAgregado::Atencion: return QStringLiteral("Atencion");
    }
    return {};
}

} // namespace satcfdi
