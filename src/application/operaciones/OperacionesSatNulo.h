#pragma once

#include "application/operaciones/OperacionesSat.h"

namespace satcfdi {

// Adaptador nulo seguro (T007 trabajo esperado 8): mientras no exista T009,
// TODA operacion falla en Preparacion (codigo "sin_adaptador_sat"); nunca
// simula exito. La credencial tambien falla (cuenta como no Lista: el worker
// no selecciona trabajo). Sin estado; seguro en cualquier hilo.
class OperacionesSatNulo final : public OperacionesSat {
public:
    static FallaOperacion falla()
    {
        return FallaOperacion::de(FaseOperacion::Preparacion, QStringLiteral("sin_adaptador_sat"),
                                  QStringLiteral("Integracion SAT no disponible en esta version."));
    }

    Resultado<ResultadoEnvio, FallaOperacion> enviar(const ContextoEnvio&) override
    {
        return Resultado<ResultadoEnvio, FallaOperacion>::fallo(falla());
    }
    Resultado<ResultadoVerificacion, FallaOperacion> verificar(const ContextoVerificacion&) override
    {
        return Resultado<ResultadoVerificacion, FallaOperacion>::fallo(falla());
    }
    Resultado<ResultadoDescarga, FallaOperacion> descargar(const ContextoDescarga&) override
    {
        return Resultado<ResultadoDescarga, FallaOperacion>::fallo(falla());
    }
    Resultado<ResultadoArchivoFinal, FallaOperacion> existeArchivoFinal(const ContextoArchivoFinal&) override
    {
        return Resultado<ResultadoArchivoFinal, FallaOperacion>::fallo(falla());
    }
    Resultado<EstadoCredencial, FallaOperacion> obtenerEstadoCredencial(const PerfilId&) override
    {
        return Resultado<EstadoCredencial, FallaOperacion>::fallo(falla());
    }
};

} // namespace satcfdi
