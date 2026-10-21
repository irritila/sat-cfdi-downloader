#pragma once

#include "app_core/MigracionRaizPaquetes.h"
#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/CredencialesSatService.h"
#include "domain/perfiles/PerfilId.h"

#include <QFuture>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QQmlApplicationEngine;

namespace satcfdi {

class AccionesWorker;
class ArchivosZipCfdi;
class AdaptadorCatalogoCfdi;
class AlmacenPdfCfdi;
class CfdiPdfRenderer;
class PrevisualizacionCfdiService;
class CatalogoCfdiService;
class CatalogoMensualCfdiService;
class DescargaGuiadaService;
class FuenteFacturasRoot;
class FuenteHistorialRoot;
class PreferenciasFacturasArchivo;
class CfdiArchiveReader;
class IndexadorCfdi;
class AccesoFinder;
class AccesoPaquetesEjecutor;
class NotificadorOS;
class ServicioNotificaciones;
class MonitorEstadoAgregado;
class AvisoVencimientoEFirma;
class ConsultaPrimerUso;
class ConsultaPreparacionPerfiles;
class ConsultaExistenciaEjecutor;
class AppLifecycleController;
class ExtensionWorker;
class OperacionExecutor;
class OperacionesSat;
class PackageStorage;
class SatGateway;
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
// T015.2: IndexadorCfdi (hilo propio, nunca la cola del ejecutor) y
// CatalogoCfdiService. iniciarMonitoreo() lanza la pasada inicial; cada
// solicitudActualizada del ejecutor sincroniza esa solicitud; la salida lo
// detiene (salidaIniciada) y el destructor une su hilo antes del dispatcher.
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
// OperacionesSatProductivo (T009) sobre el SatGateway, reloj
// del sistema y ProgramadorQt propios (uno del ejecutor y otro del worker).
// Lo inyectado no es propiedad del root y debe vivir mas que el.
struct OpcionesMonitoreo {
    OperacionesSat* operacionesSat = nullptr;
    RelojUtc reloj;
    Programador* programadorEjecutor = nullptr;
    Programador* programadorWorker = nullptr;
    // T008: almacenamiento de ZIP. Si `packageStorage` es nulo, el root crea
    // FilesystemPackageStorage(raizPaquetes); `raizPaquetes` vacia =
    // <directorio de la base>/paquetes (las pruebas nunca tocan el home). main
    // pasa resolverRaiz(dataDir, home) (D8).
    PackageStorage* packageStorage = nullptr;
    QString raizPaquetes;
    // T019 D8: aviso de la resolucion de la raiz (ArranquePreparado::
    // resolucionRaizPaquetes.aviso); main lo pasa tal cual. Vacio = sin aviso.
    std::optional<AvisoRaizPaquetes> avisoRaizPaquetes;
    // T009: SatGateway. Nulo = SatGatewayProductivo propio del root (endpoints
    // productivos). Las pruebas inyectan FakeSatGateway.
    SatGateway* satGateway = nullptr;
    // T014.3: programador del aviso de vencimiento de e.firma (cada 24 h).
    // Nulo = ProgramadorQt propio con `reloj`.
    Programador* programadorVencimiento = nullptr;
    // T015.2: lector de ZIP del indice de CFDI. Nulo = LectorZipLibzip propio.
    // Las pruebas inyectan lectores con compuertas o contadores (debe ser
    // seguro desde el hilo del indexador).
    CfdiArchiveReader* lectorCfdi = nullptr;
    // T015.2: version del parser guardada con el indice. 0 = kVersionParserCfdi.
    int versionParserCfdi = 0;
    // T015.4 (D7): cache de PDF de CFDI. Vacio = <directorio de datos>/cache/
    // cfdi-pdf si la base no esta en el directorio de datos por defecto (es
    // decir, con --data-dir o en pruebas), si no CacheLocation/cfdi-pdf.
    QString directorioCachePdf;
    // Temporal de la app si la cache no se puede escribir. Vacio =
    // TempLocation/satcfdi-cfdi-pdf.
    QString directorioTemporalPdf;
    // Limite de la cache en bytes. 0 = 200 MB (D7). Las pruebas lo reducen.
    qint64 limiteCachePdfBytes = 0;
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
    PackageStorage& packageStorage() const { return *m_packageStorage; }
    SatGateway& satGateway() const { return *m_satGateway; }
    AccesoFinder& accesoFinder() const { return *m_accesoFinder; }
    // Raiz absoluta efectiva de los paquetes (D8); vacia si se inyecto el storage.
    const QString& raizPaquetes() const noexcept { return m_raizPaquetes; }
    // T019 D8: aviso de la carpeta de paquetes para la UI (conflicto, error o
    // raiz nueva no disponible). Fijo durante la sesion; hilo grafico. Rutas
    // como `~/...`. Lo consume el view model del aviso (presentacion).
    const std::optional<AvisoRaizPaquetes>& avisoRaizPaquetes() const noexcept { return m_avisoRaizPaquetes; }
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
    // T014.3: consulta de primer uso (D1) para AppViewModel, y aviso de
    // vencimiento (D2), que iniciarMonitoreo() arranca.
    ConsultaPrimerUso& consultaPrimerUso() const;
    AvisoVencimientoEFirma& avisoVencimiento() const { return *m_avisoVencimiento; }
    // T014.4 D2: estado agregado del icono del menu bar (se inicia en
    // iniciarCicloDeVida()).
    MonitorEstadoAgregado& estadoAgregado() const { return *m_estadoAgregado; }
    // T015.2: indexador (hilo propio) y API de consulta del indice para T015.3.
    IndexadorCfdi& indexadorCfdi() const { return *m_indexadorCfdi; }
    CatalogoCfdiService& catalogoCfdi() const { return *m_catalogoCfdi; }
    // T023.2/T023.3: catalogo mensual y descarga guiada (sin UI aun: T023.4).
    CatalogoMensualCfdiService& catalogoMensual() const { return *m_catalogoMensual; }
    DescargaGuiadaService& descargaGuiada() const { return *m_descargaGuiada; }
    // T015.4: previsualizacion PDF (hilo propio) y sus directorios efectivos.
    PrevisualizacionCfdiService& previsualizacionCfdi() const { return *m_previsualizacionCfdi; }
    const QString& directorioCachePdf() const noexcept { return m_directorioCachePdf; }
    const QString& directorioTemporalPdf() const noexcept { return m_directorioTemporalPdf; }
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
    std::unique_ptr<PackageStorage> m_packageStoragePropio;
    std::unique_ptr<SatGateway> m_satGatewayPropio;
    SatGateway* m_satGateway = nullptr;
    OSIntegration* m_os = nullptr; // T009: notificaciones (lo posee main)
    std::unique_ptr<NotificadorOS> m_notificador;
    QMetaObject::Connection m_conexionInvalidarSesion;
    std::unique_ptr<ServicioNotificaciones> m_servicioNotificaciones;
    // T014.3 (se destruyen antes que notificaciones, servicios y dispatcher).
    std::unique_ptr<ConsultaPrimerUso> m_consultaPrimerUso;
    std::unique_ptr<ConsultaPreparacionPerfiles> m_consultaVencimiento;
    std::unique_ptr<Programador> m_programadorVencimientoPropio;
    std::unique_ptr<AvisoVencimientoEFirma> m_avisoVencimiento;
    // T014.4 D2 (se destruye antes que las consultas y servicios que usa).
    std::unique_ptr<MonitorEstadoAgregado> m_estadoAgregado;
    // T015.2 (se destruyen antes que el dispatcher; el indexador une su hilo).
    std::unique_ptr<CfdiArchiveReader> m_lectorCfdiPropio;
    std::unique_ptr<ArchivosZipCfdi> m_archivosCfdi;
    std::unique_ptr<IndexadorCfdi> m_indexadorCfdi;
    std::unique_ptr<CatalogoCfdiService> m_catalogoCfdi;
    std::unique_ptr<CatalogoMensualCfdiService> m_catalogoMensual;
    std::unique_ptr<DescargaGuiadaService> m_descargaGuiada;
    std::unique_ptr<ConsultaPreparacionPerfiles> m_consultaPerfilesFacturas; // T023.4
    std::unique_ptr<FuenteFacturasRoot> m_fuenteFacturas;                    // T023.4
    std::unique_ptr<FuenteHistorialRoot> m_fuenteHistorial;                  // T023.5
    std::unique_ptr<PreferenciasFacturasArchivo> m_preferenciasFacturas;     // T023.4
    // T015.3: ConsultaCatalogoCfdi de la presentacion sobre m_catalogoCfdi
    // (se destruye antes que el, despues de los view models).
    std::unique_ptr<AdaptadorCatalogoCfdi> m_adaptadorCatalogo;
    // T015.4: previsualizacion PDF (se destruye antes que el indexador, que
    // usa para reindexar, y que el dispatcher; une su hilo). Sus puertos
    // propios (lector y archivos independientes de los del indexador,
    // renderer y cache) se destruyen despues de el.
    std::unique_ptr<CfdiArchiveReader> m_lectorPdf;
    std::unique_ptr<ArchivosZipCfdi> m_archivosPdf;
    std::unique_ptr<CfdiPdfRenderer> m_renderizadorPdf;
    std::unique_ptr<AlmacenPdfCfdi> m_almacenPdf;
    std::unique_ptr<PrevisualizacionCfdiService> m_previsualizacionCfdi;
    QString m_directorioCachePdf;
    QString m_directorioTemporalPdf;
    PackageStorage* m_packageStorage = nullptr;
    QString m_raizPaquetes;
    std::optional<AvisoRaizPaquetes> m_avisoRaizPaquetes;
    std::unique_ptr<Programador> m_programadorEjecutorPropio;
    std::unique_ptr<Programador> m_programadorWorkerPropio;
    std::unique_ptr<OperacionExecutor> m_ejecutor;
    std::unique_ptr<WorkerLocal> m_worker;
    std::unique_ptr<ExtensionWorker> m_extensionWorker;
    std::unique_ptr<AccionesWorker> m_accionesWorker;
    std::unique_ptr<ConsultaExistenciaEjecutor> m_consultaExistencia;
    // T009.1: fachada de acceso (sobre el ejecutor) y orquestacion con Finder.
    std::unique_ptr<AccesoPaquetesEjecutor> m_accesoPaquetes;
    std::unique_ptr<AccesoFinder> m_accesoFinder;
    bool m_monitoreoIniciado = false;
    std::unique_ptr<PresentacionViewModels> m_viewModels;
    std::unique_ptr<AppLifecycleController> m_controlador;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
};

} // namespace satcfdi
