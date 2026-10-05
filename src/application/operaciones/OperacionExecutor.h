#pragma once

#include "application/operaciones/OperacionesSat.h"
#include "application/operaciones/Programador.h"
#include "application/operaciones/TiposOperacion.h"
#include "application/persistence/PuertosPersistencia.h"
#include "domain/operaciones/RegistrosOperacion.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QFuture>
#include <QList>
#include <QObject>

#include <chrono>
#include <functional>
#include <memory>

namespace satcfdi {

class SolicitudMasivaRepository;
class LogSolicitudRepository;
class PaqueteSolicitudRepository;
class PackageStorage;
class OperacionesSolicitudRepository;
class UnitOfWork;
class LogSanitizer;

// Puertos del ejecutor: referencias NO propietarias que el composition root
// crea antes y destruye despues de detener el ejecutor. Los repositorios usan
// la conexion SQLite del HILO DEL EJECUTOR (SqlitePersistencia crea una por
// hilo); `cerrarConexion` se invoca en ese hilo antes de terminarlo.
struct PuertosEjecutor {
    SolicitudMasivaRepository& solicitudes;
    LogSolicitudRepository& logs;
    OperacionesSolicitudRepository& operaciones;
    UnitOfWork& unidadDeTrabajo;
    const LogSanitizer& sanitizer;
    OperacionesSat& sat;
    std::function<void()> cerrarConexion; // p. ej. SqlitePersistencia::cerrarConexionDelHiloActual
    // T008 (aditivos, al final para no romper inicializaciones existentes):
    // - paquetes: lectura de paquetes por solicitud para asociar finales del
    //   escaneo (archivo_huerfano, D10);
    // - almacenamiento: escanearRecuperacion, eliminarTemporal y la consulta de
    //   existencia (D9-D11), siempre en el hilo del ejecutor.
    // nullptr = sin almacenamiento: la recuperacion omite el escaneo (con
    // diagnostico) y consultarExistencia devuelve ErrorComprobacion.
    PaqueteSolicitudRepository* paquetes = nullptr;
    PackageStorage* almacenamiento = nullptr;
};

// Ejecutor serial de operaciones criticas (T007 D5, ADR 0014/0016/0018).
//
// Ownership/hilo:
// - QObject con afinidad al hilo grafico; lo crea y posee el composition root.
//   Posee su propio QThread (NO reutiliza PersistenceDispatcher) donde corre
//   una cola serial: una operacion a la vez, con prioridad
//   Recuperacion > Manual > Automatica y FIFO dentro de cada prioridad.
// - Los metodos publicos se llaman desde el hilo grafico; las senales se
//   emiten hacia el hilo grafico (conexiones encoladas; tipos registrados con
//   qRegisterMetaType en el constructor).
// - Cada operacion: (1) tx breve que marca Enviando/Descargando; (2) llamada a
//   OperacionesSat SIN transaccion abierta; (3) tx BEGIN IMMEDIATE que aplica
//   el resultado solo si eliminado_en IS NULL (D5), con su LogSolicitud
//   saneado en la misma tx.
//
// Salida (D1, D12): detener() deja de aceptar operaciones (las nuevas se
// resuelven como Rechazada), espera la actual hasta `plazo` medido con el
// Programador/Reloj inyectados, solicita la cancelacion cooperativa, aplica
// la falla resultante, cierra la conexion del hilo y termina el QThread.
class OperacionExecutor : public QObject {
    Q_OBJECT

public:
    OperacionExecutor(PuertosEjecutor puertos, RelojUtc reloj, Programador& programador,
                      QObject* parent = nullptr);
    ~OperacionExecutor() override; // llama detener() con plazo 0 si no se detuvo

    // --- Operaciones (devuelven el desenlace; el future se completa en el
    //     hilo del ejecutor, continuar con then(contextoGrafico, ...)). ------

    // Envio de una solicitud Creada (solo usuario, D4).
    QFuture<ResultadoOperacion> enviar(const SolicitudId& solicitudId);

    // Verificacion. `intencionCapturadaEn`: si consume una intencion pendiente,
    // el accion_pendiente_en leido al reclamarla (D13).
    QFuture<ResultadoOperacion> verificar(const SolicitudId& solicitudId, OrigenLog origen,
                                          std::optional<QDateTime> intencionCapturadaEn = std::nullopt);

    // Descarga de un paquete (Error solo con origen Usuario).
    QFuture<ResultadoOperacion> descargar(const QString& paqueteId, OrigenLog origen,
                                          std::optional<QDateTime> intencionCapturadaEn = std::nullopt);

    // Registra una intencion manual con el monitoreo pausado (sin puerto).
    QFuture<ResultadoOperacion> registrarIntencion(const SolicitudId& solicitudId, TipoIntencion tipo);

    // Descarta una intencion que ya no aplica (D13: solo si accion_pendiente_en
    // sigue igual a `capturadaEn`), con log accion_pendiente_descartada. La usa
    // WorkerLocal cuando una intencion de descarga no tiene paquetes
    // reintentables. ADITIVO en el corte B1 (sin cambiar el resto del contrato).
    QFuture<ResultadoOperacion> descartarIntencion(const SolicitudId& solicitudId, TipoIntencion tipo,
                                                   const QDateTime& capturadaEn);

    // Vencimiento estimado local de un paquete (D10).
    QFuture<ResultadoOperacion> vencerEstimado(const QString& paqueteId);

    // Recuperacion al arrancar (D9): Enviando -> EnvioIncierto; Descargando
    // -> Descargado / Disponible / sin cambio segun existeArchivoFinal (D14).
    // Un log por operacion afectada. T008 D9/D10, con `almacenamiento`:
    // escanearRecuperacion(); temporal propio (asociado a un paquete
    // Descargando o no asociable) -> eliminarTemporal tras aplicar la regla de
    // T007; final de una solicitud visible sin paquete con ese nombre ->
    // se conserva y log archivo_huerfano (una vez por archivo); final sin
    // solicitud visible -> se conserva y solo diagnostico de app (D3). Si el
    // escaneo falla, no se borra nada (diagnostico) y la recuperacion SQLite
    // continua. Diagnosticos en la categoria "satcfdi.recuperacion.archivos",
    // con rutas relativas.
    QFuture<ResultadoOperacion> recuperar();

    // T008 D11: existencia de un archivo final (ruta RELATIVA de ruta_local) en
    // el hilo del ejecutor, prioridad Manual. Solo lectura: sin estado ni log.
    // Tambien emite existenciaConsultada (encolada al hilo grafico).
    QFuture<ExistenciaArchivo> consultarExistencia(const QString& rutaRelativa);

    // Gate por perfil (D9), en el hilo del ejecutor y en orden de cola. Los
    // perfiles cuya consulta FALLA no aparecen en la lista (cuentan como no
    // Lista); estadoCredencialCambiado se emite al cambiar el estado observado.
    QFuture<QList<std::pair<PerfilId, EstadoCredencial>>> consultarCredenciales(const QList<PerfilId>& perfiles);

    // Lectura de seleccion del worker (ciclo D9) en el hilo del ejecutor, con
    // su conexion, EN ORDEN de cola (prioridad Automatica). Solo lecturas del
    // repositorio de operaciones; sin puerto ni transaccion.
    QFuture<void> leer(std::function<void(OperacionesSolicitudRepository&)> lectura);

    // --- Ciclo de vida -------------------------------------------------------

    bool aceptaOperaciones() const noexcept;
    // Idempotente. El future se completa cuando el hilo termino.
    QFuture<void> detener(std::chrono::milliseconds plazo = std::chrono::seconds(10));

signals:
    void operacionIniciada(satcfdi::TipoOperacion tipo);
    void operacionTerminada(const satcfdi::ResultadoOperacion& resultado);
    // La solicitud (y/o sus paquetes) cambio en SQLite: refrescar la vista.
    void solicitudActualizada(const satcfdi::SolicitudId& solicitudId);
    // Cambio el estado de credencial observado de un perfil (D9).
    void estadoCredencialCambiado(const satcfdi::PerfilId& perfil, satcfdi::EstadoCredencial estado);
    // Resultado de consultarExistencia (T008 D11).
    void existenciaConsultada(const QString& rutaRelativa, satcfdi::ExistenciaArchivo existencia);
    // La cola quedo vacia (sin operacion activa).
    void inactivo();
    void detenido();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace satcfdi

Q_DECLARE_METATYPE(satcfdi::EstadoCredencial)
