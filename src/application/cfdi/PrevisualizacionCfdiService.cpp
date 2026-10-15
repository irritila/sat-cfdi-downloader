#include "application/cfdi/PrevisualizacionCfdiService.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "ports/cfdi/AlmacenPdfCfdi.h"
#include "ports/cfdi/ArchivosZipCfdi.h"
#include "ports/cfdi/CfdiArchiveReader.h"
#include "ports/cfdi/CfdiPdfRenderer.h"

#include <QCryptographicHash>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QPromise>

#include <optional>
#include <utility>

namespace satcfdi {

namespace {

// D11: solo tipo de error, evento de cache, conteos e Id local enmascarado.
Q_LOGGING_CATEGORY(lcPrevisualizacion, "satcfdi.cfdi.previsualizacion")

QString idEnmascarado(const QString& paqueteId)
{
    return paqueteId.left(8) + QStringLiteral("...");
}

const char* claveError(ErrorPrevisualizacion::Tipo tipo)
{
    switch (tipo) {
    case ErrorPrevisualizacion::Tipo::OrigenNoDisponible: return "origen-no-disponible";
    case ErrorPrevisualizacion::Tipo::VersionNoSoportada: return "version-no-soportada";
    case ErrorPrevisualizacion::Tipo::Ilegible: return "ilegible";
    case ErrorPrevisualizacion::Tipo::Cancelada: return "cancelada";
    case ErrorPrevisualizacion::Tipo::Fallo: break;
    }
    return "fallo";
}

QString soloSeguro(const QString& texto)
{
    QString r;
    for (const QChar c : texto) {
        if ((c >= u'0' && c <= u'9') || (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || c == u'-'
            || c == u'_') {
            r.append(c);
        }
    }
    return r;
}

} // namespace

// UUID (normalizado) y estado de parseo de un CFDI del indice.
struct CfdiIdentidad {
    QString uuid;
    EstadoParseo estado = EstadoParseo::Ilegible;
};

// Lectura del indice en el dispatcher: paquetes y el CFDI en (paquete, posicion).
struct PrevisualizacionCfdiService::LecturaIndice {
    Resultado<QList<PaqueteIndexable>, ErrorPersistencia> paquetes =
        Resultado<QList<PaqueteIndexable>, ErrorPersistencia>::exito({});
    bool errorCfdi = false;
    std::optional<CfdiIdentidad> cfdi;
};

struct PrevisualizacionCfdiService::Peticion {
    SolicitudId solicitud;
    QString paqueteId;
    int posicion = 0;
    quint64 generacion = 0;
    QPromise<ResultadoPdf> promesa;
    bool terminada = false;
    // Identidad del CFDI elegido, tomada del indice en el primer intento (antes
    // de revalidar o reindexar). Solo se usa en el hilo del servicio.
    std::optional<CfdiIdentidad> esperado;
};

// Resultado de la parte en el hilo del servicio.
struct PrevisualizacionCfdiService::Paso {
    std::optional<ResultadoPdf> resultado;
    bool reintentar = false;   // el indice no esta vigente: reindexar y reintentar
    bool avisarIndice = false; // pedir revision del indice sin esperar (ZIP faltante o metadatos)
    const char* evento = "";   // para el log: "cache-acierto", "generado", "temporal"
    int paginas = 0;
};

PrevisualizacionCfdiService::PrevisualizacionCfdiService(PuertosPrevisualizacionCfdi puertos, QObject* parent)
    : QObject(parent)
    , m_puertos(std::move(puertos))
    , m_contextoHilo(new QObject)
    , m_generacion(std::make_shared<std::atomic<quint64>>(0))
{
    m_hilo.setObjectName(QStringLiteral("satcfdi-pdf-cfdi"));
    m_contextoHilo->moveToThread(&m_hilo);
    connect(&m_hilo, &QThread::finished, m_contextoHilo, &QObject::deleteLater);
    m_hilo.start();
}

PrevisualizacionCfdiService::~PrevisualizacionCfdiService()
{
    detener();
    QThread* hilo = &m_hilo;
    QMetaObject::invokeMethod(m_contextoHilo, [hilo]() { hilo->quit(); }, Qt::QueuedConnection);
    m_hilo.wait();
}

void PrevisualizacionCfdiService::detener()
{
    if (m_detenido) {
        return;
    }
    m_detenido = true;
    m_generacion->fetch_add(1); // el trabajo en curso ve la cancelacion entre etapas
}

QString PrevisualizacionCfdiService::directorioCache() const
{
    return m_puertos.almacen.directorioCache();
}

QString PrevisualizacionCfdiService::directorioTemporal() const
{
    return m_puertos.almacen.directorioTemporal();
}

QString PrevisualizacionCfdiService::nombreSugerido(const QString& serie, const QString& folio, const QString& uuid)
{
    QString nombre = QStringLiteral("CFDI");
    const QString serieFolio = soloSeguro(serie + folio).left(40);
    if (!serieFolio.isEmpty()) {
        nombre += QLatin1Char('-') + serieFolio;
    }
    const QString inicioUuid = soloSeguro(uuid).left(8);
    if (!inicioUuid.isEmpty()) {
        nombre += QLatin1Char('-') + inicioUuid;
    }
    return nombre + QStringLiteral(".pdf");
}

QFuture<PrevisualizacionCfdiService::ResultadoPdf>
PrevisualizacionCfdiService::generar(const SolicitudId& solicitud, const QString& paqueteId, int posicion)
{
    auto peticion = std::make_shared<Peticion>();
    peticion->promesa.start();
    QFuture<ResultadoPdf> futuro = peticion->promesa.future();
    if (m_detenido) {
        terminar(*peticion, ResultadoPdf::fallo({ErrorPrevisualizacion::Tipo::Cancelada}));
        return futuro;
    }
    if (solicitud.esNulo() || paqueteId.isEmpty() || posicion < 0) {
        terminar(*peticion, ResultadoPdf::fallo({ErrorPrevisualizacion::Tipo::OrigenNoDisponible}));
        return futuro;
    }
    peticion->solicitud = solicitud;
    peticion->paqueteId = paqueteId;
    peticion->posicion = posicion;
    // La ultima peticion gana: las anteriores ven otra generacion y terminan
    // con Cancelada en su siguiente etapa.
    peticion->generacion = m_generacion->fetch_add(1) + 1;
    intentar(std::move(peticion), true);
    return futuro;
}

void PrevisualizacionCfdiService::intentar(std::shared_ptr<Peticion> peticion, bool permitirReindexar)
{
    CfdiCatalogRepository* catalogo = &m_puertos.catalogo;
    const SolicitudId solicitud = peticion->solicitud;
    const QString paqueteId = peticion->paqueteId;
    const int posicion = peticion->posicion;
    m_puertos.dispatcher
        .despachar<LecturaIndice>([catalogo, solicitud, paqueteId, posicion] {
            LecturaIndice l;
            l.paquetes = catalogo->listarPaquetesIndexables(solicitud);
            const auto cfdis = catalogo->listarCfdi(solicitud, FiltroListaCfdi{});
            if (!cfdis.esExito()) {
                l.errorCfdi = true;
                return l;
            }
            for (const CfdiListado& c : cfdis.valor()) {
                if (c.paqueteId == paqueteId && c.posicion == posicion) {
                    l.cfdi = CfdiIdentidad{c.resumen.uuid, c.resumen.estado};
                    break;
                }
            }
            return l;
        })
        .then(m_contextoHilo, [this, peticion](const LecturaIndice& indice) { return preparar(indice, *peticion); })
        .then(this,
              [this, peticion, permitirReindexar](const Paso& paso) {
                  if (peticion->terminada) {
                      return;
                  }
                  if (m_generacion->load() != peticion->generacion) {
                      terminar(*peticion, ResultadoPdf::fallo({ErrorPrevisualizacion::Tipo::Cancelada}));
                      return;
                  }
                  if (paso.avisarIndice && m_puertos.reindexar) {
                      m_puertos.reindexar(peticion->solicitud);
                  }
                  if (paso.reintentar) {
                      if (!permitirReindexar || !m_puertos.reindexar) {
                          terminar(*peticion, ResultadoPdf::fallo({ErrorPrevisualizacion::Tipo::OrigenNoDisponible}));
                          return;
                      }
                      qCInfo(lcPrevisualizacion).noquote()
                          << "paquete" << idEnmascarado(peticion->paqueteId) << "cambio; se reindexa antes del PDF";
                      m_puertos.reindexar(peticion->solicitud)
                          .then(this, [this, peticion] { intentar(peticion, false); })
                          .onCanceled(this, [this, peticion] {
                              terminar(*peticion, ResultadoPdf::fallo({ErrorPrevisualizacion::Tipo::Cancelada}));
                          });
                      return;
                  }
                  if (paso.resultado && paso.resultado->esExito()) {
                      qCInfo(lcPrevisualizacion).noquote()
                          << "pdf listo; paquete" << idEnmascarado(peticion->paqueteId) << "evento:" << paso.evento
                          << "paginas:" << paso.paginas;
                  }
                  terminar(*peticion, paso.resultado.value_or(ResultadoPdf::fallo({ErrorPrevisualizacion::Tipo::Fallo})));
              })
        .onCanceled(this, [this, peticion] {
            terminar(*peticion, ResultadoPdf::fallo({ErrorPrevisualizacion::Tipo::Cancelada}));
        });
}

PrevisualizacionCfdiService::Paso
PrevisualizacionCfdiService::preparar(const LecturaIndice& indice, Peticion& peticion)
{
    const Resultado<QList<PaqueteIndexable>, ErrorPersistencia>& paquetes = indice.paquetes;
    const auto vigente = [this, &peticion] { return m_generacion->load() == peticion.generacion; };
    const auto error = [](ErrorPrevisualizacion::Tipo tipo) {
        Paso p;
        p.resultado = ResultadoPdf::fallo({tipo});
        return p;
    };
    const auto reintentar = [] {
        Paso p;
        p.reintentar = true;
        return p;
    };
    if (!vigente()) {
        return error(ErrorPrevisualizacion::Tipo::Cancelada);
    }
    if (!paquetes.esExito() || indice.errorCfdi) {
        return error(ErrorPrevisualizacion::Tipo::Fallo);
    }
    const PaqueteIndexable* paquete = nullptr;
    for (const PaqueteIndexable& p : paquetes.valor()) {
        if (p.paqueteId == peticion.paqueteId) {
            paquete = &p;
            break;
        }
    }
    if (!paquete) {
        return error(ErrorPrevisualizacion::Tipo::OrigenNoDisponible); // eliminado o ya no Descargado
    }
    // Identidad del CFDI elegido: se fija en el primer intento. Tras reindexar,
    // si en esa posicion ya hay otro CFDI (o ninguno), no se genera nada.
    const auto mismoCfdi = [&peticion](const CfdiIdentidad& c) {
        const CfdiIdentidad& e = *peticion.esperado;
        return e.uuid.isEmpty() ? (c.uuid.isEmpty() && c.estado == e.estado) : c.uuid == e.uuid;
    };
    if (!peticion.esperado) {
        if (!indice.cfdi && paquete->estado == EstadoIndicePaquete::Indexado) {
            return error(ErrorPrevisualizacion::Tipo::OrigenNoDisponible); // ese CFDI no esta en el indice
        }
        peticion.esperado = indice.cfdi;
    } else if (paquete->estado == EstadoIndicePaquete::Indexado && (!indice.cfdi || !mismoCfdi(*indice.cfdi))) {
        return error(ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
    }

    // 1. Origen.
    const ArchivoZipLocal archivo = m_puertos.archivos.consultar(paquete->rutaLocal);
    if (archivo.estado != ArchivoZipLocal::Estado::Existe) {
        Paso p = error(ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
        p.avisarIndice = paquete->estado != EstadoIndicePaquete::OrigenNoDisponible;
        return p;
    }

    // 2. Vigencia del indice y huella del ZIP.
    const bool versionVigente = paquete->versionParser == m_puertos.versionParser && paquete->huella.has_value();
    const bool mismosMetadatos =
        paquete->huella && paquete->huella->mismosMetadatos(archivo.tamano, archivo.modificadoMs);
    if (versionVigente && mismosMetadatos && paquete->estado == EstadoIndicePaquete::Ilegible) {
        return error(ErrorPrevisualizacion::Tipo::Ilegible); // ZIP corrupto ya revisado
    }
    if (!versionVigente || paquete->estado != EstadoIndicePaquete::Indexado) {
        return reintentar();
    }
    bool avisarIndice = false;
    if (!mismosMetadatos) {
        const auto sha = m_puertos.archivos.sha256(archivo.rutaAbsoluta, vigente);
        if (!vigente()) {
            return error(ErrorPrevisualizacion::Tipo::Cancelada);
        }
        if (!sha || *sha != paquete->huella->sha256) {
            return reintentar();
        }
        avisarIndice = true; // mismo contenido: el indice actualiza sus metadatos
    }

    // 3. Solo la entrada de esa posicion.
    auto lectura = m_puertos.lector.leerEntrada(archivo.rutaAbsoluta, peticion.posicion);
    if (!lectura.esExito()) {
        return reintentar(); // el indice registrara OrigenNoDisponible o Ilegible
    }
    const ArchivoZipLocal despues = m_puertos.archivos.consultar(paquete->rutaLocal);
    if (despues.estado != ArchivoZipLocal::Estado::Existe || despues.tamano != archivo.tamano
        || despues.modificadoMs != archivo.modificadoMs) {
        return reintentar(); // cambio durante la lectura
    }
    if (!lectura.valor()) {
        return error(ErrorPrevisualizacion::Tipo::Ilegible);
    }
    const QByteArray& xml = *lectura.valor();
    const ResultadoParseoCfdi cfdi = m_puertos.parser(QByteArrayView(xml));
    // NoEsCfdi no se indexa: se compara como Ilegible (nunca coincide con un UUID).
    if (peticion.esperado && !mismoCfdi(CfdiIdentidad{cfdi.resumen.uuid, cfdi.estado()})) {
        return error(ErrorPrevisualizacion::Tipo::OrigenNoDisponible); // no es el CFDI seleccionado
    }
    switch (cfdi.estado()) {
    case EstadoParseo::Ilegible:
    case EstadoParseo::NoEsCfdi:
        return error(ErrorPrevisualizacion::Tipo::Ilegible);
    case EstadoParseo::VersionNoSoportada:
        return error(ErrorPrevisualizacion::Tipo::VersionNoSoportada);
    case EstadoParseo::Completo:
    case EstadoParseo::Parcial:
        break;
    }
    const QString nombre = nombreSugerido(cfdi.resumen.serie, cfdi.resumen.folio, cfdi.resumen.uuid);

    // 4. Cache.
    const ClavePdfCfdi clave{cfdi.resumen.uuid,
                             QString::fromLatin1(QCryptographicHash::hash(xml, QCryptographicHash::Sha256).toHex()),
                             m_puertos.renderer.version()};
    Paso paso;
    paso.avisarIndice = avisarIndice;
    if (const auto ruta = m_puertos.almacen.buscar(clave)) {
        paso.resultado = ResultadoPdf::exito(PdfCfdi{*ruta, nombre});
        paso.evento = "cache-acierto";
        return paso;
    }
    if (!vigente()) {
        return error(ErrorPrevisualizacion::Tipo::Cancelada);
    }
    auto pdf = m_puertos.renderer.renderizar(cfdi);
    if (!pdf.esExito()) {
        qCWarning(lcPrevisualizacion) << "no se pudo generar el PDF";
        return error(ErrorPrevisualizacion::Tipo::Fallo);
    }
    auto guardado = m_puertos.almacen.guardar(clave, pdf.valor().contenido);
    if (!guardado.esExito()) {
        return error(ErrorPrevisualizacion::Tipo::Fallo);
    }
    paso.resultado = ResultadoPdf::exito(PdfCfdi{guardado.valor().rutaAbsoluta, nombre});
    paso.evento = guardado.valor().enTemporal ? "generado-en-temporal" : "generado";
    paso.paginas = pdf.valor().paginas;
    return paso;
}

void PrevisualizacionCfdiService::terminar(Peticion& peticion, ResultadoPdf resultado)
{
    if (peticion.terminada) {
        return;
    }
    peticion.terminada = true;
    if (!resultado.esExito() && resultado.error().tipo != ErrorPrevisualizacion::Tipo::Cancelada) {
        qCInfo(lcPrevisualizacion).noquote() << "previsualizacion sin PDF; paquete" << idEnmascarado(peticion.paqueteId)
                                             << "tipo:" << claveError(resultado.error().tipo);
    }
    peticion.promesa.addResult(std::move(resultado));
    peticion.promesa.finish();
}

QFuture<PrevisualizacionCfdiService::ResultadoGuardar>
PrevisualizacionCfdiService::guardarComo(const QString& rutaPdf, const QString& destinoAbsoluto)
{
    auto promesa = std::make_shared<QPromise<ResultadoGuardar>>();
    promesa->start();
    QFuture<ResultadoGuardar> futuro = promesa->future();
    const auto completar = [promesa](ResultadoGuardar r) {
        promesa->addResult(std::move(r));
        promesa->finish();
    };
    if (m_detenido) {
        completar(ResultadoGuardar::fallo({ErrorPrevisualizacion::Tipo::Cancelada}));
        return futuro;
    }
    AlmacenPdfCfdi* almacen = &m_puertos.almacen;
    const bool encolado = QMetaObject::invokeMethod(
        m_contextoHilo,
        [almacen, rutaPdf, destinoAbsoluto, completar] {
            const auto r = almacen->copiar(rutaPdf, destinoAbsoluto);
            if (r.esExito()) {
                completar(ResultadoGuardar::exito(true));
            } else {
                qCInfo(lcPrevisualizacion) << "guardar PDF sin exito";
                completar(ResultadoGuardar::fallo({ErrorPrevisualizacion::Tipo::Fallo}));
            }
        },
        Qt::QueuedConnection);
    if (!encolado) {
        completar(ResultadoGuardar::fallo({ErrorPrevisualizacion::Tipo::Fallo}));
    }
    return futuro;
}

} // namespace satcfdi
