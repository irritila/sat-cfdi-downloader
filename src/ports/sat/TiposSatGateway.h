#pragma once

#include "domain/solicitudes/EstadosSolicitud.h"
#include "ports/storage/TiposAlmacenamiento.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace satcfdi {

// Tipos del puerto SatGateway (T009 D4). Sin SOAP, XML, URLs ni persistencia.
// Los textos SAT (`mensaje`) llegan TAL CUAL: quien los muestre o persista
// debe sanearlos (LogSanitizer / catalogo D10).

// Fase en la que termino una operacion del gateway.
// - Preparacion: no salio nada a la red (entrada invalida, sobre o firma
//   imposibles de construir con el material dado).
// - AntesDeEnvio: no se observo requestSent (DNS, conexion, TLS, deadline).
// - DespuesDeEnvio: la peticion salio y no hubo HTTP completo (incierto).
// - RespuestaExplicita: HTTP completo, pero sin resultado utilizable: Fault,
//   HTTP no 200, XML invalido, resultado ausente o Paquete invalido.
enum class FaseSatGateway {
    Preparacion,
    AntesDeEnvio,
    DespuesDeEnvio,
    RespuestaExplicita,
};

QString claveEstable(FaseSatGateway fase);

struct ErrorSatGateway {
    FaseSatGateway fase = FaseSatGateway::Preparacion;
    std::optional<int> estadoHttp;   // solo RespuestaExplicita
    std::optional<QString> codigoSat; // CodEstatus o faultcode, si se pudo leer
    bool cancelada = false;          // termino por Cancelacion (fase = donde se corto)
    bool deadlineVencido = false;    // termino por el timeout de la operacion (D7)
    // INFERIDO (T006 no lo observo): HTTP 401, o Fault de seguridad
    // (faultcode con InvalidSecurity, FailedAuthentication o
    // SecurityTokenUnavailable/Expired). OperacionesSat invalida la sesion de
    // token (D6) y la siguiente operacion autentica de nuevo.
    bool tokenRechazado = false;
    // Tecnico y saneado: fase, HTTP, faultcode, enum de red. Sin cuerpo,
    // token, RFC, ids, URL ni bytes del paquete.
    QString diagnosticoSanitizado;
};

// Solicitud de descarga masiva (ADR 0013). El gateway elige la operacion SAT
// por `tipo` y fija TipoSolicitud=CFDI y EstadoComprobante=Vigente.
// - Emitidos: RfcEmisor = RfcSolicitante = rfcSolicitante; `contrapartes` ->
//   RfcReceptores/RfcReceptor (0..n).
// - Recibidos: RfcReceptor = RfcSolicitante = rfcSolicitante; `contrapartes`
//   -> RfcEmisor (0..1; mas de una es Preparacion).
struct SolicitudSat {
    TipoDescarga tipo = TipoDescarga::Emitidos;
    QString rfcSolicitante;              // RFC canonico del perfil
    QString fechaInicial;                // "yyyy-MM-ddTHH:mm:ss" (fecha_inicial_sat)
    QString fechaFinal;                  // "yyyy-MM-ddTHH:mm:ss" (fecha_final_sat)
    QStringList contrapartes;            // RFC canonicos
    std::optional<QString> tipoComprobante;
    std::optional<QString> complemento;
};

// VerificaSolicitudDescarga. `idSolicitud` se valida (UUID) antes de la red.
struct ConsultaSolicitudSat {
    QString idSolicitud;
    QString rfcSolicitante;
};

// Descargar. `idPaquete` se valida (<UUID>_<NN>) antes de la red.
struct ConsultaPaqueteSat {
    QString idPaquete;
    QString rfcSolicitante;
};

// Respuesta explicita de SolicitaDescarga* (cualquier CodEstatus, incluidos
// rechazos documentados: los clasifica OperacionesSat).
struct RespuestaCreacion {
    QString codEstatus;
    QString mensaje;
    std::optional<QString> idSolicitud; // vacio en el XML -> nullopt
};

// Respuesta explicita de VerificaSolicitudDescarga. codEstatus es el de la
// PETICION; estadoSolicitud (1..6) y codigoEstadoSolicitud, los de la
// solicitud verificada (se conservan separados).
struct RespuestaVerificacion {
    QString codEstatus;
    std::optional<int> estadoSolicitud;
    std::optional<QString> codigoEstadoSolicitud;
    QString mensaje;
    std::optional<qint64> numeroCfdi;
    QStringList idsPaquete;
};

// Respuesta explicita de Descargar. Con codEstatus 5000 y Paquete valido, el
// receptor recibio la fuente (paqueteEntregado). Con otro codigo (5007, 5008,
// 5004...) el receptor NO se invoca.
struct RespuestaDescarga {
    QString codEstatus;
    QString mensaje;
    bool paqueteEntregado = false;
    qint64 bytesPaquete = 0; // ZIP decodificado (lo unico registrable)
};

// Receptor del ZIP decodificado (T009 D5). Lo implementa OperacionesSat: su
// recibir() suele llamar PackageStorage::guardarAtomico(ubicacion, paquete,
// cancelacion) y guardar el resultado en si mismo. Se invoca a lo sumo una vez
// por descarga, en el hilo del llamador, solo con CodEstatus 5000 y Paquete
// Base64 valido no vacio. La fuente entrega chunks de 64 KiB.
class ReceptorPaqueteSat {
public:
    virtual ~ReceptorPaqueteSat() = default;
    virtual void recibir(FuenteZipPorChunks& paquete) = 0;
};

inline QString claveEstable(FaseSatGateway fase)
{
    switch (fase) {
    case FaseSatGateway::Preparacion: return QStringLiteral("Preparacion");
    case FaseSatGateway::AntesDeEnvio: return QStringLiteral("AntesDeEnvio");
    case FaseSatGateway::DespuesDeEnvio: return QStringLiteral("DespuesDeEnvio");
    case FaseSatGateway::RespuestaExplicita: return QStringLiteral("RespuestaExplicita");
    }
    return {};
}

} // namespace satcfdi
