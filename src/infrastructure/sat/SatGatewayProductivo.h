#pragma once

#include "infrastructure/sat/SatOperaciones.h"
#include "ports/SatGateway.h"

#include <QDateTime>
#include <QUrl>

#include <chrono>
#include <functional>
#include <map>

namespace satcfdi {

// Configuracion del gateway (T009 D7). La inyecta el composition root; no es
// preferencia de usuario.
struct SatGatewayOptions {
    std::chrono::milliseconds timeoutAutenticacion{30000};
    std::chrono::milliseconds timeoutCreacion{45000};
    std::chrono::milliseconds timeoutVerificacion{30000};
    std::chrono::milliseconds timeoutDescarga{300000};
    // Sustituye el endpoint productivo de una operacion. Sin
    // permitirEndpointsDePrueba solo se aceptan https://*.clouda.sat.gob.mx
    // (otro valor: Preparacion sin trafico). Vacio = sat::descriptor().
    std::map<sat::Operacion, QUrl> endpoints;
    // SOLO pruebas (servidor HTTP local). El composition root nunca lo activa.
    bool permitirEndpointsDePrueba = false;
    // Reloj UTC para el Timestamp de Autentica (inyectable en pruebas).
    std::function<QDateTime()> reloj;
    // Cada cuanto se consulta la Cancelacion mientras se espera la red.
    std::chrono::milliseconds intervaloCancelacion{20};
};

// SatGateway productivo sobre los bloques de T006: SobresSat (firma
// confirmada: exclusiva en Autentica, inclusiva en el resto, X509IssuerSerial
// docsat, sin WS-Addressing), ClienteHttpSat (fases por requestSent,
// deadline, SOAPAction, Authorization WRAP) y RespuestasSat (parser estricto
// y Faults).
//
// Hilo y espera: cada llamada crea su propio ClienteHttpSat
// (QNetworkAccessManager) en el hilo que invoca y espera la respuesta con un
// QEventLoop LOCAL de ese hilo (el del OperacionExecutor, que tiene event
// dispatcher). Nunca debe invocarse desde el hilo grafico: el event loop local
// reentraria la UI. Un QTimer de ese mismo loop consulta la Cancelacion cada
// `intervaloCancelacion` y aborta el QNetworkReply. La instancia no tiene
// estado mutable (solo opciones): no hay sesion de token ni material
// retenido. Sin reutilizacion de conexiones entre llamadas (un handshake TLS
// por operacion; aceptable para el ritmo del ejecutor serial).
//
// Memoria en descarga: ClienteHttpSat lee el cuerpo completo; el pico
// aproximado es cuerpo Base64 (~1.33 N) + texto del lector XML (UTF-16, hasta
// ~2.7 N transitorio) + ZIP decodificado (N). El ZIP se entrega al receptor
// en chunks de 64 KiB y los buffers se liberan al volver.
//
// Saneado en la frontera (SaneadoRespuestaSat): codEstatus y
// codigoEstadoSolicitud por lista de codigos documentados; faultcode por lista
// permitida (si no, "fault_no_reconocido"); Mensaje enmascarado y truncado;
// IdSolicitud/IdsPaquetes validados por formato. Nunca expone faultstring,
// detail ni cuerpos HTTP. Nunca registra token, SOAP, firma, certificado ni
// bytes del paquete; los diagnosticos son plantillas fijas (operacion, fase,
// HTTP, codigo permitido, clave tecnica y enum de red).
class SatGatewayProductivo final : public SatGateway {
public:
    explicit SatGatewayProductivo(SatGatewayOptions opciones = {});

    Resultado<TokenSat, ErrorSatGateway> autenticar(const MaterialFirma& material,
                                                    const Cancelacion& cancelacion) override;
    Resultado<RespuestaCreacion, ErrorSatGateway> crearSolicitud(const TokenSat& token, const SolicitudSat& solicitud,
                                                                 const MaterialFirma& material,
                                                                 const Cancelacion& cancelacion) override;
    Resultado<RespuestaVerificacion, ErrorSatGateway> verificarSolicitud(const TokenSat& token,
                                                                         const ConsultaSolicitudSat& consulta,
                                                                         const MaterialFirma& material,
                                                                         const Cancelacion& cancelacion) override;
    Resultado<RespuestaDescarga, ErrorSatGateway> descargarPaquete(const TokenSat& token,
                                                                   const ConsultaPaqueteSat& consulta,
                                                                   const MaterialFirma& material,
                                                                   ReceptorPaqueteSat& receptor,
                                                                   const Cancelacion& cancelacion) override;

    const SatGatewayOptions& opciones() const noexcept { return m_opciones; }

private:
    SatGatewayOptions m_opciones;
};

} // namespace satcfdi
