#pragma once

#include "domain/paquetes/EstadoDescarga.h"
#include "domain/solicitudes/DedupKey.h"
#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QList>
#include <QString>

#include <optional>

namespace satcfdi {

// Resultado de evaluar duplicados (T003 "Reglas de duplicados").
// Precedencia entre coincidencias: Bloqueado > RequiereConfirmacion > Libre.
enum class ClasificacionDuplicado {
    Libre,
    Bloqueado,
    RequiereConfirmacion,
};

// Motivo de la clasificacion, tomado de la coincidencia de referencia.
enum class MotivoDuplicado {
    SinCoincidencias,              // Libre
    SolicitudEnCurso,              // Creada, Enviando, Enviada sin estado SAT, Aceptada, EnProceso -> Bloqueado
    TerminadaConPaquetesPendientes,// Terminada con algun Disponible/Descargando/Error -> Bloqueado
    TerminadaDescargada,           // Terminada con todos Descargado -> Bloqueado
    TerminadaConPaquetesVencidos,  // >=1 Vencido y resto Descargado/Vencido -> RequiereConfirmacion (D002)
    TerminadaSinPaquetes,          // sin paquetes no eliminados -> RequiereConfirmacion (D008)
    EnvioIncierto,                 // -> RequiereConfirmacion (D003)
    SolicitudSinExito,             // EnvioFallido, Error SAT, Rechazada, Vencida -> RequiereConfirmacion
    SolicitudEliminada,            // eliminado_en no nulo -> RequiereConfirmacion
};

// Claves estables (nombre del enumerador) para logs y diagnostico.
QString claveEstable(ClasificacionDuplicado clasificacion);
QString claveEstable(MotivoDuplicado motivo);

// Una fila de solicitud_masiva con la misma dedup_key, INCLUIDAS eliminadas.
// `paquetesNoEliminados`: estado_descarga de sus paquetes con
// eliminado_en IS NULL (solo relevante si estadoSat == Terminada).
struct CoincidenciaDuplicado {
    SolicitudId id;
    EstadoLocal estadoLocal = EstadoLocal::Creada;
    std::optional<EstadoSolicitudSat> estadoSat;
    bool eliminada = false;
    QList<EstadoDescarga> paquetesNoEliminados;
};

struct EvaluacionDuplicado {
    ClasificacionDuplicado clasificacion = ClasificacionDuplicado::Libre;
    DedupKey dedupKey;
    // Coincidencia que determina la clasificacion; nulo si Libre.
    std::optional<SolicitudId> solicitudReferencia;
    MotivoDuplicado motivo = MotivoDuplicado::SinCoincidencias;
};

// Regla pura de la matriz de duplicados. Infraestructura la usa dentro de
// SolicitudMasivaRepository::clasificarDuplicado() para no duplicar la matriz
// en SQL. Ante empate de severidad la referencia es la primera coincidencia
// en el orden recibido (infra entrega creada_en DESC, id DESC).
EvaluacionDuplicado clasificarCoincidencias(const DedupKey& clave,
                                            const QList<CoincidenciaDuplicado>& coincidencias);

} // namespace satcfdi
