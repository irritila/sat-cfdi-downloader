#pragma once

#include "domain/logs/LogSolicitud.h"
#include "domain/operaciones/FallaOperacion.h"
#include "domain/operaciones/TransicionNotificable.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/secrets/SecretStoreTypes.h"

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

// Resultado de consultarExistencia (T008 D11). Solo lectura del filesystem:
// nunca cambia el estado persistido del paquete.
enum class ExistenciaArchivo {
    Presente,
    NoEncontrado,
    ErrorComprobacion, // ruta invalida, raiz ilegible, sin almacenamiento o ejecutor detenido
};

QString claveEstable(ExistenciaArchivo existencia);

// Resolucion de una ruta revelable en Finder (T009.1 D2, D4, D7). Solo
// lectura: nunca cambia SQLite ni LogSolicitud ni crea directorios.
// - Disponible: `rutaAbsoluta` validada bajo la raiz en ese momento (solo para
//   OSIntegration; la UI no la muestra ni la concatena).
// - NoEncontrado: la ruta es valida pero el destino no existe (`mensaje` D7).
// - NoAplica: paquete no visible o no Descargado; solicitud eliminada o sin
//   paquetes Descargado (la accion no se ofrece; sin mensaje).
// - Error: validacion, lectura o ejecutor detenido (`mensaje` D7, sin ruta).
struct ResolucionRevelable {
    enum class Estado { Disponible, NoEncontrado, NoAplica, Error };

    Estado estado = Estado::Error;
    QString rutaAbsoluta; // solo con Disponible
    QString mensaje;      // D7, solo con NoEncontrado y Error

    friend bool operator==(const ResolucionRevelable&, const ResolucionRevelable&) = default;
};

QString claveEstable(ResolucionRevelable::Estado estado);

namespace mensajesacceso {
inline QString archivoNoEncontrado() { return QStringLiteral("Archivo local no encontrado"); }
inline QString carpetaNoEncontrada() { return QStringLiteral("Carpeta de paquetes no encontrada"); }
} // namespace mensajesacceso

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
Q_DECLARE_METATYPE(satcfdi::ResolucionRevelable)
Q_DECLARE_METATYPE(satcfdi::ExistenciaArchivo)
// Senales de OperacionExecutor (T007 D9, T009 D9).
Q_DECLARE_METATYPE(satcfdi::EstadoCredencial)
Q_DECLARE_METATYPE(satcfdi::TransicionNotificable)
