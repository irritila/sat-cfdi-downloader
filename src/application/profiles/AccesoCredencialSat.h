#pragma once

#include "application/profiles/CredencialesSatService.h"

namespace satcfdi {

// Acceso SINCRONO a la credencial de un perfil desde un hilo de trabajo
// (T009 D5): lo consume OperacionesSatProductivo en el hilo del
// OperacionExecutor, donde no se puede esperar un QFuture que depende del
// dispatcher o del hilo grafico.
//
// Hilo: nunca el hilo grafico (HiloNoPermitido, sin tocar SQLite ni el
// SecretStore). Ambas llamadas se serializan con importar/reemplazar/eliminar:
// observan la credencial anterior o la nueva completa.
// Secretos: MaterialFirma se devuelve por valor (move-only) y quien lo recibe
// no lo retiene mas alla de la operacion.
class AccesoCredencialSat {
public:
    virtual ~AccesoCredencialSat() = default;

    // Como CredencialesSatService::obtenerEstado, pero sincrono: SinCredencial
    // sin fila; MaterialDanado con referencias invalidas; si no, el estado del
    // SecretStore con el reloj del servicio. No devuelve Validando.
    virtual Resultado<EstadoCredencial, ErrorCredencialSat> estadoEnHiloDeTrabajo(const PerfilId& perfilId) = 0;

    // CredencialesSatService::obtenerMaterialFirma (no valida vigencia).
    virtual Resultado<MaterialFirma, ErrorCredencialSat> materialEnHiloDeTrabajo(const PerfilId& perfilId) = 0;
};

} // namespace satcfdi
