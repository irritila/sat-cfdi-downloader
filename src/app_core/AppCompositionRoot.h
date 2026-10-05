#pragma once

#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/CredencialesSatService.h"

#include <QFuture>
#include <QString>

#include <functional>
#include <memory>

class QQmlApplicationEngine;

namespace satcfdi {

class AccionesWorker;
class AppLifecycleController;
class ExtensionWorker;
class OperacionExecutor;
class OperacionesSat;
class Programador;
class WorkerLocal;
class CredencialesSatServicePersistido;
class SecretStore;
class ConfiguracionAppService;
class ConfiguracionAppServicePersistido;
class LogSanitizer;
class OSIntegration;
class SingleInstanceCoordinator;
class PerfilesSatService;
class PerfilesSatServicePersistido;
class PersistenceDispatcher;
class PresentacionViewModels;
class SolicitudesService;
class SolicitudesServicePersistido;
class SqlitePersistencia;

// Composition root de T003: unico lugar que elige implementaciones concretas y
// arma persistencia SQLite -> dispatcher -> servicios persistidos -> view
// models -> QML. No instancia servicios demo ni crea carpeta de ZIPs.
//
// Precondicion: `rutaBase` ya inicializada por AppBootstrapper (migrada, WAL,
// conexion de bootstrap cerrada).
//
// T005: recibe el SecretStore (MacOSSecretStore en produccion, FakeSecretStore
// en pruebas) por referencia NO propietaria: lo posee main y debe vivir mas
// que este objeto (las CredencialPreparada capturan el store). Al construir el
// grafo encola reconciliar() en el dispatcher serial sin esperar: no bloquea
// la carga de QML y las operaciones de credencial posteriores quedan detras.
// Un error de reconciliacion no borra nada ni impide arrancar.
//
// T004: iniciarCicloDeVida() crea AppLifecycleController sobre la ventana ya
// cargada; OSIntegration y SingleInstanceCoordinator los posee main y deben
// vivir mas que este objeto.
//
// T007: OperacionExecutor (hilo y conexion SQLite propios), WorkerLocal y
// el puerto OperacionesSat (nulo hasta T009). La salida explicita los detiene
// antes (ExtensionWorker); el destructor lo garantiza en cualquier caso.
//
// Cierre (destructor, hilo grafico): engine -> controlador -> view models ->
// acciones/worker -> ejecutor (detenido; cierra su conexion) ->
// servicios (credenciales, configuracion, solicitudes, perfiles) ->
// tarea de cierre de la conexion en el hilo del dispatcher ->
// dispatcher.cerrar() -> persistencia.
// T007: dependencias inyectables del monitoreo. Vacias = produccion:
// OperacionesSatNulo (hasta T009; toda operacion falla en Preparacion), reloj
// del sistema y ProgramadorQt propios (uno del ejecutor y otro del worker).
// Lo inyectado no es propiedad del root y debe vivir mas que el.
struct OpcionesMonitoreo {
    OperacionesSat* operacionesSat = nullptr;
    RelojUtc reloj;
    Programador* programadorEjecutor = nullptr;
    Programador* programadorWorker = nullptr;
};

class AppCompositionRoot {
public:
    AppCompositionRoot(const QString& rutaBase, SecretStore& secretStore,
                       OpcionesMonitoreo monitoreo = {});
    ~AppCompositionRoot();

    AppCompositionRoot(const AppCompositionRoot&) = delete;
    AppCompositionRoot& operator=(const AppCompositionRoot&) = delete;

    // Crea el engine, inyecta las propiedades iniciales y carga
    // SatCfdiDownloader/Main. Devuelve false si no hay objeto raiz.
    bool cargar();

    // Despues de cargar(): crea el controlador, lo inicia (inicializa `os`,
    // refleja la configuracion persistida y muestra la ventana si el arranque
    // fue Manual) y conecta las activaciones de `instancia` (opcional).
    // `salida` vacia: QCoreApplication::exit(0).
    AppLifecycleController& iniciarCicloDeVida(OSIntegration& os,
                                               SingleInstanceCoordinator* instancia = nullptr,
                                               std::function<void()> salida = {});

    // T007: encola la recuperacion y arranca el worker (pausado o activo segun
    // la configuracion persistida, que el worker lee). Idempotente.
    // iniciarCicloDeVida() lo llama; las pruebas pueden llamarlo sin QML.
    void iniciarMonitoreo();

    // Para pruebas de integracion.
    OperacionExecutor& ejecutor() const { return *m_ejecutor; }
    WorkerLocal& worker() const { return *m_worker; }
    ConfiguracionAppService& configuracion() const;
    CredencialesSatService& credenciales() const;
    // Reconciliacion encolada al construir (T005, DA5).
    QFuture<CredencialesSatService::ResultadoReconciliacion> reconciliacionInicial() const
    {
        return m_reconciliacionInicial;
    }
    AppLifecycleController* controlador() const { return m_controlador.get(); }
    SolicitudesService& solicitudes() const;
    PerfilesSatService& perfiles() const;
    PresentacionViewModels& viewModels() const { return *m_viewModels; }
    QQmlApplicationEngine* engine() const { return m_engine.get(); }

private:
    std::unique_ptr<SqlitePersistencia> m_persistencia;
    std::unique_ptr<PersistenceDispatcher> m_dispatcher;
    std::unique_ptr<LogSanitizer> m_sanitizer;
    std::unique_ptr<PerfilesSatServicePersistido> m_perfilesService;
    std::unique_ptr<SolicitudesServicePersistido> m_solicitudesService;
    std::unique_ptr<ConfiguracionAppServicePersistido> m_configuracionService;
    std::unique_ptr<CredencialesSatServicePersistido> m_credencialesService;
    QFuture<CredencialesSatService::ResultadoReconciliacion> m_reconciliacionInicial;
    // T007: monitoreo (orden de destruccion inverso: acciones -> worker ->
    // ejecutor -> puerto/programadores, todo antes del dispatcher).
    std::unique_ptr<OperacionesSat> m_operacionesSatPropio;
    std::unique_ptr<Programador> m_programadorEjecutorPropio;
    std::unique_ptr<Programador> m_programadorWorkerPropio;
    std::unique_ptr<OperacionExecutor> m_ejecutor;
    std::unique_ptr<WorkerLocal> m_worker;
    std::unique_ptr<ExtensionWorker> m_extensionWorker;
    std::unique_ptr<AccionesWorker> m_accionesWorker;
    bool m_monitoreoIniciado = false;
    std::unique_ptr<PresentacionViewModels> m_viewModels;
    std::unique_ptr<AppLifecycleController> m_controlador;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
};

} // namespace satcfdi
