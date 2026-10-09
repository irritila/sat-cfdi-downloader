#pragma once

#include "domain/logs/LogSolicitud.h"
#include "domain/operaciones/FallaOperacion.h"
#include "domain/paquetes/EstadoDescarga.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/Duplicados.h"
#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace satcfdi {

// Fila de la lista de solicitudes. estadoResumen no se guarda: se deriva con
// derivarEstadoResumen(estadoLocal, estadoSat) (domain/solicitudes/EstadoResumen.h).
struct SolicitudResumen {
    SolicitudId id;
    QString perfilRfc;                     // rfc_solicitante
    std::optional<QString> rfcContraparte; // emitidos: primer receptor; recibidos: rfc_emisor
    TipoDescarga tipoDescarga = TipoDescarga::Emitidos;
    QDate fechaInicial;                    // dia de fecha_inicial_sat
    QDate fechaFinal;                      // dia de fecha_final_sat
    EstadoLocal estadoLocal = EstadoLocal::Creada;
    std::optional<EstadoSolicitudSat> estadoSat; // nulo sin respuesta SAT
    QDateTime creadaEn;                          // UTC
    int totalPaquetes = 0;                       // paquetes visibles
    // T013 D8 (UX-09): nombre del perfil SAT de la solicitud; "" si el perfil
    // no tiene nombre o ya no es visible (eliminado).
    QString perfilNombre;
};

struct PaqueteResumen {
    QString idPaqueteSat;
    EstadoDescarga estadoDescarga = EstadoDescarga::Disponible;
    QDateTime disponibleEn;                // UTC
    std::optional<QDateTime> descargadoEn; // solo si Descargado
    std::optional<QDateTime> vencidoEn;    // solo si Vencido
    std::optional<QString> codigoDescargaSat;
};

// Evento visible del detalle (log_solicitud saneado). Sin payload crudo.
struct LogResumen {
    TipoEventoLog tipoEvento = TipoEventoLog::SolicitudCreada;
    OrigenLog origen = OrigenLog::Usuario;
    std::optional<OrigenCodigoSat> origenCodigoSat;
    std::optional<QString> codigoSat;
    std::optional<QString> mensajeSat;
    QDateTime creadoEn; // UTC
};

struct SolicitudDetalle {
    SolicitudResumen resumen;

    // Filtros persistidos (texto SAT, hora Centro de Mexico sin offset).
    QString fechaInicialSat;
    QString fechaFinalSat;
    QStringList rfcContrapartes; // emitidos: receptores; recibidos: 0..1 emisor
    std::optional<QString> tipoComprobante;
    std::optional<QString> complemento;

    // Estado y codigos SAT persistidos (nulos en T003 salvo fixtures).
    std::optional<QString> idSolicitudSat;
    std::optional<QString> codEstatusSolicitud;
    std::optional<QString> mensajeSolicitudSat;
    std::optional<QString> codigoEstadoSolicitud;
    std::optional<QString> mensajeVerificacionSat;
    std::optional<qint64> numeroCfdi;
    std::optional<QDateTime> enviadaEn;
    std::optional<QDateTime> ultimaVerificacionEn;
    std::optional<QString> ultimoError;
    // T009: desglose de ultimoError cuando lo escribio una FallaOperacion
    // (domain/operaciones/FallaOperacion.h, desglosarUltimoError). Permite a
    // la UI elegir el texto D10 por fase/codigo (p. ej. Creada tras una falla
    // de Preparacion/Autenticacion/AntesDeEnvio). Vacio si ultimoError no
    // tiene ese formato (rechazo explicito "CodEstatus N", envio interrumpido).
    std::optional<UltimoErrorDesglosado> ultimoErrorDesglosado;

    QList<PaqueteResumen> paquetes; // resumen.totalPaquetes == paquetes.size()
    QList<LogResumen> logs;         // creado_en ASC
};

// Entrada del formulario de nueva solicitud (DA2). Una sola contraparte en
// T003; el dominio admite lista para emitidos.
struct NuevaSolicitudRequest {
    PerfilId perfilId;
    TipoDescarga tipoDescarga = TipoDescarga::Emitidos;
    QDate fechaInicial;
    QDate fechaFinal;
    std::optional<QString> rfcContraparte;
    std::optional<QString> tipoComprobante; // I, E, T, N, P
    std::optional<QString> complemento;
};

// Confirmacion explicita del usuario para crear una solicitud cuya evaluacion
// fue RequiereConfirmacion. Nunca habilita crear un duplicado Bloqueado.
enum class ConfirmacionDuplicado {
    SinConfirmar,
    Confirmada,
};

// Resultado idempotente de eliminar(): `cambio=false` si el id no existe o ya
// estaba eliminado (entonces eliminadoEn es nulo).
struct ResultadoEliminacion {
    bool cambio = false;
    std::optional<QDateTime> eliminadoEn; // UTC, mismo valor en solicitud, paquetes y logs
};

} // namespace satcfdi
