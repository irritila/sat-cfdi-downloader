#pragma once

#include "application/operaciones/SenalCancelacion.h"
#include "domain/common/Resultado.h"
#include "domain/operaciones/FallaOperacion.h"
#include "domain/operaciones/ResultadosOperacion.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/SolicitudId.h"
#include "domain/solicitudes/SolicitudPersistida.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QString>

namespace satcfdi {

// Contextos de operacion (T007 D3, D12): datos NO secretos que el adaptador
// necesita. Sin MaterialFirma, token, rutas absolutas ni bytes (D15: el
// adaptador de T009 obtiene el material y el token por si mismo).

struct ContextoEnvio {
    SolicitudPersistida solicitud;   // filtros canonicos ya persistidos (estado Enviando)
    SenalCancelacion cancelacion;
};

struct ContextoVerificacion {
    SolicitudId solicitudId;
    PerfilId perfilSatId;
    QString rfcSolicitante;
    QString idSolicitudSat;
    SenalCancelacion cancelacion;
};

struct ContextoDescarga {
    QString paqueteId;               // id local (UUID); el adaptador deriva el destino final
    SolicitudId solicitudId;
    PerfilId perfilSatId;
    QString rfcSolicitante;
    QString idPaqueteSat;
    SenalCancelacion cancelacion;
    // T009: fecha_inicial_sat de la solicitud ("yyyy-MM-ddTHH:mm:ss"); con
    // rfcSolicitante y solicitudId forma la UbicacionPaquete de T008. Vacia
    // si no se pudo leer: el adaptador falla en Preparacion sin red.
    QString fechaInicialSat;
};

struct ContextoArchivoFinal {
    QString paqueteId;
    SolicitudId solicitudId;
    PerfilId perfilSatId;
    QString idPaqueteSat;
    SenalCancelacion cancelacion;
    // T009: datos para derivar la ruta final (UbicacionPaquete de T008).
    QString rfcSolicitante;
    QString fechaInicialSat;
};

// Puerto de aplicacion de las operaciones externas (T007 D3, ADR 0018).
// Lo consume SOLO OperacionExecutor; T009 lo implementa sobre SatGateway,
// PackageStorage y SecretStore. T007 provee FakeOperacionesSat (pruebas) y un
// adaptador nulo (falla Preparacion) para la app sin T009.
//
// Contrato:
// - SINCRONO y potencialmente BLOQUEANTE (red, Keychain, disco). Se llama
//   desde el hilo del OperacionExecutor, una operacion a la vez; nunca desde el
//   hilo grafico ni desde PersistenceDispatcher, y nunca con una transaccion
//   SQLite abierta.
// - Cancelacion cooperativa: el adaptador observa `contexto.cancelacion`
//   (solicitada(), esperar(), alSolicitar()) y termina en cuanto puede con
//   FallaOperacion{cancelada = true, fase = la fase alcanzada}. Si no regresa,
//   el proceso sale y la recuperacion resuelve (D1, D12).
// - Resultados semanticos ya clasificados (ver ResultadosOperacion.h y
//   FallaOperacion.h). Diagnosticos saneados; sin SOAP, token, ZIP ni
//   MaterialFirma.
// - Debe ser seguro llamarlo desde el hilo del ejecutor durante toda la vida
//   del ejecutor; el composition root lo crea antes y lo destruye despues.
class OperacionesSat {
public:
    virtual ~OperacionesSat() = default;

    // SolicitaDescarga{Emitidos,Recibidos}. Cualquier CodEstatus explicito ->
    // ResultadoEnvio; el resto -> FallaOperacion (Preparacion, Autenticacion y
    // AntesDeEnvio GARANTIZAN que la solicitud no se transmitio: ADR 0017).
    virtual Resultado<ResultadoEnvio, FallaOperacion> enviar(const ContextoEnvio& contexto) = 0;

    // VerificaSolicitudDescarga. Exito solo con CodEstatus 5000; otro
    // CodEstatus -> FallaOperacion{RespuestaExplicita, codigo}.
    virtual Resultado<ResultadoVerificacion, FallaOperacion> verificar(const ContextoVerificacion& contexto) = 0;

    // Descargar + guardado del ZIP en su destino final (T008). Exito solo con
    // CodEstatus 5000 y archivo final confirmado; otros CodEstatus ->
    // RespuestaExplicita; fallas de guardado -> Almacenamiento (causa).
    virtual Resultado<ResultadoDescarga, FallaOperacion> descargar(const ContextoDescarga& contexto) = 0;

    // Solo el archivo FINAL (D14). Una falla NO significa "no existe".
    virtual Resultado<ResultadoArchivoFinal, FallaOperacion>
    existeArchivoFinal(const ContextoArchivoFinal& contexto) = 0;

    // Gate por perfil (D9): estado de la credencial del perfil (SinCredencial,
    // Lista, Vencida, ...). Una falla (p. ej. Keychain bloqueado) cuenta como
    // "no Lista" para el ciclo.
    virtual Resultado<EstadoCredencial, FallaOperacion> obtenerEstadoCredencial(const PerfilId& perfil) = 0;
};

} // namespace satcfdi
