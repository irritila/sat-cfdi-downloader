#pragma once

#include "application/profiles/PerfilConPreparacion.h"
#include "application/requests/SolicitudDtos.h"

#include <QString>

namespace satcfdi::catalogo {

// Catalogo de mensajes visibles de T009 (D10). Textos fijos: sin RFC
// completo, Ids, token, rutas ni Mensaje SAT crudo. Vacio = sin mensaje.

// Por que no se puede enviar con la credencial del perfil (vacio si Lista y
// perfil activo).
QString motivoCredencial(PreparacionPerfil preparacion, bool perfilActivo);

// Estado de la solicitud segun estado local, codigos de creacion y
// verificacion (conservados por separado).
QString mensajeSolicitud(const SolicitudDetalle& detalle);

// Paquete: vencido, maximo de descargas (5008) o error reintentable.
QString mensajePaquete(const PaqueteResumen& paquete);

// 5008: el paquete alcanzo el maximo de descargas; no se ofrece reintentar.
bool esMaximoDescargas(const PaqueteResumen& paquete);

} // namespace satcfdi::catalogo
