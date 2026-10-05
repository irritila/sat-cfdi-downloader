#pragma once

#include "domain/operaciones/FallaOperacion.h"
#include "domain/operaciones/RegistrosOperacion.h"
#include "domain/operaciones/ResultadosOperacion.h"
#include "domain/paquetes/EstadoDescarga.h"
#include "domain/paquetes/PaquetePersistido.h"
#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/SolicitudPersistida.h"

#include <QDateTime>
#include <QStringList>

#include <chrono>
#include <optional>

namespace satcfdi::politicas {

// Politicas PURAS de T007 (sin reloj, sin E/S): agenda D8, desenlaces D7/D6 y
// vencimiento estimado D10. El ejecutor las aplica dentro de sus transacciones.

inline constexpr std::chrono::minutes kIntervaloCorto{10};
inline constexpr std::chrono::minutes kIntervaloLargo{30};
inline constexpr int kSinCambioParaIntervaloLargo = 3;
inline constexpr int kFallasParaSuspender = 3;
inline constexpr std::chrono::hours kVidaEstimadaPaquete{72};

// --- Envio (D7, ADR 0017) ----------------------------------------------------

// Rechazos DOCUMENTADOS de la creacion (sin IdSolicitud): 300-305, 5001,
// 5002, 5005.
bool esRechazoDocumentadoDeCreacion(QStringView codEstatus);

// Destino de Enviando segun el resultado o la falla:
// - 5000 con IdSolicitud -> Enviada;
// - rechazo documentado sin IdSolicitud -> EnvioFallido;
// - 5000 sin IdSolicitud, 5006, codigo no documentado -> EnvioIncierto;
// - falla Preparacion/Autenticacion/AntesDeEnvio -> Creada;
// - falla DespuesDeEnvio/RespuestaExplicita/Almacenamiento -> EnvioIncierto.
// La cancelacion se aplica como la falla de su fase (D12).
EstadoLocal destinoEnvio(const ResultadoEnvio& resultado);
EstadoLocal destinoEnvio(const FallaOperacion& falla);

// --- Verificacion (D8) -------------------------------------------------------

// Observacion SAT comparable: estado, codigo, numero y CONJUNTO de ids.
bool sinCambio(const SolicitudPersistida& anterior, const QStringList& idsPaquetesConocidos,
               const ResultadoVerificacion& nueva);

// Estados SAT terminales para el worker (sin verificacion automatica):
// Terminada, Error, Rechazada, Vencida.
bool esEstadoSatTerminal(EstadoSolicitudSat estado);

struct AgendaTrasExito {
    std::optional<QDateTime> siguienteVerificacionEn; // nullopt si el estado es terminal
    int verificacionesSinCambio = 0;
};
// Cambio -> contador 0 e intervalo 10 min; sin cambio -> contador + 1 y, desde
// el tercero consecutivo, 30 min. Terminal -> sin siguiente.
AgendaTrasExito agendaTrasExito(int verificacionesSinCambioPrevias, bool huboCambio, EstadoSolicitudSat estado,
                                const QDateTime& ahoraUtc);

// Codigos que suspenden la verificacion automatica tras 3 fallas iguales:
// 300, 302, 303, 5004 (solo en RespuestaExplicita). Red, 404, Fault y 5011
// son transitorios.
bool esFallaSuspendible(const FallaOperacion& falla);

struct AgendaTrasFalla {
    QString claveFalla;
    int fallasIguales = 1;
    bool registrarLog = true;      // solo si cambio la clave (fase, codigo)
    bool suspender = false;        // tercera falla igual suspendible
    std::optional<QDateTime> siguienteVerificacionEn; // ahora + 30 min, o nullopt si suspende
};
AgendaTrasFalla agendaTrasFalla(const RachaVerificacion& racha, const FallaOperacion& falla,
                                const QDateTime& ahoraUtc);

// --- Descarga (D6, D14) -------------------------------------------------------

struct DesenlaceDescarga {
    EstadoDescarga destino = EstadoDescarga::Error;
    std::optional<MotivoVencimiento> motivo;
    std::optional<OrigenVencimiento> origen;
};
// Exito -> Descargado. Falla RespuestaExplicita 5007 -> Vencido (SAT,
// paquete_expirado). Cualquier otra falla (incluidas 5008, Preparacion,
// Autenticacion, Almacenamiento/ColisionDestino y cancelacion) -> Error.
DesenlaceDescarga desenlaceDescarga(const FallaOperacion& falla);

// Paquete con vencimiento estimado alcanzado y aun no Descargado/Vencido.
bool vencimientoEstimadoAlcanzado(const PaquetePersistido& paquete, const QDateTime& ahoraUtc);

QDateTime vencimientoEstimado(const QDateTime& primeraObservacionUtc);

} // namespace satcfdi::politicas
