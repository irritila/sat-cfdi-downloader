#pragma once

#include "domain/logs/LogSolicitud.h"
#include "domain/operaciones/FallaOperacion.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QMetaType>
#include <QString>

#include <optional>

namespace satcfdi {

// Tipos de operacion del ejecutor serial (T007).
enum class TipoOperacion {
    Envio,               // solo usuario (D4)
    Verificacion,
    Descarga,
    VencimientoEstimado, // local (D10)
    Recuperacion,        // al arrancar (D9)
    RegistroIntencion,   // pausa: solo activa la bandera (sin puerto)
};

QString claveEstable(TipoOperacion tipo);

// Prioridad de cola: manual (usuario) antes que automatica (worker); dentro
// del mismo origen, verificacion antes que descarga (D9).
enum class PrioridadOperacion {
    Recuperacion = 0,
    Manual = 1,
    Automatica = 2,
};

// Desenlace publicado de una operacion (senal encolada al hilo grafico).
struct ResultadoOperacion {
    enum class Desenlace {
        Aplicada,    // se aplico la transicion (exito o falla de la operacion)
        Descartada,  // ya no aplicaba: eliminada, estado distinto, intencion consumida
        Rechazada,   // el ejecutor esta deteniendose o detenido (D1)
        ErrorLocal,  // fallo de persistencia al marcar/aplicar (se registra diagnostico)
    };

    TipoOperacion tipo = TipoOperacion::Verificacion;
    OrigenLog origen = OrigenLog::Usuario;
    Desenlace desenlace = Desenlace::Aplicada;
    std::optional<SolicitudId> solicitudId;
    std::optional<QString> paqueteId;
    std::optional<FallaOperacion> falla; // si la operacion externa fallo
};

} // namespace satcfdi

Q_DECLARE_METATYPE(satcfdi::TipoOperacion)
Q_DECLARE_METATYPE(satcfdi::ResultadoOperacion)
