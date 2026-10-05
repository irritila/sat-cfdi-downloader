#pragma once

#include "application/operaciones/Programador.h"
#include "application/operaciones/TiposOperacion.h"
#include "application/persistence/PuertosPersistencia.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QMetaType>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace satcfdi {

class OperacionExecutor;
class ConfiguracionAppService;

// Estado del worker (T007 D11). No existe un Error global: los errores son de
// cada operacion y su log.
enum class EstadoWorker {
    Pausado,
    ActivoEnEspera,
    Ejecutando, // `tipo` en InstantaneaWorker
    Deteniendo,
    Detenido,
};

QString claveEstable(EstadoWorker estado);

// Lo que se publica al menu bar (D2) via OSIntegration (plataforma).
struct InstantaneaWorker {
    EstadoWorker estado = EstadoWorker::Detenido;
    std::optional<TipoOperacion> tipo; // solo en Ejecutando
    int pendientes = 0;                // solicitudes con alguna intencion pendiente

    friend bool operator==(const InstantaneaWorker&, const InstantaneaWorker&) = default;
};

// Monitoreo local (T007, ADR 0007/0018). QObject del hilo grafico; lo crea y
// posee el composition root despues del OperacionExecutor y lo destruye antes.
// No ejecuta operaciones: solo encola en el ejecutor.
//
// - iniciar(): encola la recuperacion (antes de cualquier ciclo, D9) y programa
//   el primer ciclo con el Programador.
// - Ciclo (D9): vencimientos estimados; si no esta pausado: gate por perfil
//   (consultarCredenciales, una vez por perfil), intenciones pendientes,
//   verificaciones debidas y descargas automaticas, con seleccion acotada.
//   Reprograma el siguiente ciclo al minimo siguiente_verificacion_en futuro.
// - Pausa: observa ConfiguracionAppService::configuracionCambiada; pausado no
//   encola operaciones con puerto; al reanudar consume intenciones.
// - Acciones manuales: con pausa registran la intencion; sin pausa encolan la
//   operacion manual (prioridad sobre la automatica).
class WorkerLocal : public QObject {
    Q_OBJECT

public:
    WorkerLocal(OperacionExecutor& ejecutor, ConfiguracionAppService& configuracion, Programador& programador,
                RelojUtc reloj, QObject* parent = nullptr);
    ~WorkerLocal() override;

    void iniciar();
    // Detiene timers y deja de encolar (D1); no espera al ejecutor.
    void detener();

    // Acciones manuales de la UI.
    void enviar(const SolicitudId& solicitudId);           // D4 (ver nota de pausa en el reporte de T007)
    void verificarAhora(const SolicitudId& solicitudId);   // tambien reanuda una verificacion suspendida
    void reintentarDescarga(const SolicitudId& solicitudId); // paquetes Error/Disponible de la solicitud

    // Fuerza un ciclo ahora (p. ej. al reanudar); para pruebas y la UI.
    void ejecutarCiclo();

    InstantaneaWorker instantanea() const;

signals:
    void instantaneaCambiada(const satcfdi::InstantaneaWorker& instantanea);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace satcfdi

Q_DECLARE_METATYPE(satcfdi::InstantaneaWorker)
