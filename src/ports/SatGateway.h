#pragma once

#include "domain/common/Resultado.h"
#include "ports/sat/TiposSatGateway.h"
#include "ports/sat/TokenSat.h"
#include "ports/secrets/SecretStoreTypes.h"
#include "ports/storage/TiposAlmacenamiento.h"

namespace satcfdi {

// Puerto hacia el servicio web de descarga masiva del SAT (T009 D4, contrato
// recomendado por T006). Sin SOAP ni persistencia: devuelve respuestas
// explicitas (cualquier CodEstatus) o ErrorSatGateway con la fase.
//
// Hilo: SINCRONO; se invoca desde el hilo del OperacionExecutor, nunca desde
// el hilo grafico. Una llamada a la vez por instancia y por hilo.
// Material: `MaterialFirma` se presta por referencia a cada operacion (todas
// firman: Autentica con WS-Security, las demas con firma enveloped) y el
// adaptador NO lo conserva. Token: lo crea autenticar(); el adaptador no
// guarda sesion (la sesion de token es de OperacionesSat, D6).
// Cancelacion: se consulta mientras espera la red y aborta la peticion; el
// error conserva la fase (AntesDeEnvio/DespuesDeEnvio) con cancelada=true.
// Timeouts: por operacion, configurados en el adaptador (D7).
// Nunca registra token, SOAP, firma, certificado ni bytes del paquete.
class SatGateway {
public:
    virtual ~SatGateway() = default;

    // Autentica. Exito: token con Expires (TTL observado: 300 s).
    virtual Resultado<TokenSat, ErrorSatGateway> autenticar(const MaterialFirma& material,
                                                            const Cancelacion& cancelacion) = 0;

    // SolicitaDescargaEmitidos o SolicitaDescargaRecibidos segun solicitud.tipo.
    virtual Resultado<RespuestaCreacion, ErrorSatGateway> crearSolicitud(const TokenSat& token,
                                                                         const SolicitudSat& solicitud,
                                                                         const MaterialFirma& material,
                                                                         const Cancelacion& cancelacion) = 0;

    virtual Resultado<RespuestaVerificacion, ErrorSatGateway>
    verificarSolicitud(const TokenSat& token, const ConsultaSolicitudSat& consulta, const MaterialFirma& material,
                       const Cancelacion& cancelacion) = 0;

    // Descargar. Con 5000 y Paquete valido entrega el ZIP decodificado al
    // receptor (por chunks) antes de volver; RespuestaDescarga refleja lo
    // explicito del SAT. Paquete vacio o Base64 invalido: RespuestaExplicita.
    virtual Resultado<RespuestaDescarga, ErrorSatGateway>
    descargarPaquete(const TokenSat& token, const ConsultaPaqueteSat& consulta, const MaterialFirma& material,
                     ReceptorPaqueteSat& receptor, const Cancelacion& cancelacion) = 0;
};

} // namespace satcfdi
