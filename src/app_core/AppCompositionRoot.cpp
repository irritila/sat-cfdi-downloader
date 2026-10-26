#include "AppCompositionRoot.h"

#include <QCoreApplication>

#include "AccesoFinder.h"
#include "AppLifecycleController.h"
#include "IntegracionWorker.h"
#include "ProgramadorQt.h"
#include "SingleInstanceCoordinator.h"
#include "app_core/FuenteFacturasRoot.h"
#include "application/cfdi/CatalogoMensualCfdiService.h"
#include "application/cfdi/DescargaGuiadaService.h"
#include "application/cfdi/CatalogoCfdiService.h"
#include "application/cfdi/IndexadorCfdi.h"
#include "application/cfdi/PrevisualizacionCfdiService.h"
#include "application/cfdi/ExportacionFacturasService.h"
#include "application/cfdi/PlanificadorExportacion.h"
#include "AppBootstrapper.h"
#include "infrastructure/cfdi/pdf/AlmacenPdfCfdiFs.h"
#include "infrastructure/cfdi/pdf/RenderizadorPdfCfdi.h"
#include "application/configuration/ConfiguracionAppServicePersistido.h"
#include "application/logging/RegexLogSanitizer.h"
#include "application/operaciones/OperacionExecutor.h"
#include "application/notificaciones/ServicioNotificaciones.h"
#include "app_core/FuenteRespaldoLocal.h"
#include "app_core/IdInstalacionRespaldo.h"
#include "app_core/ConfiguracionRespaldoIni.h"
#include "app_core/AreaRestauracionLocal.h"
#include "app_core/DatosPreviosRestauracion.h"
#include "app_core/RespaldoPresentacionRoot.h"
#include "application/respaldo/RestauracionService.h"
#include "infrastructure/persistence/sqlite/SqliteBaseRestauracion.h"
#include "application/respaldo/GestorRespaldo.h"
#include "application/respaldo/ProgramadorRespaldoDiario.h"
#include "application/respaldo/RespaldoService.h"
#include "infrastructure/crypto/ContenedorRespaldoOpenSsl.h"
#include "infrastructure/storage/DestinoRespaldoPosix.h"
#include "infrastructure/storage/DestinoExportacionPosix.h"
#include "infrastructure/cfdi/zip/EscritorZipStreaming.h"
#include "application/estadoagregado/MonitorEstadoAgregado.h"
#include "application/operaciones/OperacionesSatProductivo.h"
#include "application/paquetes/AccesoPaquetesService.h"
#include "application/primeruso/ConsultaPrimerUsoPersistida.h"
#include "application/profiles/ConsultaPreparacionPerfiles.h"
#include "application/vencimiento/AvisoVencimientoEFirma.h"
#include "application/operaciones/WorkerLocal.h"
#include "application/profiles/CredencialesSatServicePersistido.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/PerfilesSatServicePersistido.h"
#include "application/requests/SolicitudesServicePersistido.h"
#include "infrastructure/cfdi/xml/ParserCfdi.h"
#include "infrastructure/cfdi/zip/ArchivosZipCfdiFs.h"
#include "infrastructure/cfdi/zip/LectorZipLibzip.h"
#include "infrastructure/persistence/sqlite/SqliteCopiaRespaldo.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"
#include "infrastructure/storage/FilesystemPackageStorage.h"
#include "infrastructure/sat/SatGatewayProductivo.h"
#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/CatalogoMensajes.h"
#include "presentation/viewmodels/ConsultaCatalogoCfdi.h"
#include "presentation/viewmodels/PrevisualizacionCfdi.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QWindow>

#include <functional>

namespace satcfdi {

namespace {
Q_LOGGING_CATEGORY(lcMonitoreo, "satcfdi.monitoreo")

} // namespace

// T015.3: la presentacion consulta el indice de CFDI por su propia interfaz
// (ConsultaCatalogoCfdi); este adaptador la implementa sobre
// CatalogoCfdiService y reemite sus senales. Solo lectura (y revisarSolicitud).
class AdaptadorCatalogoCfdi final : public ConsultaCatalogoCfdi {
public:
    explicit AdaptadorCatalogoCfdi(CatalogoCfdiService& catalogo)
        : m_catalogo(catalogo)
    {
        QObject::connect(&catalogo, &CatalogoCfdiService::indiceCambiado, this,
                         &ConsultaCatalogoCfdi::indiceCambiado);
        QObject::connect(&catalogo, &CatalogoCfdiService::actividadCambiada, this,
                         &ConsultaCatalogoCfdi::actividadCambiada);
    }

    QFuture<ResultadoLista> listar(const SolicitudId& solicitud, const FiltroListaCfdi& filtro) override
    {
        return m_catalogo.listar(solicitud, filtro);
    }
    QFuture<ResultadoResumen> resumen(const SolicitudId& solicitud) override
    {
        return m_catalogo.resumen(solicitud);
    }
    QFuture<void> revisarSolicitud(const SolicitudId& solicitud) override
    {
        return m_catalogo.revisarSolicitud(solicitud);
    }

private:
    CatalogoCfdiService& m_catalogo;
};

// T015.4 (bloque de interfaz): la presentacion pide el PDF por su propia
// interfaz (PrevisualizacionCfdi); este adaptador la implementa sobre
// PrevisualizacionCfdiService y traduce el tipo de error. Los futures del
// servicio ya se completan en el hilo grafico; el map es trivial y corre ahi.
// Lo posee PresentacionViewModels (hijo QObject), que se destruye antes que
// el servicio.
namespace {

MotivoPrevisualizacion motivoPresentacion(ErrorPrevisualizacion::Tipo tipo)
{
    switch (tipo) {
    case ErrorPrevisualizacion::Tipo::OrigenNoDisponible:
        return MotivoPrevisualizacion::OrigenNoDisponible;
    case ErrorPrevisualizacion::Tipo::VersionNoSoportada:
        return MotivoPrevisualizacion::VersionNoSoportada;
    case ErrorPrevisualizacion::Tipo::Ilegible:
        return MotivoPrevisualizacion::Ilegible;
    case ErrorPrevisualizacion::Tipo::Cancelada:
        return MotivoPrevisualizacion::Cancelada;
    case ErrorPrevisualizacion::Tipo::Fallo:
        break;
    }
    return MotivoPrevisualizacion::Fallo;
}

class AdaptadorPrevisualizacionCfdi final : public PrevisualizacionCfdi {
public:
    AdaptadorPrevisualizacionCfdi(PrevisualizacionCfdiService& servicio, QObject* parent)
        : PrevisualizacionCfdi(parent)
        , m_servicio(servicio)
    {
    }

    QFuture<ResultadoGenerar> generar(const SolicitudId& solicitud, const QString& paqueteId, int posicion) override
    {
        return m_servicio.generar(solicitud, paqueteId, posicion)
            .then(QtFuture::Launch::Sync, [](const PrevisualizacionCfdiService::ResultadoPdf& r) {
                return r.esExito() ? ResultadoGenerar::exito(PdfPrevisualizado{r.valor().rutaArchivo,
                                                                               r.valor().nombreSugerido})
                                   : ResultadoGenerar::fallo(motivoPresentacion(r.error().tipo));
            });
    }
    QFuture<ResultadoGuardar> guardarComo(const QString& rutaPdf, const QString& destinoAbsoluto) override
    {
        return m_servicio.guardarComo(rutaPdf, destinoAbsoluto)
            .then(QtFuture::Launch::Sync, [](const PrevisualizacionCfdiService::ResultadoGuardar& r) {
                return r.esExito() ? ResultadoGuardar::exito(r.valor())
                                   : ResultadoGuardar::fallo(motivoPresentacion(r.error().tipo));
            });
    }

private:
    PrevisualizacionCfdiService& m_servicio;
};

} // namespace

AppCompositionRoot::AppCompositionRoot(const QString& rutaBase, SecretStore& secretStore,
                                       OpcionesMonitoreo monitoreo)
    : m_persistencia(std::make_unique<SqlitePersistencia>(rutaBase))
    , m_dispatcher(std::make_unique<PersistenceDispatcher>())
    , m_sanitizer(std::make_unique<RegexLogSanitizer>())
{
    SqlitePersistencia& p = *m_persistencia;
    const PuertosPersistencia puertos{p.perfiles(), p.solicitudes(), p.paquetes(),
                                      p.logs(),     p.unidadDeTrabajo(), *m_sanitizer,
                                      &p.configuracion()};

    m_perfilesService = std::make_unique<PerfilesSatServicePersistido>(
        *m_dispatcher, p.perfiles(), p.unidadDeTrabajo());
    m_solicitudesService = std::make_unique<SolicitudesServicePersistido>(*m_dispatcher, puertos);
    m_configuracionService = std::make_unique<ConfiguracionAppServicePersistido>(
        *m_dispatcher, p.configuracion(), p.unidadDeTrabajo());
    m_credencialesService = std::make_unique<CredencialesSatServicePersistido>(
        *m_dispatcher, p.perfiles(), p.credenciales(), p.unidadDeTrabajo(), secretStore);
    // T005.1: la presentacion de perfiles y e.firma consume el mismo servicio
    // de credenciales persistido.
    // T007: ejecutor serial con su propio hilo y conexion SQLite (la de su
    // hilo en SqlitePersistencia), puerto SAT (productivo desde T009) y worker.
    const RelojUtc reloj = monitoreo.reloj ? monitoreo.reloj : relojSistema();
    Programador* programadorEjecutor = monitoreo.programadorEjecutor;
    if (!programadorEjecutor) {
        m_programadorEjecutorPropio = std::make_unique<ProgramadorQt>(reloj);
        programadorEjecutor = m_programadorEjecutorPropio.get();
    }
    Programador* programadorWorker = monitoreo.programadorWorker;
    if (!programadorWorker) {
        m_programadorWorkerPropio = std::make_unique<ProgramadorQt>(reloj);
        programadorWorker = m_programadorWorkerPropio.get();
    }
    // T008 D8: almacenamiento de ZIP. La raiz la crea AppBootstrapper al
    // arrancar, en su hilo de E/S (nunca en el hilo grafico); el adaptador
    // tambien la crea al primer guardado.
    m_avisoRaizPaquetes = monitoreo.avisoRaizPaquetes;
    m_packageStorage = monitoreo.packageStorage;
    if (!m_packageStorage) {
        m_raizPaquetes = monitoreo.raizPaquetes.isEmpty()
                             ? QDir(QFileInfo(rutaBase).absolutePath()).filePath(QStringLiteral("paquetes"))
                             : monitoreo.raizPaquetes;
        m_packageStoragePropio = std::make_unique<FilesystemPackageStorage>(m_raizPaquetes);
        m_packageStorage = m_packageStoragePropio.get();
    }

    // T009: SatGateway productivo (o inyectado). Se crea en el hilo grafico
    // pero SOLO se invoca desde el hilo del ejecutor (adaptador OperacionesSat).
    m_satGateway = monitoreo.satGateway;
    if (!m_satGateway) {
        SatGatewayOptions opciones;
        opciones.reloj = reloj;
        m_satGatewayPropio = std::make_unique<SatGatewayProductivo>(std::move(opciones));
        m_satGateway = m_satGatewayPropio.get();
    }

    // T009: OperacionesSat productivo (o inyectado). Se crea antes del
    // ejecutor y se destruye despues (su destructor cierra las sesiones). Un
    // cambio de credencial invalida la sesion de token de ese perfil.
    OperacionesSat* sat = monitoreo.operacionesSat;
    if (!sat) {
        auto productivo = std::make_unique<OperacionesSatProductivo>(*m_satGateway, *m_credencialesService,
                                                                     *m_packageStorage, reloj);
        OperacionesSatProductivo* operaciones = productivo.get();
        m_conexionInvalidarSesion = QObject::connect(m_credencialesService.get(), &CredencialesSatService::credencialCambio,
                         m_credencialesService.get(), [operaciones](const QString& id) {
                             if (const auto perfil = PerfilId::desdeTexto(id)) {
                                 operaciones->invalidarSesion(*perfil);
                             }
                         });
        m_operacionesSatPropio = std::move(productivo);
        sat = m_operacionesSatPropio.get();
    }

    SqlitePersistencia* persistencia = m_persistencia.get();
    m_copiaRespaldo = std::make_unique<SqliteCopiaRespaldo>(p);
    PuertosEjecutor puertosEjecutor{p.solicitudes(), p.logs(), p.operaciones(), p.unidadDeTrabajo(), *m_sanitizer,
                                    *sat, [persistencia] { persistencia->cerrarConexionDelHiloActual(); },
                                    &p.paquetes(), m_packageStorage, m_copiaRespaldo.get()};
    m_ejecutor = std::make_unique<OperacionExecutor>(std::move(puertosEjecutor), reloj, *programadorEjecutor);
    // T025.2: captura del respaldo. Staging en <directorio de la base>/respaldo-tmp.
    {
        FuenteRespaldoLocal::Opciones opcionesRespaldo;
        opcionesRespaldo.directorioDatos = QFileInfo(rutaBase).absolutePath();
        opcionesRespaldo.raizPaquetes = m_raizPaquetes;
        opcionesRespaldo.rutaPreferencias =
            QDir(opcionesRespaldo.directorioDatos).filePath(QStringLiteral("preferencias.ini"));
        opcionesRespaldo.versionApp = QStringLiteral(SATCFDI_VERSION_APP);
        opcionesRespaldo.reloj = reloj;
        // El lector (sin estado) se comparte con las continuaciones de captura,
        // que pueden terminar despues del root.
        m_fuenteRespaldo = std::make_unique<FuenteRespaldoLocal>(
            *m_ejecutor, std::make_shared<SqliteLectorCopiaRespaldo>(), std::move(opcionesRespaldo));
        // Stagings de un proceso anterior que termino a mitad de un respaldo:
        // se borran fuera del hilo grafico. La tarea retiene el estado
        // compartido (no el objeto) y nunca toca una captura activa, asi que una
        // captura inmediata no necesita esperarla.
        m_limpiezaRespaldoInicial = m_dispatcher->despachar<int>(m_fuenteRespaldo->tareaLimpiezaHuerfanos());

        // T025.3: id local (preferencias.ini, seccion respaldo; no se
        // respalda). Perezoso: se crea al primer respaldo o limpieza, no al
        // arrancar (arrancar no escribe en el directorio de datos).
        m_rutaPreferencias = QDir(QFileInfo(rutaBase).absolutePath()).filePath(QStringLiteral("preferencias.ini"));
        respaldo::RespaldoService::Opciones opcionesServicio;
        opcionesServicio.proveedorIdInstalacion = [this] { return idInstalacion(); };
        // D20: registro local de lo publicado, en los datos de la app (ni en
        // SQLite ni en el respaldo). Se crea al primer respaldo.
        opcionesServicio.registro = std::make_shared<RegistroPublicadosJson>(
            QDir(QFileInfo(rutaBase).absolutePath()).filePath(QStringLiteral("respaldo/publicados.json")));
        opcionesServicio.reloj = reloj;
        auto contenedor = std::make_shared<crypto::ContenedorRespaldoOpenSsl>();
        m_respaldoService = std::make_unique<respaldo::RespaldoService>(*m_fuenteRespaldo, contenedor,
                                                                        std::move(opcionesServicio));

        // T025.4: configuracion local, KEK y diario.
        m_configuracionRespaldo = std::make_unique<ConfiguracionRespaldoIni>(m_rutaPreferencias);
        respaldo::GestorRespaldo::Dependencias dependencias;
        dependencias.configuracion = m_configuracionRespaldo.get();
        dependencias.almacenKek = monitoreo.almacenKekRespaldo;
        dependencias.carpetas = monitoreo.carpetaRespaldoSistema;
        dependencias.contenedor = contenedor;
        dependencias.servicio = m_respaldoService.get();
        const QString parciales =
            QDir(QFileInfo(rutaBase).absolutePath()).filePath(QStringLiteral("respaldo/parciales"));
        dependencias.fabricaDestino = [parciales](const QString& ruta, const QString& uuidVolumen) {
            DestinoRespaldoPosix::Opciones o;
            o.carpetaParciales = parciales;
            o.uuidVolumen = uuidVolumen;
            return std::shared_ptr<respaldo::DestinoRespaldo>(std::make_shared<DestinoRespaldoPosix>(ruta, std::move(o)));
        };
        dependencias.reloj = reloj;
        m_gestorRespaldo = std::make_unique<respaldo::GestorRespaldo>(std::move(dependencias));
        Programador* programadorRespaldo = monitoreo.programadorRespaldo;
        if (!programadorRespaldo) {
            m_programadorRespaldoPropio = std::make_unique<ProgramadorQt>(reloj);
            programadorRespaldo = m_programadorRespaldoPropio.get();
        }
        respaldo::ProgramadorRespaldoDiario::Opciones diario;
        if (monitoreo.intervaloRespaldoDiario.count() > 0) {
            diario.intervalo = monitoreo.intervaloRespaldoDiario;
        }
#if !defined(NDEBUG)
        // Solo Debug: verificacion manual del despertar y de `pmset` antes de
        // T025.6 (p. ej. 120 s). Nunca en Release.
        if (const int prueba = qEnvironmentVariableIntValue("SATCFDI_RESPALDO_INTERVALO_PRUEBA_S"); prueba > 0) {
            diario.intervalo = std::chrono::seconds(prueba);
            diario.esperaTrasFallo = std::chrono::seconds(prueba);
        }
#endif
        m_programadorDiario = std::make_unique<respaldo::ProgramadorRespaldoDiario>(*m_gestorRespaldo, *programadorRespaldo,
                                                                                  reloj, diario);

        // T025.5: restauracion. Staging y marcador junto a la base y en el
        // volumen de la raiz efectiva de paquetes (T019).
        respaldo::RestauracionService::Dependencias restauracion;
        restauracion.contenedor = contenedor;
        restauracion.area = std::make_shared<AreaRestauracionLocal>(
            AreaRestauracionLocal::Opciones{QFileInfo(rutaBase).absolutePath(), m_raizPaquetes, {}});
        restauracion.base = std::make_shared<SqliteBaseRestauracion>();
        restauracion.detenerApp = [this] { return detenerParaRestaurar(); };
        restauracion.relanzar = [this] {
            return m_os && m_os->solicitarRelanzamiento() == OSIntegration::ResultadoRelanzamiento::Programado;
        };
        restauracion.salir = [this] {
            if (m_controlador) {
                m_controlador->solicitarSalida();
            } else {
                QCoreApplication::exit(0);
            }
        };
        m_restauracion = std::make_unique<respaldo::RestauracionService>(std::move(restauracion));

        // T025.6 (corte B): adaptadores de presentacion.
        m_controlRespaldo = std::make_unique<ControlRespaldoGestor>(
            *m_gestorRespaldo, monitoreo.almacenKekRespaldo != nullptr && monitoreo.carpetaRespaldoSistema != nullptr);
        m_controlRestauracion = std::make_unique<ControlRestauracionServicio>(*m_restauracion);
        m_datosPreviosPresentacion = std::make_unique<DatosPreviosRestauracionRoot>(*this);
    }
    m_restauracionAlArrancar = monitoreo.restauracionAlArrancar;
    m_directorioDatos = QFileInfo(rutaBase).absolutePath();
    // T026.1 (D12): destino de la exportacion; su limpieza corre en
    // iniciarMonitoreo(). Construirlo no escribe en disco.
    {
        DestinoExportacionPosix::Opciones o;
        o.directorioDatos = m_directorioDatos;
        m_destinoExportacion = std::make_shared<DestinoExportacionPosix>(std::move(o));
    }
    m_worker = std::make_unique<WorkerLocal>(*m_ejecutor, *m_configuracionService, *programadorWorker, reloj);
    // T026.2 (D19): la salida tambien espera el cierre de la exportacion
    // (cancelada y sin `.partial`); el servicio se crea mas abajo.
    if (monitoreo.plazoCierreExportacion.count() > 0) {
        m_plazoCierreExportacion = monitoreo.plazoCierreExportacion;
    }
    m_extensionWorker = std::make_unique<ExtensionWorker>(
        *m_worker, *m_ejecutor,
        [this] { return m_exportacionFacturas ? m_exportacionFacturas->cerrar() : QtFuture::makeReadyVoidFuture(); },
        m_plazoCierreExportacion);
    m_accionesWorker = std::make_unique<AccionesWorker>(*m_worker);
    m_consultaExistencia = std::make_unique<ConsultaExistenciaEjecutor>(*m_dispatcher, p.paquetes(), *m_ejecutor);
    m_accesoPaquetes = std::make_unique<AccesoPaquetesEjecutor>(*m_ejecutor);
    m_accesoFinder = std::make_unique<AccesoFinder>(*m_accesoPaquetes, *m_dispatcher, p.paquetes());

    // Los cambios aplicados por el ejecutor refrescan la UI por las senales
    // del servicio de solicitudes (lista y detalle).
    SolicitudesService* solicitudes = m_solicitudesService.get();
    QObject::connect(m_ejecutor.get(), &OperacionExecutor::solicitudActualizada, solicitudes,
                     [solicitudes](const SolicitudId& id) {
                         emit solicitudes->solicitudActualizada(id);
                         emit solicitudes->listaCambiada();
                     });

    // D9: cambio del estado de credencial observado por el gate del worker.
    // Diagnostico de app (solo la clave del estado; sin RFC ni perfil) y
    // evento para la UI: credencialCambio hace que PerfilesSatViewModel
    // reverifique ese perfil y que el selector de nueva solicitud se recargue.
    CredencialesSatService* credenciales = m_credencialesService.get();
    QObject::connect(m_ejecutor.get(), &OperacionExecutor::estadoCredencialCambiado, credenciales,
                     [credenciales](const PerfilId& perfil, EstadoCredencial estado) {
                         qCInfo(lcMonitoreo, "estado de credencial de un perfil cambio a %s",
                                qUtf8Printable(claveEstable(estado)));
                         emit credenciales->credencialCambio(perfil.texto());
                     });

    // T009 D1/D9: notificaciones decididas por la aplicacion (una sola fuente,
    // tambien las de credencial) solo tras commit, entregadas por OSIntegration
    // cuando exista (iniciarCicloDeVida). El resultado no vuelve a la app.
    m_notificador = std::make_unique<NotificadorOS>();
    m_servicioNotificaciones = std::make_unique<ServicioNotificaciones>(*m_notificador);
    QObject::connect(m_ejecutor.get(), &OperacionExecutor::transicionConfirmada, m_servicioNotificaciones.get(),
                     &ServicioNotificaciones::alConfirmarTransicion);
    QObject::connect(m_ejecutor.get(), &OperacionExecutor::estadoCredencialCambiado,
                     m_servicioNotificaciones.get(), &ServicioNotificaciones::alCambiarEstadoCredencial);

    // T015.2: indice de CFDI. Calculo (stat, SHA-256, ZIP y parseo) en el
    // hilo propio del indexador; lecturas y escrituras por el dispatcher (una
    // transaccion por paquete). Raiz: la de paquetes (o la de por defecto si
    // se inyecto el almacenamiento).
    {
        CfdiArchiveReader* lector = monitoreo.lectorCfdi;
        if (!lector) {
            m_lectorCfdiPropio = std::make_unique<LectorZipLibzip>();
            lector = m_lectorCfdiPropio.get();
        }
        const QString raizIndice =
            !m_raizPaquetes.isEmpty() ? m_raizPaquetes
            : !monitoreo.raizPaquetes.isEmpty()
                ? monitoreo.raizPaquetes
                : QDir(QFileInfo(rutaBase).absolutePath()).filePath(QStringLiteral("paquetes"));
        m_archivosCfdi = std::make_unique<ArchivosZipCfdiFs>(raizIndice);
        PuertosIndexadorCfdi puertosIndice{*m_dispatcher,
                                           p.catalogoCfdi(),
                                           p.unidadDeTrabajo(),
                                           *lector,
                                           *m_archivosCfdi,
                                           [](QByteArrayView xml) { return parsearCfdi(xml); },
                                           monitoreo.versionParserCfdi > 0 ? monitoreo.versionParserCfdi
                                                                           : kVersionParserCfdi,
                                           reloj};
        m_indexadorCfdi = std::make_unique<IndexadorCfdi>(std::move(puertosIndice));
        m_catalogoCfdi = std::make_unique<CatalogoCfdiService>(*m_dispatcher, p.catalogoCfdi(), *m_indexadorCfdi);
        // T023.2/T023.3: flujo mensual guiado (catalogo y disponibilidad del
        // mes; descarga guiada). Cableado minimo: gate de e.firma por el
        // ejecutor (D9) y envio por el worker (solo encola: el worker no crea
        // solicitudes).
        m_catalogoMensual = std::make_unique<CatalogoMensualCfdiService>(
            *m_dispatcher, p.catalogoMensual(), p.lotesDescargaGuiada(), *m_indexadorCfdi, reloj);
        OperacionExecutor* ejecutor = m_ejecutor.get();
        WorkerLocal* worker = m_worker.get();
        m_descargaGuiada = std::make_unique<DescargaGuiadaService>(PuertosDescargaGuiada{
            *m_dispatcher, puertos, p.catalogoMensual(), p.lotesDescargaGuiada(), *m_indexadorCfdi,
            [ejecutor](const PerfilId& perfil) {
                return ejecutor->consultarCredenciales({perfil}).then(
                    [perfil](const QList<std::pair<PerfilId, EstadoCredencial>>& estados) {
                        for (const auto& [id, estado] : estados) {
                            if (id == perfil) {
                                return std::optional(estado);
                            }
                        }
                        return std::optional<EstadoCredencial>();
                    });
            },
            [worker](const SolicitudId& id) { worker->enviar(id); }, reloj});
        SolicitudesService* listaSolicitudes = m_solicitudesService.get();
        QObject::connect(m_descargaGuiada.get(), &DescargaGuiadaService::mesActualizado, listaSolicitudes,
                         [listaSolicitudes] { emit listaSolicitudes->listaCambiada(); });
        // (b) Tras cada commit del ejecutor (incluida la descarga de un
        // paquete) se sincroniza el indice de esa solicitud.
        IndexadorCfdi* indexador = m_indexadorCfdi.get();
        QObject::connect(m_ejecutor.get(), &OperacionExecutor::solicitudActualizada, indexador,
                         [indexador](const SolicitudId& id) { indexador->sincronizarSolicitud(id); });
        // Eliminar una solicitud borra su indice en la misma transaccion
        // (trigger de 006): se anuncia para recargar el explorador.
        CatalogoCfdiService* catalogo = m_catalogoCfdi.get();
        QObject::connect(m_solicitudesService.get(), &SolicitudesService::solicitudEliminada, catalogo,
                         [catalogo](const SolicitudId& id) { emit catalogo->indiceCambiado(id); });

        // T015.4: previsualizacion PDF en su propio hilo (nunca el grafico ni
        // la cola SAT). Lector y archivos propios (sin estado; los del
        // indexador pueden ser fakes con compuertas en pruebas). Reindexa con
        // el indexador. Cache D7: con --data-dir (o en pruebas) dentro del
        // directorio de datos; si no, CacheLocation del usuario.
        const QString directorioDatos = QDir::cleanPath(QFileInfo(rutaBase).absolutePath());
        m_directorioCachePdf = QDir::cleanPath(
            !monitoreo.directorioCachePdf.isEmpty() ? QDir(monitoreo.directorioCachePdf).absolutePath()
            : directorioDatos == QDir::cleanPath(AppBootstrapper::directorioDatosPorDefecto())
                ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                      .filePath(QStringLiteral("cfdi-pdf"))
                : QDir(directorioDatos).filePath(QStringLiteral("cache/cfdi-pdf")));
        m_directorioTemporalPdf = QDir::cleanPath(
            !monitoreo.directorioTemporalPdf.isEmpty()
                ? QDir(monitoreo.directorioTemporalPdf).absolutePath()
                : QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                      .filePath(QStringLiteral("satcfdi-cfdi-pdf")));
        m_lectorPdf = std::make_unique<LectorZipLibzip>();
        m_archivosPdf = std::make_unique<ArchivosZipCfdiFs>(raizIndice);
        m_renderizadorPdf = std::make_unique<RenderizadorPdfCfdi>();
        m_almacenPdf = std::make_unique<AlmacenPdfCfdiFs>(m_directorioCachePdf, m_directorioTemporalPdf,
                                                          monitoreo.limiteCachePdfBytes);
        PuertosPrevisualizacionCfdi puertosPdf{*m_dispatcher,
                                               p.catalogoCfdi(),
                                               *m_lectorPdf,
                                               *m_archivosPdf,
                                               [](QByteArrayView xml) { return parsearCfdi(xml); },
                                               *m_renderizadorPdf,
                                               *m_almacenPdf,
                                               [indexador](const SolicitudId& id) {
                                                   return indexador->revisarSolicitud(id);
                                               },
                                               indexador->versionParser()};
        m_previsualizacionCfdi = std::make_unique<PrevisualizacionCfdiService>(std::move(puertosPdf));
        // "Abrir en Vista Previa" solo acepta PDF de la cache o del temporal.
        m_accesoFinder->setDirectoriosVistaPrevia({m_directorioCachePdf, m_directorioTemporalPdf});

        // T026.2: exportacion de facturas en su propio hilo (ADR 0023; nunca
        // el grafico ni la cola SAT). Plan y visibilidad por el dispatcher;
        // lector y archivos propios (sin estado); un renderer nuevo por
        // exportacion (D13). La indexacion en curso no se consulta al planear
        // (IndexadorCfdi::cubre es del hilo grafico): un mes con paquetes aun
        // sin indexar se informa como "cobertura incompleta".
        m_lectorExportacion = monitoreo.lectorExportacion ? monitoreo.lectorExportacion
                                                          : std::make_shared<LectorZipLibzip>();
        m_archivosExportacion = std::make_shared<ArchivosZipCfdiFs>(raizIndice);
        m_planificadorExportacion =
            std::make_unique<PlanificadorExportacion>(p.catalogoMensual(), reloj, indexador->versionParser());
        PuertosExportacionFacturas puertosExportacion{
            ExportacionFacturasService::planearPorDispatcher(*m_dispatcher, *m_planificadorExportacion),
            ExportacionFacturasService::paqueteExportablePorCatalogo(*m_dispatcher, p.catalogoCfdi()),
            [this] { return m_limpiezaExportacionInicial; },
            m_destinoExportacion,
            fabricaEscritorZip(),
            m_lectorExportacion,
            m_archivosExportacion,
            [](QByteArrayView xml) { return parsearCfdi(xml); },
            []() -> std::unique_ptr<CfdiPdfRenderer> { return std::make_unique<RenderizadorPdfCfdi>(); },
            reloj, // por valor; no depende de la vida del root (C7)
            m_plazoCierreExportacion, // C5: tambien acota la espera del destructor
            {}};
        m_exportacionFacturas = std::make_unique<ExportacionFacturasService>(std::move(puertosExportacion));
        // D19: eliminar localmente omite al instante las facturas pendientes
        // de esa solicitud (ademas de la revalidacion por paquete).
        QObject::connect(m_solicitudesService.get(), &SolicitudesService::solicitudEliminada,
                         m_exportacionFacturas.get(), &ExportacionFacturasService::notificarSolicitudEliminada);
    }

    // T014.3 D1: primer uso (lectura en el dispatcher). D2/D3: aviso de
    // vencimiento de e.firma; arranca con iniciarMonitoreo() y reevalua al
    // cambiar una credencial (un reemplazo reinicia el dedupe).
    m_consultaPrimerUso = std::make_unique<ConsultaPrimerUsoPersistida>(*m_dispatcher, p.perfiles(), p.solicitudes(),
                                                                        p.credenciales(), secretStore, reloj);
    m_consultaVencimiento = std::make_unique<ConsultaPreparacionPerfiles>(*m_perfilesService, *m_credencialesService,
                                                                          nullptr, reloj);
    Programador* programadorVencimiento = monitoreo.programadorVencimiento;
    if (!programadorVencimiento) {
        m_programadorVencimientoPropio = std::make_unique<ProgramadorQt>(reloj);
        programadorVencimiento = m_programadorVencimientoPropio.get();
    }
    ConsultaPreparacionPerfiles* consultaVencimiento = m_consultaVencimiento.get();
    m_avisoVencimiento = std::make_unique<AvisoVencimientoEFirma>(
        [consultaVencimiento] { return consultaVencimiento->listarVerificados(); }, *m_dispatcher,
        p.avisosVencimiento(), p.unidadDeTrabajo(), *m_servicioNotificaciones, *programadorVencimiento, reloj);
    QObject::connect(m_credencialesService.get(), &CredencialesSatService::credencialCambio, m_avisoVencimiento.get(),
                     &AvisoVencimientoEFirma::alCambiarCredencial);

    // T014.4 D2: estado agregado del icono. Consultas de solo lectura (una
    // tarea del dispatcher para solicitudes/paquetes; preparacion de perfiles
    // por metadata). Recalcula al cambiar el worker, la lista de solicitudes
    // (creacion, transiciones confirmadas del ejecutor, eliminacion), los
    // perfiles o una credencial. Se inicia en iniciarCicloDeVida().
    {
        PersistenceDispatcher* dispatcher = m_dispatcher.get();
        SolicitudMasivaRepository* repoSolicitudes = &p.solicitudes();
        PaqueteSolicitudRepository* repoPaquetes = &p.paquetes();
        m_estadoAgregado = std::make_unique<MonitorEstadoAgregado>(
            [dispatcher, repoSolicitudes, repoPaquetes] {
                return consultarAtencionSolicitudes(*dispatcher, *repoSolicitudes, *repoPaquetes);
            },
            [consultaVencimiento] { return consultarAtencionPerfiles(*consultaVencimiento); });
        MonitorEstadoAgregado* monitor = m_estadoAgregado.get();
        QObject::connect(m_worker.get(), &WorkerLocal::instantaneaCambiada, monitor,
                         &MonitorEstadoAgregado::alCambiarWorker);
        QObject::connect(m_solicitudesService.get(), &SolicitudesService::listaCambiada, monitor,
                         &MonitorEstadoAgregado::refrescarSolicitudes);
        QObject::connect(m_solicitudesService.get(), &SolicitudesService::solicitudEliminada, monitor,
                         &MonitorEstadoAgregado::refrescarSolicitudes);
        QObject::connect(m_perfilesService.get(), &PerfilesSatService::perfilesCambiaron, monitor,
                         &MonitorEstadoAgregado::refrescarPerfiles);
        QObject::connect(m_credencialesService.get(), &CredencialesSatService::credencialCambio, monitor,
                         &MonitorEstadoAgregado::refrescarPerfiles);
    }

    m_viewModels = std::make_unique<PresentacionViewModels>(
        m_solicitudesService.get(), m_perfilesService.get(), m_credencialesService.get(), nullptr,
        reloj); // T014.3: mismo reloj que AvisoVencimientoEFirma (badge = aviso)
    if (m_avisoRaizPaquetes) { // T019 D8: aviso de la carpeta de paquetes (fijo en la sesion)
        using MotivoVm = AvisoRaizPaquetesViewModel::Motivo;
        const auto& a = *m_avisoRaizPaquetes;
        const MotivoVm motivo = a.motivo == AvisoRaizPaquetes::Motivo::Conflicto ? MotivoVm::Conflicto
                                : a.motivo == AvisoRaizPaquetes::Motivo::Error   ? MotivoVm::Error
                                                                                 : MotivoVm::RaizNoDisponible;
        m_viewModels->avisoRaizPaquetes()->fijar({motivo, a.rutaLegada, a.rutaNueva, a.rutaEfectiva});
    }
    m_viewModels->setAccionesSolicitud(m_accionesWorker.get());
    m_viewModels->setConsultaExistencia(m_consultaExistencia.get());
    m_viewModels->setAccionesFinder(m_accesoFinder.get());
    m_viewModels->setConsultaPrimerUso(m_consultaPrimerUso.get()); // T014.3 D1
    m_adaptadorCatalogo = std::make_unique<AdaptadorCatalogoCfdi>(*m_catalogoCfdi);
    m_viewModels->setConsultaCatalogo(m_adaptadorCatalogo.get()); // T015.3
    // T015.4 (interfaz): visor PDF del explorador sobre el servicio de core.
    m_viewModels->setPrevisualizacionCfdi(
        new AdaptadorPrevisualizacionCfdi(*m_previsualizacionCfdi, m_viewModels.get()));
    // T023.4: pantalla Facturas (pagina inicial) y ultimo perfil usado.
    m_consultaPerfilesFacturas = std::make_unique<ConsultaPreparacionPerfiles>(*m_perfilesService,
                                                                               *m_credencialesService, nullptr, reloj);
    m_fuenteFacturas = std::make_unique<FuenteFacturasRoot>(*m_catalogoMensual, *m_descargaGuiada,
                                                            *m_consultaPerfilesFacturas, reloj);
    {
        FuenteFacturasRoot* fuente = m_fuenteFacturas.get();
        QObject::connect(m_ejecutor.get(), &OperacionExecutor::solicitudActualizada, fuente,
                         [fuente] { fuente->avisarCambio(); });
        QObject::connect(m_catalogoCfdi.get(), &CatalogoCfdiService::indiceCambiado, fuente,
                         [fuente] { fuente->avisarCambio(); });
        QObject::connect(m_solicitudesService.get(), &SolicitudesService::solicitudEliminada, fuente,
                         [fuente] { fuente->avisarCambio(); });
        QObject::connect(m_perfilesService.get(), &PerfilesSatService::perfilesCambiaron, fuente,
                         &FuenteFacturasMes::perfilesCambiados);
        QObject::connect(m_credencialesService.get(), &CredencialesSatService::credencialCambio, fuente,
                         [fuente] { emit fuente->perfilesCambiados(); });
    }
    m_preferenciasFacturas = std::make_unique<PreferenciasFacturasArchivo>(
        QDir(QFileInfo(rutaBase).absolutePath()).filePath(QStringLiteral("preferencias.ini")));
    m_viewModels->setFuenteFacturas(m_fuenteFacturas.get(), m_preferenciasFacturas.get());
    // T026.3: exportacion de facturas desde Facturas, con el mismo reloj.
    m_viewModels->setExportacionFacturas(m_exportacionFacturas.get(), reloj);
    // T023.5: Historial y destino de notificaciones de solicitud.
    m_fuenteHistorial = std::make_unique<FuenteHistorialRoot>(*m_catalogoMensual, *m_fuenteFacturas);
    m_viewModels->setFuenteHistorial(m_fuenteHistorial.get());
    // T025.6 (C2): pagina Respaldo y franja de avisos. Antes de cargar():
    // initialProperties ya incluye respaldoViewModel.
    m_viewModels->setControlRespaldo(m_controlRespaldo.get());
    m_viewModels->setControlRestauracion(m_controlRestauracion.get());
    m_viewModels->setDatosPreviosRestauracion(m_datosPreviosPresentacion.get());
    m_viewModels->setImpresionTexto(impresionTexto());
    m_viewModels->setCopiaTextoSecreto(copiaTextoSecreto());
    m_viewModels->setResolucionDestinos(m_fuenteHistorial.get());

    // Primera tarea del dispatcher serial: limpia generaciones huerfanas del
    // SecretStore (residuos de fallos previos). No se espera aqui; las
    // operaciones de credencial posteriores se encolan detras.
    // T025.5 (D22): con datos previos de una restauracion sin descartar no se
    // borran generaciones (las referencia la base de pre-restauracion).
    m_reconciliacionInicial = m_credencialesService->reconciliarSalvo(
        [datos = m_directorioDatos] { return datos_previos_restauracion::bloqueanReconciliacion(datos); });
}

AppCompositionRoot::~AppCompositionRoot()
{
    // 1. Consumidores, del mas externo al mas interno.
    m_engine.reset();
    m_controlador.reset();
    m_viewModels.reset();
    // T025.6: adaptadores de presentacion antes que sus servicios.
    m_datosPreviosPresentacion.reset();
    m_controlRestauracion.reset();
    m_controlRespaldo.reset();
    m_restauracion.reset(); // T025.5: sus callbacks usan este root
    // T026.2: cancela, descarta su `.partial` y une su hilo antes que sus
    // puertos (el planificador vive hasta cerrar el dispatcher).
    m_exportacionFacturas.reset();
    m_archivosExportacion.reset(); // C7: el hilo abandonado conserva los suyos
    m_lectorExportacion.reset();

    // T007: el ejecutor termina antes que el dispatcher (y su hilo cierra su
    // propia conexion). Si la salida explicita ya lo detuvo, es inmediato.
    m_estadoAgregado.reset(); // T014.4: antes que sus consultas y servicios
    // T015.2: el indexador cancela su paquete en curso (sin escribirlo) y une
    // su hilo; sus escrituras ya encoladas se completan en dispatcher.cerrar().
    m_adaptadorCatalogo.reset(); // T015.3: sus consumidores (view models) ya no existen
    // T015.4: antes que el indexador (lo usa para reindexar) y el dispatcher;
    // une su hilo. Despues, sus puertos.
    m_previsualizacionCfdi.reset();
    m_almacenPdf.reset();
    m_renderizadorPdf.reset();
    m_archivosPdf.reset();
    m_lectorPdf.reset();
    m_fuenteHistorial.reset(); // T023.5: antes que la fuente de Facturas y el catalogo
    m_fuenteFacturas.reset(); // T023.4: antes que sus servicios
    m_consultaPerfilesFacturas.reset();
    m_descargaGuiada.reset();  // T023.3: antes que indexador, worker, ejecutor y dispatcher
    m_catalogoMensual.reset(); // T023.2
    m_catalogoCfdi.reset();
    m_indexadorCfdi.reset();
    m_archivosCfdi.reset();
    m_lectorCfdiPropio.reset();
    m_avisoVencimiento.reset(); // cancela su programacion; antes que notificaciones
    m_programadorVencimientoPropio.reset();
    m_consultaVencimiento.reset();
    m_consultaPrimerUso.reset();
    m_servicioNotificaciones.reset();
    m_accesoFinder.reset();
    m_accesoPaquetes.reset();
    m_accionesWorker.reset();
    m_consultaExistencia.reset();
    m_extensionWorker.reset();
    if (m_worker) {
        m_worker->detener();
    }
    m_worker.reset();
    // T025.2: la limpieza inicial termina antes de soltar la fuente (no usa
    // `this`, pero asi el cierre no deja borrados a medias).
    m_limpiezaRespaldoInicial.waitForFinished();
    // T025.4: diario y gestor antes que el servicio (el gestor une su hilo).
    m_programadorDiario.reset();
    m_gestorRespaldo.reset();
    m_programadorRespaldoPropio.reset();
    // T025.3: cancela, abandona una captura pendiente y une su hilo.
    m_respaldoService.reset();
    m_configuracionRespaldo.reset();
    m_fuenteRespaldo.reset(); // antes que el ejecutor; sus capturas en curso retienen su estado
    m_ejecutor.reset(); // ~OperacionExecutor: cancela, espera y cierra su conexion en su hilo
    m_copiaRespaldo.reset();
    m_programadorWorkerPropio.reset();
    m_programadorEjecutorPropio.reset();
    QObject::disconnect(m_conexionInvalidarSesion);
    m_operacionesSatPropio.reset(); // ~OperacionesSatProductivo: cerrarSesiones()
    m_packageStoragePropio.reset();
    m_satGatewayPropio.reset();
    m_notificador.reset();

    m_credencialesService.reset();
    m_configuracionService.reset();
    m_solicitudesService.reset();
    m_perfilesService.reset();

    // 2. Cerrar la conexion de trabajo en su hilo propietario; cerrar() espera
    //    a que terminen las tareas encoladas (incluida esta) y une el hilo.
    SqlitePersistencia* persistencia = m_persistencia.get();
    m_dispatcher->despachar<void>(std::function<void()>(
        [persistencia] { persistencia->cerrarConexionDelHiloActual(); }));
    m_dispatcher->cerrar();
    m_dispatcher.reset();
    m_planificadorExportacion.reset(); // T026.2: ya no hay tareas de planear()

    // 3. Repositorios y proveedor.
    m_sanitizer.reset();
    m_persistencia.reset();
}

QString claveEstable(ResultadoDescarteDatosPrevios::Estado e)
{
    using E = ResultadoDescarteDatosPrevios::Estado;
    switch (e) {
    case E::Descartados: return QStringLiteral("descartados");
    case E::SinDatosPrevios: return QStringLiteral("sin_datos_previos");
    case E::Incompleto: return QStringLiteral("incompleto");
    case E::ErrorReconciliacion: return QStringLiteral("error_reconciliacion");
    }
    return QStringLiteral("incompleto");
}

ControlRespaldoPresentacion& AppCompositionRoot::controlRespaldoPresentacion() const
{
    return *m_controlRespaldo;
}

ControlRestauracionPresentacion& AppCompositionRoot::controlRestauracionPresentacion() const
{
    return *m_controlRestauracion;
}

DatosPreviosRestauracionPresentacion& AppCompositionRoot::datosPreviosRestauracionPresentacion() const
{
    return *m_datosPreviosPresentacion;
}

ImpresionTexto AppCompositionRoot::impresionTexto()
{
    return [this](const QString& titulo, const QString& texto) {
        if (!m_os) {
            return false;
        }
        const auto r = m_os->imprimirTexto(titulo, texto);
        return r == OSIntegration::ResultadoImpresion::Enviado;
    };
}

CopiaTextoSecreto AppCompositionRoot::copiaTextoSecreto()
{
    return [this](const QString& texto) {
        return m_os && m_os->copiarTextoSecreto(texto) == OSIntegration::ResultadoCopiaSecreta::Copiado;
    };
}

QFuture<bool> AppCompositionRoot::hayDatosPrevios() const
{
    return QtFuture::makeReadyVoidFuture().then(QtFuture::Launch::Async, [datos = m_directorioDatos] {
        return datos_previos_restauracion::bloqueanReconciliacion(datos);
    });
}

QFuture<ResultadoDescarteDatosPrevios> AppCompositionRoot::descartarDatosPrevios()
{
    using R = ResultadoDescarteDatosPrevios;
    const QString datos = m_directorioDatos;
    CredencialesSatServicePersistido* credenciales = m_credencialesService.get();
    return QtFuture::makeReadyVoidFuture()
        .then(QtFuture::Launch::Async,
              [datos] {
                  const auto d = datos_previos_restauracion::descartar(datos);
                  R r;
                  r.carpetasBorradas = d.borradas;
                  r.carpetasAjenas = d.ajenas;
                  r.estado = d.fallidas > 0 ? R::Estado::Incompleto
                             : d.borradas > 0 ? R::Estado::Descartados
                                              : R::Estado::SinDatosPrevios;
                  if (r.estado == R::Estado::Incompleto) {
                      r.diagnostico = QStringLiteral("datos_previos.borrar");
                  }
                  qInfo("restauracion: datos previos descartados (%d carpetas; %d ajenas intactas)", d.borradas, d.ajenas);
                  return r;
              })
        .then(credenciales,
              [credenciales, datos](R r) -> QFuture<R> {
                  if (r.estado != R::Estado::Descartados) {
                      return QtFuture::makeReadyValueFuture(r);
                  }
                  return credenciales
                      ->reconciliarSalvo([datos] { return datos_previos_restauracion::bloqueanReconciliacion(datos); })
                      .then([r](CredencialesSatService::ResultadoReconciliacion c) mutable {
                          if (c) {
                              r.generacionesEliminadas = c.valor().generacionesEliminadas;
                          } else {
                              r.estado = R::Estado::ErrorReconciliacion;
                              r.diagnostico = QStringLiteral("credencial.reconciliar");
                          }
                          return r;
                      });
              })
        .unwrap();
}

QFuture<void> AppCompositionRoot::detenerParaRestaurar()
{
    // T025.5: nada que escriba debe seguir. El worker deja de programar y
    // encolar; el respaldo (diario, gestor y servicio) se cierra; el indice y
    // la vista previa se detienen; el ejecutor rechaza operaciones nuevas,
    // pide YA la cancelacion cooperativa de la actual (plazo 0) y el future se
    // completa cuando su hilo termino (sus resultados posteriores no se
    // aplican: ADR 0018).
    if (m_worker) {
        m_worker->detener();
    }
    if (m_programadorDiario) {
        m_programadorDiario.reset();
    }
    if (m_gestorRespaldo) {
        m_gestorRespaldo->cerrar();
    }
    if (m_respaldoService) {
        m_respaldoService->cerrar();
    }
    if (m_indexadorCfdi) {
        m_indexadorCfdi->detener();
    }
    if (m_previsualizacionCfdi) {
        m_previsualizacionCfdi->detener();
    }
    // T026.2 (D19, C5): la exportacion se cancela y su `.partial` se descarta
    // antes de que la restauracion escriba nada; se espera junto al ejecutor,
    // como maximo el plazo (agotado: el siguiente arranque borra el parcial).
    QList<QFuture<void>> detenciones;
    if (m_exportacionFacturas) {
        detenciones.append(esperarConPlazo(m_exportacionFacturas->cerrar(), m_plazoCierreExportacion,
                                           "restauracion.exportacion_plazo_agotado"));
    }
    detenciones.append(m_ejecutor->detener(std::chrono::milliseconds(0)));
    return QtFuture::whenAll(detenciones.begin(), detenciones.end()).then([](const QList<QFuture<void>>&) {});
}

QUuid AppCompositionRoot::idInstalacion() const
{
    if (m_idInstalacion.isNull()) {
        m_idInstalacion = obtenerOCrearIdInstalacion(m_rutaPreferencias);
    }
    return m_idInstalacion;
}

std::shared_ptr<respaldo::DestinoRespaldo> AppCompositionRoot::crearDestinoRespaldo(const QString& carpetaAbsoluta) const
{
    DestinoRespaldoPosix::Opciones o;
    o.carpetaParciales = QDir(QFileInfo(m_rutaPreferencias).absolutePath()).filePath(QStringLiteral("respaldo/parciales"));
    return std::make_shared<DestinoRespaldoPosix>(carpetaAbsoluta, std::move(o));
}

exportacion::FabricaEscritorZip AppCompositionRoot::fabricaEscritorZip() const
{
    return EscritorZipStreaming::fabrica();
}

respaldo::FuenteRespaldo& AppCompositionRoot::fuenteRespaldo() const
{
    return *m_fuenteRespaldo;
}

bool AppCompositionRoot::cargar()
{
    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->setInitialProperties(m_viewModels->initialProperties());
    m_engine->loadFromModule("SatCfdiDownloader", "Main");
    return !m_engine->rootObjects().isEmpty();
}

void AppCompositionRoot::iniciarMonitoreo()
{
    if (m_monitoreoIniciado) {
        return;
    }
    m_monitoreoIniciado = true;
    // T026.1 (D12): `.partial` de exportacion huerfanos de un proceso anterior,
    // en un hilo del pool y antes de que la UI pueda iniciar una exportacion.
    m_limpiezaExportacionInicial = QtFuture::makeReadyVoidFuture().then(
        QtFuture::Launch::Async, [destino = m_destinoExportacion] {
            const auto r = destino->limpiarParcialesPropios();
            return r ? r.valor().borrados : -1;
        });
    m_worker->iniciar(); // recuperacion (D9) y primer ciclo
    m_avisoVencimiento->iniciar(); // T014.3: al arrancar y cada 24 h
    m_indexadorCfdi->iniciar();    // T015.2 (a)+(d): pasada inicial del indice
    // T025.4: configuracion, KEK huerfanas, .partial propios (fuera del hilo
    // grafico) y despues la primera evaluacion del diario.
    respaldo::ProgramadorRespaldoDiario* diario = m_programadorDiario.get();
    m_arranqueRespaldo = m_gestorRespaldo->iniciar();
    m_arranqueRespaldo.then(m_programadorDiario.get(), [diario](int) { diario->evaluar(); });
}

QFuture<respaldo::ResultadoRespaldo> AppCompositionRoot::respaldarAhora()
{
    return m_gestorRespaldo->respaldarAhora();
}

respaldo::EstadoRespaldoGestor AppCompositionRoot::estadoRespaldo() const
{
    return m_gestorRespaldo->estado();
}

ConsultaPrimerUso& AppCompositionRoot::consultaPrimerUso() const
{
    return *m_consultaPrimerUso;
}

AppLifecycleController& AppCompositionRoot::iniciarCicloDeVida(OSIntegration& os,
                                                              SingleInstanceCoordinator* instancia,
                                                              std::function<void()> salida)
{
    Q_ASSERT(!m_controlador);
    QWindow* ventana = nullptr;
    if (m_engine && !m_engine->rootObjects().isEmpty()) {
        ventana = qobject_cast<QWindow*>(m_engine->rootObjects().constFirst());
    }
    m_controlador = std::make_unique<AppLifecycleController>(
        os, *m_configuracionService, *m_viewModels->app(), ventana);
    if (salida) {
        m_controlador->setSalida(std::move(salida));
    }
    // T009: notificaciones (credencial aqui; las de solicitud las decide el
    // servicio de aplicacion) y aviso de notificaciones deshabilitadas.
    m_os = &os;
    // T025.4: actividad de sistema durante cada respaldo y diario al despertar.
    m_gestorRespaldo->fijarOS(&os);
    QObject::connect(&os, &OSIntegration::sistemaDesperto, m_programadorDiario.get(),
                     &respaldo::ProgramadorRespaldoDiario::evaluar);
    m_notificador->setOS(&os);
    m_accesoFinder->setOS(&os);
    AppViewModel* app = m_viewModels->app();
    const auto reflejarPermiso = [app](OSIntegration::NotificationStatus s) {
        app->setNotificacionesDeshabilitadas(s == OSIntegration::NotificationStatus::Denied
                                             || s == OSIntegration::NotificationStatus::Unavailable);
    };
    reflejarPermiso(os.notificationStatus());
    QObject::connect(&os, &OSIntegration::notificationStatusChanged, app, reflejarPermiso);

    // T007: pausa/reanudacion confirmadas y salida (D1) pasan por el worker.
    m_controlador->setExtension(m_extensionWorker.get());

    // Estado del worker -> menu bar (D2, D11): estado inicial y cada cambio.
    QObject::connect(m_worker.get(), &WorkerLocal::instantaneaCambiada, &os,
                     [&os](const InstantaneaWorker& i) { os.reflejarEstadoMonitoreo(estadoMonitoreoDe(i)); });

    // T009.1 D1: "Abrir carpeta de paquetes" del menu bar. Si no se puede
    // abrir (carpeta inexistente, error o Finder fallido) el aviso D7 se
    // muestra en la lista de la ventana, que se trae al frente; nunca se crea
    // la carpeta.
    AppLifecycleController* controlador = m_controlador.get();
    QObject::connect(&os, &OSIntegration::abrirCarpetaPaquetesSolicitada, controlador, [this, app, controlador] {
        app->setMensajeFinder(QString());
        m_accesoFinder->abrirCarpetaPaquetes().then(controlador, [app, controlador](const ResultadoAccionFinder& r) {
            if (r.estado != ResultadoAccionFinder::Estado::Mostrado) {
                app->setMensajeFinder(r.mensaje);
                app->mostrarDiagnostico();
                controlador->mostrarVentana();
            }
        });
    });

    // T014.4 D2: estado agregado -> icono, solo cuando cambia (la primera
    // publicacion fija el estado inicial). En la salida deja de publicar.
    MonitorEstadoAgregado* monitor = m_estadoAgregado.get();
    QObject::connect(monitor, &MonitorEstadoAgregado::estadoCambiado, &os,
                     [&os](EstadoAgregado estado) { os.reflejarEstadoIcono(estadoIconoDe(estado)); });
    QObject::connect(controlador, &AppLifecycleController::salidaIniciada, monitor,
                     &MonitorEstadoAgregado::detener);
    // T015.2: la salida detiene el indexador (el paquete en curso no se escribe).
    QObject::connect(controlador, &AppLifecycleController::salidaIniciada, m_indexadorCfdi.get(),
                     &IndexadorCfdi::detener);
    // T015.4: la salida cancela la previsualizacion en curso.
    QObject::connect(controlador, &AppLifecycleController::salidaIniciada, m_previsualizacionCfdi.get(),
                     &PrevisualizacionCfdiService::detener);
    // T026.2 (D19): la salida cancela la exportacion de inmediato; la
    // extension (ExtensionWorker) espera su cerrar() antes del ultimo cierre.
    QObject::connect(controlador, &AppLifecycleController::salidaIniciada, m_exportacionFacturas.get(),
                     &ExportacionFacturasService::cancelar);
    monitor->alCambiarWorker(m_worker->instantanea());
    monitor->iniciar();

    m_controlador->iniciar();
    os.reflejarEstadoMonitoreo(estadoMonitoreoDe(m_worker->instantanea()));
    iniciarMonitoreo();
    if (instancia) {
        // Segunda apertura -> mostrar/enfocar; entrega lo encolado antes.
        QObject::connect(instancia, &SingleInstanceCoordinator::activacionSolicitada,
                         m_controlador.get(), &AppLifecycleController::mostrarVentana);
        instancia->habilitarEntrega();
    }
    return *m_controlador;
}

CredencialesSatService& AppCompositionRoot::credenciales() const
{
    return *m_credencialesService;
}

ConfiguracionAppService& AppCompositionRoot::configuracion() const
{
    return *m_configuracionService;
}

SolicitudesService& AppCompositionRoot::solicitudes() const
{
    return *m_solicitudesService;
}

PerfilesSatService& AppCompositionRoot::perfiles() const
{
    return *m_perfilesService;
}

} // namespace satcfdi
