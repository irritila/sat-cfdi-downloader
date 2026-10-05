#pragma once

#include "domain/common/Resultado.h"
#include "infrastructure/sat/TokenSat.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

namespace satcfdi::sat {

// Parser de respuestas SOAP del SAT y de SOAP 1.1 Faults (T006, D2/D7) con
// QXmlStreamReader (sin DTD ni entidades externas). Estricto por operacion:
// el elemento de resultado debe estar en el namespace del servicio y dentro
// de su contenedor (p. ej. VerificaSolicitudDescargaResult dentro de
// VerificaSolicitudDescargaResponse; IdsPaquetes solo como hijo directo del
// Result); lo que aparezca en otro namespace o contenedor se ignora (y si
// falta el resultado: SinResultado). Mantiene SEPARADOS
// CodEstatus (de la operacion), EstadoSolicitud y CodigoEstadoSolicitud (de
// la solicitud verificada). Los textos se devuelven tal cual vienen del SAT;
// el enmascarado para evidencia es responsabilidad de EnmascaradorEvidencia.

struct FaultSoap {
    QString codigo;  // faultcode (p. ej. "s:Client" o "a:InvalidSecurity")
    QString mensaje; // faultstring
    QString detalle; // texto plano de <detail>, truncado a 500 caracteres
};

struct ErrorRespuesta {
    enum class Tipo {
        XmlMalFormado,
        Fault,          // `fault` presente
        SinResultado,   // no aparece el elemento de resultado esperado
        ValorInvalido,  // entero o Base64 invalido, token vacio
    };
    Tipo tipo = Tipo::XmlMalFormado;
    std::optional<FaultSoap> fault;
    QString diagnostico; // tecnico, sin valores de la respuesta
};

// AutenticaResponse/AutenticaResult + Timestamp de la respuesta (TTL).
struct RespuestaAutentica {
    TokenSat token; // move-only; nunca se registra
};

// SolicitaDescarga{Emitidos,Recibidos}Result.
struct RespuestaSolicitud {
    QString idSolicitud;
    QString rfcSolicitante;
    QString codEstatus;
    QString mensaje;
};

// VerificaSolicitudDescargaResult.
struct RespuestaVerificacion {
    QString codEstatus;            // estatus de la PETICION de verificacion
    QString mensaje;
    std::optional<int> estadoSolicitud;  // 1..6 (Aceptada..Vencida)
    QString codigoEstadoSolicitud; // codigo de la SOLICITUD verificada
    std::optional<int> numeroCfdis;
    QStringList idsPaquetes;
};

// Descargar: CodEstatus/Mensaje (header h:respuesta, SUPUESTO phpcfdi) y Paquete.
struct RespuestaDescarga {
    QString codEstatus;
    QString mensaje;
    QByteArray paquete;          // ZIP decodificado; NUNCA se registra su contenido
    qsizetype tamanoPaquete = 0; // bytes decodificados (lo unico registrable)
};

// Token: se copia del lector XML directamente a un BufferSecreto (sin
// QString intermedio cuando llega en un solo bloque de texto). Best effort:
// QXmlStreamReader conserva su propio buffer decodificado y el QByteArray del
// cuerpo puede tener copias compartidas (QFuture/ResultadoHttp) que este
// parser no controla.
Resultado<RespuestaAutentica, ErrorRespuesta> parsearAutentica(const QByteArray& cuerpo);
// Igual, y despues sobrescribe con ceros el buffer de `cuerpo` (si no esta
// compartido; si lo esta, lo suelta) y lo deja vacio. La CLI debe usar esta.
Resultado<RespuestaAutentica, ErrorRespuesta> parsearAutenticaYLimpiar(QByteArray& cuerpo);
Resultado<RespuestaSolicitud, ErrorRespuesta> parsearSolicitud(const QByteArray& cuerpo);
Resultado<RespuestaVerificacion, ErrorRespuesta> parsearVerificacion(const QByteArray& cuerpo);
Resultado<RespuestaDescarga, ErrorRespuesta> parsearDescarga(const QByteArray& cuerpo);

// Solo el Fault (o nullopt si el cuerpo no contiene un s:Fault SOAP 1.1).
std::optional<FaultSoap> parsearFault(const QByteArray& cuerpo);

} // namespace satcfdi::sat
