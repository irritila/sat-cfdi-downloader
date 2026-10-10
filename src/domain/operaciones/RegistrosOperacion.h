#pragma once

#include "domain/paquetes/PaquetePersistido.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace satcfdi {

// Registros de lectura/escritura de OperacionesSolicitudRepository (T007).
// Timestamps UTC. Solo datos no secretos.

// Paquete con lo que necesita el ejecutor para operar y para el gate por
// perfil (D9), sin una segunda lectura de la solicitud.
struct PaqueteDescargable {
    PaquetePersistido paquete;
    PerfilId perfilSatId;
    QString rfcSolicitante;
    QString idSolicitudSat; // de la solicitud (Enviada)
};

// Intencion manual registrada con el monitoreo pausado (DC5 de T001).
// `accionPendienteEn` es el valor capturado al leerla: consumirIntencion()
// solo limpia si la columna conserva ese valor (D13).
struct IntencionPendiente {
    SolicitudId solicitudId;
    PerfilId perfilSatId;
    bool verificacionPendiente = false;
    bool descargaPendiente = false;
    QDateTime accionPendienteEn;
};

// Intencion manual de reintento de UN paquete (T014.2 D2, ADR 0007
// enmendado): paquete_solicitud.reintento_pendiente_en. `reintentoPendienteEn`
// es el valor capturado: consumirIntencionPaquete() solo limpia si la columna
// lo conserva (misma regla D13).
struct IntencionPaquete {
    QString paqueteId;
    SolicitudId solicitudId;
    PerfilId perfilSatId;
    QString idPaqueteSat;
    QDateTime reintentoPendienteEn;
};

enum class TipoIntencion {
    Verificacion, // verificacion_pendiente
    Descarga,     // descarga_pendiente (reintenta paquetes en Error/Disponible de la solicitud)
};

// Racha de verificacion (D8; columnas de la migracion 003 + T001).
struct RachaVerificacion {
    int verificacionesSinCambio = 0;                    // verificaciones_sin_cambio
    std::optional<QString> ultimaClaveFalla;            // ultima_clave_falla_verificacion
    int fallasIguales = 0;                              // fallas_verificacion_iguales
};

// Solicitud y paquetes en estado intermedio al arrancar (recuperacion, D9).
struct TrabajoInterrumpido {
    QList<SolicitudId> solicitudesEnviando;
    QList<PaqueteDescargable> paquetesDescargando;
};

// --- Escrituras --------------------------------------------------------------

// Desenlace de un envio: Enviando -> destino (Enviada, Creada, EnvioFallido o
// EnvioIncierto). El repositorio fija los invariantes de cada destino:
// - Enviada: idSolicitudSat, codEstatus, enviadaEn y siguienteVerificacionEn
//   obligatorios.
// - Creada (D7/ADR 0017): limpia envio_iniciado_en, cod_estatus_solicitud y
//   mensaje_solicitud_sat; guarda ultimoError.
// - EnvioFallido / EnvioIncierto: conserva envio_iniciado_en; guarda codigo y
//   mensaje si los hay, y ultimoError.
struct AplicacionEnvio {
    SolicitudId solicitudId;
    EstadoLocal destino = EstadoLocal::EnvioIncierto;
    std::optional<QString> idSolicitudSat;
    std::optional<QString> codEstatus;
    std::optional<QString> mensaje;          // saneado
    std::optional<QString> ultimoError;      // saneado
    std::optional<QDateTime> enviadaEn;
    std::optional<QDateTime> siguienteVerificacionEn;
};

// Paquete nuevo observado en una verificacion (D10): se inserta solo si el
// par (solicitud, idPaqueteSat) no existe; `vencimientoEstimadoEn` se fija una
// vez y nunca se desplaza.
struct PaqueteNuevo {
    QString id; // UUID canonico local
    QString idPaqueteSat;
    QDateTime disponibleEn;
    QDateTime vencimientoEstimadoEn;
};

// Verificacion exitosa (CodEstatus 5000) en UNA transaccion (D5, ADR 0015):
// estado SAT, agenda, reinicio de la racha de fallas, paquetes nuevos y, si
// `vencerNoDescargados`, paquetes no Descargado/Vencido -> Vencido (SAT,
// motivo solicitud_expirada).
struct AplicacionVerificacion {
    SolicitudId solicitudId;
    QDateTime verificadaEn;
    EstadoSolicitudSat estadoSolicitudSat = EstadoSolicitudSat::Aceptada;
    std::optional<QString> codigoEstadoSolicitud;
    std::optional<QString> mensajeVerificacion;  // saneado
    std::optional<qint64> numeroCfdi;
    std::optional<QDateTime> siguienteVerificacionEn; // nullopt = sin verificacion automatica
    int verificacionesSinCambio = 0;
    QList<PaqueteNuevo> paquetesNuevos;
    bool vencerNoDescargados = false;
};

struct ResultadoAplicacionVerificacion {
    bool aplicada = false;          // false: solicitud eliminada o ya no verificable
    QList<QString> paquetesInsertados;   // ids locales insertados (los ya existentes se omiten)
    QList<QString> paquetesVencidos;     // ids locales que pasaron a Vencido
};

// Verificacion fallida (D8): no toca estado SAT ni verificaciones_sin_cambio.
struct AplicacionFallaVerificacion {
    SolicitudId solicitudId;
    QDateTime falladaEn;
    QString ultimoError;                       // saneado
    QString claveFalla;                        // claveFalla(falla)
    int fallasIguales = 1;
    std::optional<QDateTime> siguienteVerificacionEn; // nullopt = suspendida
};

// Desenlace de una descarga: Descargando -> destino (Descargado, Error,
// Vencido o Disponible). Disponible solo en recuperacion sin archivo final.
// Descargado exige rutaFinal. Vencido exige motivo/origen (CHECK de T001).
struct AplicacionDescarga {
    QString paqueteId;
    EstadoDescarga destino = EstadoDescarga::Error;
    QDateTime aplicadaEn;
    std::optional<QString> rutaFinal;
    std::optional<QString> codigoDescargaSat;
    std::optional<QString> mensajeDescargaSat;   // saneado
    std::optional<QString> ultimoError;          // saneado
    std::optional<MotivoVencimiento> motivoVencimiento;
    std::optional<OrigenVencimiento> origenVencimiento;
    bool reconciliado = false;                   // fija reconciliado_en (recuperacion)
};

} // namespace satcfdi
