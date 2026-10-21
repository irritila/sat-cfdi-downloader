#pragma once

#include "domain/cfdi/MesCalendario.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/SolicitudId.h"

#include <optional>

namespace satcfdi {

// T023.5: destino de navegacion de una notificacion de solicitud ya
// resuelto contra la base local (T023 D10, D14).
// - FacturasMes: la solicitud pertenece a un lote guiado visible -> Facturas
//   en el perfil y mes del lote.
// - Diagnostico: solicitud visible sin lote (previa a T023 o con filtros) ->
//   su detalle tecnico (DetalleSolicitudPage).
// - Facturas: la solicitud ya no existe, se elimino, su perfil se elimino o
//   la lectura fallo -> Facturas sin error, con la seleccion vigente.
struct DestinoNavegacion {
    enum class Tipo { Facturas, FacturasMes, Diagnostico };

    Tipo tipo = Tipo::Facturas;
    PerfilId perfil;                    // FacturasMes
    std::optional<MesCalendario> mes;   // FacturasMes
    SolicitudId solicitud;              // Diagnostico

    static DestinoNavegacion facturas() { return {}; }
    static DestinoNavegacion facturasMes(const PerfilId& perfil, MesCalendario mes)
    {
        return {Tipo::FacturasMes, perfil, mes, SolicitudId()};
    }
    static DestinoNavegacion diagnostico(const SolicitudId& solicitud)
    {
        return {Tipo::Diagnostico, PerfilId(), std::nullopt, solicitud};
    }

    friend bool operator==(const DestinoNavegacion&, const DestinoNavegacion&) = default;
};

} // namespace satcfdi
