#pragma once

#include "app_core/MigracionRaizPaquetes.h"
#include "app_core/RestauracionArranque.h"
#include "presentation/viewmodels/PuertosRespaldoPresentacion.h"
#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/CredencialesSatService.h"
#include "domain/perfiles/PerfilId.h"
#include "ports/exportacion/EscritorZipExportacion.h"

#include <QFuture>
#include <QObject>
#include <QString>

#include <functional>
#include <QUuid>

#include <chrono>
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
class ExportacionFacturasService;
class PlanificadorExportacion;
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
class SqliteCopiaRespaldo;
class FuenteRespaldoLocal;
class ControlRespaldoGestor;
class ControlRestauracionServicio;
class DatosPreviosRestauracionRoot;
class ConfiguracionRespaldoIni;
namespace respaldo {
class FuenteRespaldo;
class DestinoRespaldo;
class RespaldoService;
class ContenedorRespaldo;
class GestorRespaldo;
class RestauracionService;
class ProgramadorRespaldoDiario;
class AlmacenKekRespaldo;
class CarpetaRespaldoSistema;
struct EstadoRespaldoGestor;
struct ResultadoRespaldo;
}
namespace exportacion {
class DestinoExportacion;
}

// T025.5 (D22): resultado de AppCompositionRoot::descartarDatosPrevios().
struct ResultadoDescarteDatosPrevios {
    enum class Estado {
        Descartados,          // carpetas propias borradas y e.firma reconciliada
        SinDatosPrevios,      // no habia carpetas propias (las ajenas no se tocan)
        Incompleto,           // alguna carpeta propia no se pudo borrar: no se reconcilia
        ErrorReconciliacion,  // borradas, pero la reconciliacion fallo (nada se borro del almacen)
    };

    Estado estado = Estado::SinDatosPrevios;
    int carpetasBorradas = 0;
    int carpetasAjenas = 0;         // `pre-restauracion-*` sin marca o enlazadas (intactas)
    int generacionesEliminadas = 0; // de la reconciliacion
    QString diagnostico;            // ASCII estable
};

QString claveEstable(ResultadoDescarteDatosPrevios::Estado e);

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
    // T025.4: almacen de KEK (Keychain) y referencia de carpeta (bookmark).
    // Nulos = plataforma sin respaldo diario (el gestor informa NoDisponible).
    // main los crea en macOS; las pruebas inyectan fakes.
    respaldo::AlmacenKekRespaldo* almacenKekRespaldo = nullptr;
    respaldo::CarpetaRespaldoSistema* carpetaRespaldoSistema = nullptr;
    // T025.4: programador del respaldo diario. Nulo = ProgramadorQt propio.
    Programador* programadorRespaldo = nullptr;
    // Intervalo del diario (0 = 24 h). En Debug, la variable de entorno
    // SATCFDI_RESPALDO_INTERVALO_PRUEBA_S lo reemplaza (verificacion manual).
    std::chrono::seconds intervaloRespaldoDiario{0};
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
    // T025.5: resultado de la restauracion aplicada al arrancar
    // (PreparacionProceso::restauracion); main lo pasa tal cual para T025.6.
    std::optional<ResultadoRestauracionArranque> restauracionAlArrancar;
    // T026.2: lector de ZIP de la exportacion (relectura por posicion en el
    // hilo del servicio). Nulo = LectorZipLibzip propio, independiente del
    // del indexador. Las pruebas inyectan compuertas (seguro entre hilos).
    // Compartido (C7): el servicio lo retiene mientras viva su hilo.
    std::shared_ptr<CfdiArchiveReader> lectorExportacion;
    // T026.2 (C5): plazo maximo que la salida y la restauracion esperan el
    // cierre de la exportacion; agotado, siguen y el `.partial` lo borra la
    // limpieza del siguiente arranque. 0 = 10 s. Las pruebas lo acortan.
    std::chrono::milliseconds plazoCierreExportacion{0};
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
    // T025.2: captura consistente para el respaldo (sin servicio ni UI aun).
    respaldo::FuenteRespaldo& fuenteRespaldo() const;
    // T025.2: limpieza de stagings huerfanos encolada al construir (dispatcher).
    QFuture<int> limpiezaRespaldoInicial() const { return m_limpiezaRespaldoInicial; }
    // T025.3: "Respaldar ahora" (sin UI, KEK provista por el llamador) y el
    // destino POSIX para una carpeta elegida.
    respaldo::RespaldoService& respaldoService() const { return *m_respaldoService; }
    // T025.4: configuracion, "respaldar ahora" y estado para T025.6.
    respaldo::GestorRespaldo& gestorRespaldo() const { return *m_gestorRespaldo; }
    respaldo::ProgramadorRespaldoDiario& programadorRespaldoDiario() const { return *m_programadorDiario; }
    QFuture<respaldo::ResultadoRespaldo> respaldarAhora();
    // T025.5: preparar y solicitar una restauracion (sin UI aun: T025.6).
    respaldo::RestauracionService& restauracion() const { return *m_restauracion; }
    // T025.5: lo que hizo el arranque con una restauracion pendiente.
    const std::optional<ResultadoRestauracionArranque>& restauracionAlArrancar() const noexcept
    {
        return m_restauracionAlArrancar;
    }
    // T025.5: detiene worker, respaldo, indice, vista previa y ejecutor
    // (cancelacion inmediata). Lo usa RestauracionService::solicitar().
    // T026.2 (D19, C5): tambien cierra la exportacion; el future se completa
    // cuando el `.partial` propio ya se descarto y su hilo termino, o al
    // agotarse OpcionesMonitoreo::plazoCierreExportacion (10 s): entonces la
    // restauracion sigue y el `.partial` lo borra el siguiente arranque.
    QFuture<void> detenerParaRestaurar();
    // T025.5 (D22): hay carpetas `pre-restauracion-*` propias sin descartar
    // (mientras existan no se reconcilia la e.firma). Fuera del hilo grafico;
    // el future se completa en un hilo del pool.
    QFuture<bool> hayDatosPrevios() const;
    // T025.5 (D22): borra (sin seguir enlaces) solo las `pre-restauracion-*`
    // creadas por la restauracion y, si ya no queda ninguna, reconcilia la
    // e.firma (borra del almacen las generaciones que la base vigente no
    // referencia). E/S en un hilo del pool; reconciliacion en el dispatcher.
    // Llamar desde el hilo grafico.
    QFuture<ResultadoDescarteDatosPrevios> descartarDatosPrevios();

    // T025.6 (C2, corte B): adaptadores de los puertos de presentacion del
    // respaldo. Viven mientras el root; hilo grafico.
    ControlRespaldoPresentacion& controlRespaldoPresentacion() const;
    ControlRestauracionPresentacion& controlRestauracionPresentacion() const;
    DatosPreviosRestauracionPresentacion& datosPreviosRestauracionPresentacion() const;
    // C4: OSIntegration::imprimirTexto (el `os` de iniciarCicloDeVida). true
    // solo con Enviado; false sin `os`, Cancelado, NoSoportado o Fallido.
    ImpresionTexto impresionTexto();
    // H1/M1: OSIntegration::copiarTextoSecreto (portapapeles con marca de
    // dato sensible). true solo con Copiado; false sin `os`.
    CopiaTextoSecreto copiaTextoSecreto();
    respaldo::EstadoRespaldoGestor estadoRespaldo() const;
    // Arranque del respaldo (en iniciarMonitoreo): configuracion, KEK
    // huerfanas, .partial propios y primera evaluacion del diario.
    QFuture<int> arranqueRespaldo() const { return m_arranqueRespaldo; }
    // T026.1 (D12): destino atomico del ZIP de exportacion (carpeta privada
    // <datos>/exportacion/parciales). Thread-safe.
    exportacion::DestinoExportacion& destinoExportacion() const { return *m_destinoExportacion; }
    // T026.1 (D12): limpieza de `.partial` de exportacion huerfanos, lanzada
    // en iniciarMonitoreo() fuera del hilo grafico. Una exportacion (T026.2)
    // debe empezar despues de que termine. Valor: borrados (-1 si fallo).
    // Antes de iniciarMonitoreo() es un future vacio.
    QFuture<int> limpiezaExportacionInicial() const { return m_limpiezaExportacionInicial; }
    // T026.1 (C2): fabrica de escritores ZIP en streaming (uno por exportacion).
    exportacion::FabricaEscritorZip fabricaEscritorZip() const;
    // T026.2: servicio de exportacion (hilo propio) para T026.3. Puertos de
    // produccion: planificador y visibilidad por el dispatcher, limpieza
    // inicial, destino atomico, escritor en streaming, lector y archivos
    // propios, parser y un RenderizadorPdfCfdi nuevo por exportacion. La
    // eliminacion local le llega por SolicitudesService::solicitudEliminada;
    // la salida (ExtensionWorker) y detenerParaRestaurar() esperan su cerrar().
    // Hilo grafico.
    ExportacionFacturasService& exportacionFacturas() const { return *m_exportacionFacturas; }
    // Id local de la instalacion (lo crea en preferencias.ini la primera vez).
    // Hilo grafico.
    QUuid idInstalacion() const;
    // D21: el .partial va en <datos>/respaldo/parciales si comparte volumen.
    std::shared_ptr<respaldo::DestinoRespaldo> crearDestinoRespaldo(const QString& carpetaAbsoluta) const;
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
    // T025.2: copia de la base (puerto del ejecutor y lector); vive mas que el
    // ejecutor.
    std::unique_ptr<SqliteCopiaRespaldo> m_copiaRespaldo;
    std::unique_ptr<OperacionExecutor> m_ejecutor;
    // T025.2: fuente de captura del respaldo (se destruye antes que el ejecutor).
    std::unique_ptr<FuenteRespaldoLocal> m_fuenteRespaldo;
    // T025.3 (se destruye antes que la fuente: cerrar() espera su trabajo).
    mutable QUuid m_idInstalacion;
    QString m_rutaPreferencias;
    std::unique_ptr<respaldo::RespaldoService> m_respaldoService;
    // T025.4 (destruccion: diario -> gestor -> servicio).
    std::unique_ptr<ConfiguracionRespaldoIni> m_configuracionRespaldo;
    std::unique_ptr<Programador> m_programadorRespaldoPropio;
    std::unique_ptr<respaldo::GestorRespaldo> m_gestorRespaldo;
    std::unique_ptr<respaldo::ProgramadorRespaldoDiario> m_programadorDiario;
    QFuture<int> m_arranqueRespaldo;
    // T026.1: compartido con la limpieza al arrancar (que puede terminar
    // despues del root).
    std::shared_ptr<exportacion::DestinoExportacion> m_destinoExportacion;
    QFuture<int> m_limpiezaExportacionInicial;
    // T026.2: el servicio se destruye antes que sus puertos (une su hilo). C7:
    // lector, archivos y destino son compartidos con el servicio, que los
    // retiene si su destructor abandona el hilo por plazo. El planificador
    // vive hasta cerrar el dispatcher: una tarea de planear() encolada lo
    // referencia (solo desde el hilo grafico, nunca desde el del servicio).
    std::shared_ptr<CfdiArchiveReader> m_lectorExportacion;
    std::shared_ptr<ArchivosZipCfdi> m_archivosExportacion;
    std::unique_ptr<PlanificadorExportacion> m_planificadorExportacion;
    std::unique_ptr<ExportacionFacturasService> m_exportacionFacturas;
    std::chrono::milliseconds m_plazoCierreExportacion{std::chrono::seconds(10)};
    // T025.5 (se destruye primero: sus callbacks usan el root).
    std::unique_ptr<respaldo::RestauracionService> m_restauracion;
    std::optional<ResultadoRestauracionArranque> m_restauracionAlArrancar;
    // T025.6: adaptadores de presentacion (se destruyen antes que los servicios).
    std::unique_ptr<ControlRespaldoGestor> m_controlRespaldo;
    std::unique_ptr<ControlRestauracionServicio> m_controlRestauracion;
    std::unique_ptr<DatosPreviosRestauracionRoot> m_datosPreviosPresentacion;
    QString m_directorioDatos;
    std::unique_ptr<WorkerLocal> m_worker;
    std::unique_ptr<ExtensionWorker> m_extensionWorker;
    std::unique_ptr<AccionesWorker> m_accionesWorker;
    std::unique_ptr<ConsultaExistenciaEjecutor> m_consultaExistencia;
    // T009.1: fachada de acceso (sobre el ejecutor) y orquestacion con Finder.
    std::unique_ptr<AccesoPaquetesEjecutor> m_accesoPaquetes;
    std::unique_ptr<AccesoFinder> m_accesoFinder;
    bool m_monitoreoIniciado = false;
    QFuture<int> m_limpiezaRespaldoInicial;
    std::unique_ptr<PresentacionViewModels> m_viewModels;
    std::unique_ptr<AppLifecycleController> m_controlador;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
};

} // namespace satcfdi
