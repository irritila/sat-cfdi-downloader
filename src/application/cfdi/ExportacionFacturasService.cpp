#include "application/cfdi/ExportacionFacturasService.h"

#include "application/cfdi/PlanificadorExportacion.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "domain/cfdi/UuidCfdi.h"
#include "ports/cfdi/ArchivosZipCfdi.h"
#include "ports/cfdi/CfdiArchiveReader.h"
#include "ports/cfdi/CfdiPdfRenderer.h"
#include "ports/repositories/CfdiCatalogRepository.h"

#include <QDeadlineTimer>
#include <QHash>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>
#include <QWaitCondition>

#include <atomic>
#include <exception>
#include <utility>

namespace satcfdi {

namespace {

// D20: solo fases, conteos y codigos estables.
Q_LOGGING_CATEGORY(lcExportacion, "satcfdi.cfdi.exportacion")

const QString kOmitidasTxt = QStringLiteral("omitidas.txt");

const char* claveMotivo(MotivoOmisionFactura m)
{
    switch (m) {
    case MotivoOmisionFactura::NoDisponibleEnEsteEquipo: return "no_disponible";
    case MotivoOmisionFactura::Ilegible: return "ilegible";
    case MotivoOmisionFactura::CambioDesdeIndexacion: return "cambio_desde_indexacion";
    case MotivoOmisionFactura::YaNoDisponible: return "ya_no_disponible";
    case MotivoOmisionFactura::SinPdf: return "sin_pdf";
    }
    return "desconocido";
}

template <typename T>
QFuture<T> futuroListo(T valor)
{
    QPromise<T> p;
    p.start();
    p.addResult(std::move(valor));
    p.finish();
    return p.future();
}

ResultadoExportacion resultadoError(ErrorExportacion error)
{
    ResultadoExportacion r;
    r.estado = ResultadoExportacion::Estado::Error;
    r.error = std::move(error);
    return r;
}

} // namespace

QString claveEstable(FaseExportacion fase)
{
    switch (fase) {
    case FaseExportacion::Preparando: return QStringLiteral("preparando");
    case FaseExportacion::Xml: return QStringLiteral("xml");
    case FaseExportacion::Pdf: return QStringLiteral("pdf");
    case FaseExportacion::Finalizando: return QStringLiteral("finalizando");
    }
    return QStringLiteral("desconocida");
}

QString claveEstable(ErrorExportacion::Codigo codigo)
{
    using C = ErrorExportacion::Codigo;
    switch (codigo) {
    case C::EnCurso: return QStringLiteral("en_curso");
    case C::Detenido: return QStringLiteral("detenido");
    case C::Estado: return QStringLiteral("estado");
    case C::Planificacion: return QStringLiteral("planificacion");
    case C::LimpiezaInicial: return QStringLiteral("limpieza_inicial");
    case C::Persistencia: return QStringLiteral("persistencia");
    case C::Destino: return QStringLiteral("destino");
    case C::Zip: return QStringLiteral("zip");
    case C::Interno: return QStringLiteral("interno");
    }
    return QStringLiteral("interno");
}

QString claveEstable(ResultadoExportacion::Estado estado)
{
    using E = ResultadoExportacion::Estado;
    switch (estado) {
    case E::Publicada: return QStringLiteral("publicada");
    case E::SinFacturas: return QStringLiteral("sin_facturas");
    case E::Cancelada: return QStringLiteral("cancelada");
    case E::Error: return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

// --- Estado de un trabajo ----------------------------------------------------
//
// Compartido entre el hilo grafico y el del servicio:
// - `cancelado` (atomico) y, bajo `mutex`, `eliminadas` y las celdas de
//   espera; `condicion` despierta las esperas al completarse un future o al
//   cancelar.
// Solo hilo del servicio: `parcial` (en EsperandoNombre), `resultado` hasta
// que se entrega. Solo hilo grafico: `promesa` y `entregado`.
template <typename T>
struct Celda {
    std::optional<T> valor;
    bool lista = false;
};

struct ExportacionFacturasService::Trabajo {
    PlanExportacion plan;
    QString carpeta;
    QString nombre;
    bool conPdf = false;

    std::atomic<bool> cancelado{false};
    QMutex mutex;
    QWaitCondition condicion;
    QSet<SolicitudId> eliminadas;
    std::shared_ptr<Celda<int>> limpieza = std::make_shared<Celda<int>>();

    std::unique_ptr<exportacion::ArchivoParcialExportacion> parcial; // EsperandoNombre
    ResultadoExportacion resultado;

    QPromise<ResultadoExportacion> promesa;
    bool entregado = false;

    bool continuar() const noexcept { return !cancelado.load(); }

    void cancelar()
    {
        cancelado.store(true);
        QMutexLocker l(&mutex);
        condicion.wakeAll();
    }

    bool eliminada(const SolicitudId& s)
    {
        QMutexLocker l(&mutex);
        return !s.esNulo() && eliminadas.contains(s);
    }

    // Completa `celda` cuando termine `futuro` (en el hilo que lo complete).
    // Un future cancelado, invalido o con excepcion deja la celda sin valor.
    template <typename T>
    static void enlazar(const std::shared_ptr<Trabajo>& t, QFuture<T> futuro, const std::shared_ptr<Celda<T>>& celda)
    {
        auto completar = [t, celda](std::optional<T> v) {
            QMutexLocker l(&t->mutex);
            if (celda->lista) {
                return;
            }
            celda->valor = std::move(v);
            celda->lista = true;
            t->condicion.wakeAll();
        };
        if (futuro.isCanceled()) {
            completar(std::nullopt);
            return;
        }
        futuro
            .then(QtFuture::Launch::Sync,
                  [completar](QFuture<T> f) {
                      std::optional<T> v;
                      try {
                          if (!f.isCanceled() && f.resultCount() > 0) {
                              v = f.result();
                          }
                      } catch (...) {
                          v.reset();
                      }
                      completar(std::move(v));
                  })
            .onCanceled([completar] { completar(std::nullopt); });
    }

    // Espera la celda o la cancelacion. nullopt si se cancelo o sin valor.
    template <typename T>
    std::optional<T> esperar(Celda<T>& celda)
    {
        QMutexLocker l(&mutex);
        while (!celda.lista && !cancelado.load()) {
            condicion.wait(&mutex);
        }
        return celda.lista ? celda.valor : std::nullopt;
    }
};

// --- Nucleo compartido con el hilo del servicio --------------------------------
//
// Lo que el hilo del servicio necesita y que debe sobrevivir al servicio si el
// destructor agota su plazo (A3): copia de los puertos (C7: destino, lector y
// archivos compartidos; cada paso encolado en el hilo retiene el nucleo, asi
// que viven hasta que el hilo termina) y el enlace con el QObject del
// servicio. `servicio` se anula (bajo `mutex`) al destruirlo; desde
// entonces nada del hilo llega al hilo grafico.
struct ExportacionFacturasService::Nucleo {
    explicit Nucleo(PuertosExportacionFacturas p)
        : puertos(std::move(p))
    {
        Q_ASSERT(puertos.destino && puertos.lector && puertos.archivos);
    }

    PuertosExportacionFacturas puertos;
    QMutex mutex;
    ExportacionFacturasService* servicio = nullptr;

    // Encola `f(servicio)` en el hilo grafico. false si el servicio ya no existe.
    template <typename F>
    bool enviar(F f)
    {
        QMutexLocker l(&mutex);
        if (!servicio) {
            return false;
        }
        ExportacionFacturasService* s = servicio;
        return QMetaObject::invokeMethod(s, [s, f = std::move(f)]() mutable { f(s); }, Qt::QueuedConnection);
    }
};

// --- Ejecucion en el hilo del servicio ---------------------------------------

class ExportacionFacturasService::Ejecucion {
public:
    Ejecucion(std::shared_ptr<Nucleo> nucleo, std::shared_ptr<Trabajo> trabajo)
        : m_n(std::move(nucleo))
        , m_p(m_n->puertos)
        , m_t(std::move(trabajo))
    {
    }

    // Trabajo completo hasta el resultado o EsperandoNombre.
    void ejecutar()
    {
        Trabajo& t = *m_t;
        const int total = t.plan.conteoPrevisto();
        m_omisiones = t.plan.omisiones;
        for (const OmisionExportacion& o : std::as_const(m_omisiones)) {
            if (const auto* f = std::get_if<OmisionFacturaExportacion>(&o); f && f->motivo != MotivoOmisionFactura::SinPdf) {
                ++t.resultado.omitidas;
            }
        }
        qCInfo(lcExportacion).noquote() << "exportacion iniciada; facturas:" << total
                                        << "pdf:" << (t.conPdf ? "si" : "no")
                                        << "omisiones del plan:" << t.plan.omisiones.size();
        progreso(FaseExportacion::Preparando, 0);

        // 1. Limpieza inicial de `.partial` huerfanos (T026.1).
        const std::optional<int> limpieza = t.esperar(*t.limpieza);
        if (!t.continuar()) {
            return terminar(ResultadoExportacion::Estado::Cancelada);
        }
        if (!limpieza || *limpieza < 0) {
            return fallar(ErrorExportacion::de(ErrorExportacion::Codigo::LimpiezaInicial));
        }

        // 2. Reserva y escritor.
        auto reserva = m_p.destino->reservar(t.carpeta, t.nombre);
        if (!reserva) {
            ErrorExportacion e = ErrorExportacion::de(ErrorExportacion::Codigo::Destino);
            e.destino = reserva.error();
            return fallar(std::move(e));
        }
        m_parcial = std::move(reserva).valor();
        m_escritor = m_p.fabricaEscritor();
        if (auto r = m_escritor->abrir(*m_parcial, m_p.reloj(), [t = m_t] { return t->continuar(); }); !r) {
            return fallarZip(r.error());
        }
        if (t.conPdf) {
            m_renderer = m_p.fabricaRenderer();
        }

        // 3. Facturas.
        for (int i = 0; i < total; ++i) {
            if (!t.continuar()) {
                return terminar(ResultadoExportacion::Estado::Cancelada);
            }
            if (!exportarFactura(t.plan.facturas.at(i), i)) {
                return; // ya termino (cancelada o error)
            }
        }

        // 4. omitidas.txt (D17, D21) y cierre.
        progreso(FaseExportacion::Finalizando, total);
        for (const OmisionExportacion& o : std::as_const(m_omisiones)) {
            if (std::holds_alternative<OmisionMesExportacion>(o)) {
                ++t.resultado.lineasMes;
            }
        }
        if (const std::optional<QByteArray> omitidas = formatearOmitidas(m_omisiones)) {
            if (auto r = m_escritor->agregar(kOmitidasTxt, *omitidas); !r) {
                return fallarZip(r.error());
            }
        }
        if (auto r = m_escritor->cerrar(); !r) {
            return fallarZip(r.error());
        }
        m_escritor.reset();
        m_renderer.reset();
        if (!t.continuar()) {
            return terminar(ResultadoExportacion::Estado::Cancelada);
        }
        publicar(std::nullopt);
    }

    // 5. Publicacion (tambien publicarComo en EsperandoNombre).
    void publicar(const std::optional<QString>& otroNombre)
    {
        Trabajo& t = *m_t;
        if (!m_parcial) {
            m_parcial = std::move(t.parcial);
        }
        if (!m_parcial) {
            return fallar(ErrorExportacion::de(ErrorExportacion::Codigo::Interno));
        }
        if (!t.continuar()) {
            return terminar(ResultadoExportacion::Estado::Cancelada);
        }
        auto r = otroNombre ? m_parcial->publicarComo(*otroNombre) : m_parcial->publicar();
        if (!r) {
            if (r.error().codigo == exportacion::ErrorDestinoExportacion::Codigo::DestinoExistente) {
                qCInfo(lcExportacion) << "destino existente al publicar; se espera otro nombre";
                t.parcial = std::move(m_parcial); // se conserva intacto
                m_n->enviar([t = m_t](ExportacionFacturasService* s) {
                    if (s->m_trabajo != t) {
                        return;
                    }
                    if (t->cancelado.load()) {
                        // cancelar() llego antes de este aviso: se descarta
                        // aqui (cerrar() ya lo hace en su tarea final).
                        if (!s->m_cerrado) {
                            s->ejecutarEnHilo(t, [n = s->m_nucleo, t] { Ejecucion(n, t).descartarEnEspera(); });
                        }
                        return;
                    }
                    s->m_estado = EstadoTrabajo::EsperandoNombre;
                    emit s->destinoExistente();
                });
                return;
            }
            ErrorExportacion e = ErrorExportacion::de(ErrorExportacion::Codigo::Destino);
            e.destino = r.error();
            return fallar(std::move(e));
        }
        m_parcial.reset(); // ya publicado: descartar() no hace nada
        t.resultado.tamanoBytes = r.valor().tamanoBytes;
        t.resultado.advertenciaDurabilidad = r.valor().advertenciaDurabilidad;
        t.resultado.errnoDurabilidad = r.valor().errnoDurabilidad;
        t.resultado.nombrePublicado = otroNombre.value_or(t.nombre);
        terminar(ResultadoExportacion::Estado::Publicada);
    }

    // Cierre en EsperandoNombre: descarta el `.partial` conservado.
    void descartarEnEspera()
    {
        if (m_t->parcial) {
            m_parcial = std::move(m_t->parcial);
            terminar(ResultadoExportacion::Estado::Cancelada);
        }
    }

private:
    enum class Paso { Incluir, Omitir, Cancelar, Abortar };

    struct Lectura {
        Paso paso = Paso::Omitir;
        MotivoOmisionFactura motivo = MotivoOmisionFactura::Ilegible;
        std::optional<ErrorExportacion> error; // con Abortar
        QByteArray xml;
        ResultadoParseoCfdi cfdi;
    };

    static Lectura omitir(MotivoOmisionFactura m)
    {
        Lectura l;
        l.paso = Paso::Omitir;
        l.motivo = m;
        return l;
    }
    static Lectura cancelar()
    {
        Lectura l;
        l.paso = Paso::Cancelar;
        return l;
    }

    // false si el trabajo termino (cancelado o error).
    bool exportarFactura(const FacturaPlaneada& f, int indice)
    {
        Trabajo& t = *m_t;
        progreso(FaseExportacion::Xml, indice);
        Lectura l = leer(f);
        switch (l.paso) {
        case Paso::Cancelar:
            terminar(ResultadoExportacion::Estado::Cancelada);
            return false;
        case Paso::Abortar:
            fallar(l.error.value_or(ErrorExportacion::de(ErrorExportacion::Codigo::Interno)));
            return false;
        case Paso::Omitir:
            omitirFactura(f, l.motivo);
            return true;
        case Paso::Incluir:
            break;
        }
        if (m_p.antesDeAgregarXml) {
            m_p.antesDeAgregarXml(indice);
        }
        // C6 (D19): la marca de eliminacion y la escritura del XML se deciden
        // bajo el mismo mutex que notificarSolicitudEliminada(). Una senal que
        // llega despues ya no encuentra este CFDI pendiente.
        {
            QMutexLocker bloqueo(&t.mutex);
            if (!f.solicitud.esNulo() && t.eliminadas.contains(f.solicitud)) {
                bloqueo.unlock();
                omitirFactura(f, MotivoOmisionFactura::YaNoDisponible);
                return true;
            }
            if (auto r = m_escritor->agregar(f.nombre.xml(), l.xml); !r) {
                bloqueo.unlock();
                fallarZip(r.error());
                return false;
            }
        }
        ++t.resultado.exportadas;
        if (!t.conPdf) {
            return true;
        }
        if (!t.continuar()) {
            terminar(ResultadoExportacion::Estado::Cancelada);
            return false;
        }
        progreso(FaseExportacion::Pdf, indice);
        const EstadoParseo estado = l.cfdi.estado();
        if (estado != EstadoParseo::Completo && estado != EstadoParseo::Parcial) {
            omitirFactura(f, MotivoOmisionFactura::SinPdf); // VersionNoSoportada: solo XML (D13)
            return true;
        }
        auto pdf = m_renderer ? m_renderer->renderizar(l.cfdi)
                              : Resultado<PdfRenderizado, ErrorRenderPdf>::fallo(ErrorRenderPdf{});
        if (!pdf) {
            omitirFactura(f, MotivoOmisionFactura::SinPdf);
            return true;
        }
        if (auto r = m_escritor->agregar(f.nombre.pdf(), pdf.valor().contenido); !r) {
            fallarZip(r.error());
            return false;
        }
        ++t.resultado.pdfExportados;
        return true;
    }

    // Revalidacion y relectura de una factura (D11, C1, C3).
    Lectura leer(const FacturaPlaneada& f)
    {
        Trabajo& t = *m_t;
        if (t.eliminada(f.solicitud)) {
            return omitir(MotivoOmisionFactura::YaNoDisponible);
        }
        // Visibilidad por el dispatcher al cambiar de paquete.
        const QString& paquete = f.origen.paqueteId;
        if (paquete != m_ultimoPaquete) {
            m_ultimoPaquete = paquete;
            if (!m_noVisibles.contains(paquete)) {
                const std::optional<Resultado<bool, ErrorPersistencia>> visible = consultarVisibilidad(f);
                if (!t.continuar()) {
                    return cancelar();
                }
                if (!visible || !visible->esExito()) {
                    Lectura l;
                    l.paso = Paso::Abortar;
                    l.error = ErrorExportacion::de(ErrorExportacion::Codigo::Persistencia);
                    if (visible) {
                        l.error->persistencia = visible->error().tipo;
                    }
                    return l;
                }
                if (!visible->valor()) {
                    m_noVisibles.insert(paquete);
                }
            }
        }
        if (m_noVisibles.contains(paquete)) {
            return omitir(MotivoOmisionFactura::YaNoDisponible);
        }

        // Huella del ZIP antes de leer.
        const ArchivoZipLocal antes = m_p.archivos->consultar(f.rutaPaquete);
        if (antes.estado == ArchivoZipLocal::Estado::NoExiste) {
            return omitir(MotivoOmisionFactura::YaNoDisponible);
        }
        if (antes.estado != ArchivoZipLocal::Estado::Existe) {
            return omitir(MotivoOmisionFactura::Ilegible);
        }
        if (const std::optional<Lectura> fallo = validarHuella(f, antes)) {
            return *fallo;
        }

        // Solo la entrada de esa posicion.
        auto lectura = m_p.lector->leerEntrada(antes.rutaAbsoluta, f.origen.posicion);
        if (!t.continuar()) {
            return cancelar();
        }
        if (!lectura) {
            return omitir(lectura.error().tipo == ErrorPaqueteZip::Tipo::NoExiste
                              ? MotivoOmisionFactura::YaNoDisponible
                              : MotivoOmisionFactura::Ilegible);
        }

        // Huella despues de leer: si cambio, los bytes deben seguir siendo los
        // del ZIP indexado.
        const ArchivoZipLocal despues = m_p.archivos->consultar(f.rutaPaquete);
        if (despues.estado == ArchivoZipLocal::Estado::NoExiste) {
            return omitir(MotivoOmisionFactura::YaNoDisponible);
        }
        if (despues.estado != ArchivoZipLocal::Estado::Existe) {
            return omitir(MotivoOmisionFactura::Ilegible);
        }
        if (despues.tamano != antes.tamano || despues.modificadoMs != antes.modificadoMs) {
            if (const std::optional<Lectura> fallo = validarHuella(f, despues)) {
                return *fallo;
            }
        }
        if (!lectura.valor()) {
            return omitir(MotivoOmisionFactura::YaNoDisponible); // entrada ausente u omitida por el lector
        }

        // Reparseo y UUID.
        Lectura l;
        l.xml = std::move(*std::move(lectura).valor());
        l.cfdi = m_p.parser(QByteArrayView(l.xml));
        if (l.cfdi.estado() == EstadoParseo::Ilegible || l.cfdi.estado() == EstadoParseo::NoEsCfdi) {
            return omitir(MotivoOmisionFactura::Ilegible);
        }
        if (cfdi::normalizarUuid(l.cfdi.resumen.uuid) != f.uuidCanonico) {
            return omitir(MotivoOmisionFactura::CambioDesdeIndexacion);
        }
        l.paso = Paso::Incluir;
        return l;
    }

    // nullopt si la huella coincide con la planeada (C1). Metadatos iguales:
    // sin SHA. Distintos: SHA-256 una vez por combinacion observada.
    std::optional<Lectura> validarHuella(const FacturaPlaneada& f, const ArchivoZipLocal& zip)
    {
        if (f.huellaPaquete.mismosMetadatos(zip.tamano, zip.modificadoMs)) {
            return std::nullopt;
        }
        const QString clave = f.rutaPaquete + QLatin1Char('\n') + QString::number(zip.tamano) + QLatin1Char('\n')
                              + QString::number(zip.modificadoMs);
        auto it = m_huellas.constFind(clave);
        if (it == m_huellas.cend()) {
            const std::optional<QString> sha =
                m_p.archivos->sha256(zip.rutaAbsoluta, [t = m_t] { return t->continuar(); });
            if (!m_t->continuar()) {
                return cancelar();
            }
            std::optional<MotivoOmisionFactura> motivo;
            if (!sha) {
                motivo = MotivoOmisionFactura::Ilegible;
            } else if (*sha != f.huellaPaquete.sha256) {
                motivo = MotivoOmisionFactura::CambioDesdeIndexacion;
            }
            it = m_huellas.insert(clave, motivo);
        }
        if (!*it) {
            return std::nullopt;
        }
        return omitir(**it);
    }

    std::optional<Resultado<bool, ErrorPersistencia>> consultarVisibilidad(const FacturaPlaneada& f)
    {
        auto celda = std::make_shared<Celda<Resultado<bool, ErrorPersistencia>>>();
        if (!m_p.paqueteExportable) {
            return Resultado<bool, ErrorPersistencia>::exito(true);
        }
        ConsultaPaqueteExportableFn consulta = m_p.paqueteExportable;
        const SolicitudId solicitud = f.solicitud;
        const QString paquete = f.origen.paqueteId;
        // El dispatcher se usa desde su hilo propietario (el grafico).
        const bool encolado = m_n->enviar([t = m_t, celda, consulta, solicitud, paquete](ExportacionFacturasService*) {
            Trabajo::enlazar(t, consulta(solicitud, paquete), celda);
        });
        if (!encolado) {
            return std::nullopt;
        }
        return m_t->esperar(*celda);
    }

    void omitirFactura(const FacturaPlaneada& f, MotivoOmisionFactura motivo)
    {
        m_omisiones.append(OmisionFacturaExportacion{f.direccion, f.mes, f.nombre.base, motivo});
        ++m_porMotivo[motivo];
        if (motivo == MotivoOmisionFactura::SinPdf) {
            ++m_t->resultado.sinPdf;
        } else {
            ++m_t->resultado.omitidas;
        }
    }

    void progreso(FaseExportacion fase, int actual)
    {
        const unsigned bit = 1u << unsigned(fase);
        if ((m_fasesRegistradas & bit) == 0) { // una vez por fase (XML y PDF se alternan por factura)
            m_fasesRegistradas |= bit;
            qCInfo(lcExportacion).noquote() << "fase:" << claveEstable(fase);
        }
        const ResultadoExportacion& r = m_t->resultado;
        m_n->enviar([fase, actual, total = m_t->plan.conteoPrevisto(), exportadas = r.exportadas, sinPdf = r.sinPdf,
                     omitidas = r.omitidas](ExportacionFacturasService* s) {
            emit s->progreso(fase, actual, total, exportadas, sinPdf, omitidas);
        });
    }

    void fallarZip(const exportacion::ErrorZipExportacion& error)
    {
        if (error.codigo == exportacion::ErrorZipExportacion::Codigo::Cancelado) {
            return terminar(ResultadoExportacion::Estado::Cancelada);
        }
        ErrorExportacion e = ErrorExportacion::de(ErrorExportacion::Codigo::Zip);
        e.zip = error;
        fallar(std::move(e));
    }

    void fallar(ErrorExportacion error)
    {
        m_t->resultado.error = std::move(error);
        terminar(ResultadoExportacion::Estado::Error);
    }

    // Resultado terminal: descarta lo propio y entrega en el hilo grafico.
    void terminar(ResultadoExportacion::Estado estado)
    {
        if (m_escritor) {
            m_escritor->abortar();
            m_escritor.reset();
        }
        if (m_parcial) {
            m_parcial->descartar();
            m_parcial.reset();
        }
        m_renderer.reset();
        ResultadoExportacion& r = m_t->resultado;
        r.estado = estado;
        if (estado != ResultadoExportacion::Estado::Error) {
            r.error.reset();
        }
        if (estado != ResultadoExportacion::Estado::Publicada) {
            r.tamanoBytes = 0;
            r.advertenciaDurabilidad = false;
            r.errnoDurabilidad = 0;
            r.nombrePublicado.clear();
        }
        registrar(r);
        m_n->enviar([t = m_t](ExportacionFacturasService* s) { s->entregar(t); });
    }

    void registrar(const ResultadoExportacion& r) const
    {
        QStringList partes{QStringLiteral("exportacion terminada; estado: ") + claveEstable(r.estado),
                           QStringLiteral("exportadas: %1").arg(r.exportadas),
                           QStringLiteral("pdf: %1").arg(r.pdfExportados), QStringLiteral("sinPdf: %1").arg(r.sinPdf),
                           QStringLiteral("omitidas: %1").arg(r.omitidas),
                           QStringLiteral("lineasMes: %1").arg(r.lineasMes)};
        for (auto it = m_porMotivo.cbegin(); it != m_porMotivo.cend(); ++it) {
            partes << QStringLiteral("%1: %2").arg(QLatin1StringView(claveMotivo(it.key()))).arg(it.value());
        }
        if (r.advertenciaDurabilidad) {
            partes << QStringLiteral("advertencia: durabilidad errno: %1").arg(r.errnoDurabilidad);
        }
        if (r.error) {
            partes << QStringLiteral("codigo: ") + claveEstable(r.error->codigo);
            if (r.error->destino) {
                partes << QStringLiteral("destino: ") + exportacion::claveEstable(r.error->destino->codigo)
                       << QStringLiteral("errno: %1").arg(r.error->destino->errnoSistema);
            }
            if (r.error->zip) {
                partes << QStringLiteral("zip: ") + exportacion::claveEstable(r.error->zip->codigo)
                       << QStringLiteral("errno: %1").arg(r.error->zip->errnoSistema);
                if (r.error->zip->destino) {
                    partes << QStringLiteral("destino: ") + exportacion::claveEstable(*r.error->zip->destino);
                }
            }
            if (r.error->persistencia) {
                partes << QStringLiteral("persistencia: %1").arg(int(*r.error->persistencia));
            }
        }
        qCInfo(lcExportacion).noquote() << partes.join(QStringLiteral("; "));
    }

    std::shared_ptr<Nucleo> m_n;
    const PuertosExportacionFacturas& m_p; // vive en m_n
    std::shared_ptr<Trabajo> m_t;
    std::unique_ptr<exportacion::ArchivoParcialExportacion> m_parcial;
    std::unique_ptr<exportacion::EscritorZipExportacion> m_escritor;
    std::unique_ptr<CfdiPdfRenderer> m_renderer;
    QList<OmisionExportacion> m_omisiones;
    QHash<QString, std::optional<MotivoOmisionFactura>> m_huellas; // ruta+metadatos -> fallo
    QSet<QString> m_noVisibles;
    QString m_ultimoPaquete;
    QMap<MotivoOmisionFactura, int> m_porMotivo;
    unsigned m_fasesRegistradas = 0;
};

// --- Servicio ----------------------------------------------------------------

// planear() pendiente: se completa una sola vez (resultado, excepcion,
// cancelacion o cerrar()).
struct ExportacionFacturasService::PlanPendiente {
    QPromise<ResultadoPlanExportacion> promesa;
    bool terminado = false;

    void completar(ResultadoPlanExportacion r)
    {
        if (terminado) {
            return;
        }
        terminado = true;
        promesa.addResult(std::move(r));
        promesa.finish();
    }
};

ExportacionFacturasService::ExportacionFacturasService(PuertosExportacionFacturas puertos, QObject* parent)
    : QObject(parent)
    , m_nucleo(std::make_shared<Nucleo>(std::move(puertos)))
    , m_hilo(new QThread)
    , m_contextoHilo(new QObject)
{
    qRegisterMetaType<satcfdi::FaseExportacion>("satcfdi::FaseExportacion");
    qRegisterMetaType<satcfdi::ResultadoExportacion>("satcfdi::ResultadoExportacion");
    m_nucleo->servicio = this;
    m_hilo->setObjectName(QStringLiteral("satcfdi-exportacion"));
    m_contextoHilo->moveToThread(m_hilo);
    connect(m_hilo, &QThread::finished, m_contextoHilo, &QObject::deleteLater);
    m_hilo->start();
}

ExportacionFacturasService::~ExportacionFacturasService()
{
    cerrar();
    const bool unido = m_hilo->wait(QDeadlineTimer(m_nucleo->puertos.plazoDestructor));
    {
        // Desde aqui el hilo ya no encola nada hacia este objeto.
        QMutexLocker l(&m_nucleo->mutex);
        m_nucleo->servicio = nullptr;
    }
    if (m_trabajo && !m_trabajo->entregado) {
        m_trabajo->entregado = true;
        // Sin unir el hilo, `resultado` puede estar cambiando: Cancelada.
        ResultadoExportacion r = unido ? m_trabajo->resultado : ResultadoExportacion{};
        if (!unido) {
            r.estado = ResultadoExportacion::Estado::Cancelada;
        }
        m_trabajo->promesa.addResult(r);
        m_trabajo->promesa.finish();
    }
    if (m_cierre && !m_cierre->future().isFinished()) {
        m_cierre->finish();
    }
    if (unido) {
        delete m_hilo;
        return;
    }
    // A3 (C5): plazo agotado (p. ej. leerEntrada bloqueado en un volumen
    // colgado). No se destruye un QThread en ejecucion: el hilo queda
    // abandonado y termina solo cuando la llamada bloqueada regrese. Ya esta
    // cancelado, asi que su trabajo descarta escritor y `.partial` sin tocar
    // el servicio (el enlace se anulo) y su cola ejecuta el quit() de
    // cerrar(); el QThread se borra en el hilo grafico (encolado) al
    // terminar (si el proceso sale antes, el `.partial` lo borra la limpieza
    // del siguiente arranque, T026.1). Ver el contrato en el header.
    qCWarning(lcExportacion) << "hilo de exportacion abandonado; plazo de cierre agotado";
    // finished() se emite antes de que el hilo salga del todo: el borrado
    // corre encolado en el hilo grafico y espera con wait() (breve). Si el
    // hilo termino justo entre el plazo y este connect, el QThread se pierde
    // (memoria; nunca se destruye en ejecucion).
    QThread* hilo = m_hilo;
    connect(
        hilo, &QThread::finished, hilo,
        [hilo] {
            hilo->wait();
            delete hilo;
        },
        Qt::QueuedConnection);
}

PlanearExportacionFn ExportacionFacturasService::planearPorDispatcher(PersistenceDispatcher& dispatcher,
                                                                    const PlanificadorExportacion& planificador)
{
    return [&dispatcher, &planificador](const AlcanceExportacion& alcance) {
        return dispatcher.despachar<Resultado<PlanExportacion, ErrorPersistencia>>(
            [&planificador, alcance] { return planificador.planear(alcance); });
    };
}

ConsultaPaqueteExportableFn ExportacionFacturasService::paqueteExportablePorCatalogo(PersistenceDispatcher& dispatcher,
                                                                                   CfdiCatalogRepository& catalogo)
{
    return [&dispatcher, &catalogo](const SolicitudId& solicitud, const QString& paqueteId) {
        return dispatcher.despachar<Resultado<bool, ErrorPersistencia>>([&catalogo, solicitud, paqueteId] {
            using R = Resultado<bool, ErrorPersistencia>;
            if (solicitud.esNulo()) {
                return R::exito(false);
            }
            const auto paquetes = catalogo.listarPaquetesIndexables(solicitud);
            if (!paquetes) {
                return R::fallo(paquetes.error());
            }
            for (const PaqueteIndexable& p : paquetes.valor()) {
                if (p.paqueteId == paqueteId) {
                    return R::exito(true);
                }
            }
            return R::exito(false);
        });
    };
}

QFuture<ResultadoPlanExportacion> ExportacionFacturasService::planear(const AlcanceExportacion& alcance)
{
    using RP = Resultado<PlanExportacion, ErrorPersistencia>;
    if (m_cerrado) {
        return futuroListo(ResultadoPlanExportacion::fallo(ErrorExportacion::de(ErrorExportacion::Codigo::Detenido)));
    }
    if (!m_nucleo->puertos.planear) {
        return futuroListo(ResultadoPlanExportacion::fallo(ErrorExportacion::de(ErrorExportacion::Codigo::Interno)));
    }
    auto pendiente = std::make_shared<PlanPendiente>();
    pendiente->promesa.start();
    QFuture<ResultadoPlanExportacion> futuro = pendiente->promesa.future();
    const auto fallaPlan = [] {
        ErrorExportacion e = ErrorExportacion::de(ErrorExportacion::Codigo::Planificacion);
        e.persistencia = ErrorPersistencia::Tipo::Interno;
        qCInfo(lcExportacion) << "plan sin exito; codigo: planificacion";
        return ResultadoPlanExportacion::fallo(std::move(e));
    };
    QFuture<RP> origen;
    try {
        origen = m_nucleo->puertos.planear(alcance);
    } catch (...) {
        pendiente->completar(fallaPlan());
        return futuro;
    }
    m_planes.append(pendiente);
    // Se completa en el hilo grafico; excepcion -> Planificacion; cancelacion
    // (dispatcher cerrado) -> Detenido; cerrar() resuelve los pendientes.
    origen
        .then(this,
              [this, pendiente, fallaPlan](QFuture<RP> f) {
                  m_planes.removeOne(pendiente);
                  std::optional<RP> r;
                  try {
                      f.waitForFinished(); // ya termino: solo relanza una excepcion guardada
                      if (!f.isCanceled() && f.resultCount() > 0) {
                          r.emplace(f.result());
                      }
                  } catch (...) {
                      pendiente->completar(fallaPlan());
                      return;
                  }
                  if (!r) {
                      pendiente->completar(
                          ResultadoPlanExportacion::fallo(ErrorExportacion::de(ErrorExportacion::Codigo::Detenido)));
                      return;
                  }
                  if (*r) {
                      pendiente->completar(ResultadoPlanExportacion::exito(std::move(*r).valor()));
                      return;
                  }
                  ErrorExportacion e = ErrorExportacion::de(ErrorExportacion::Codigo::Planificacion);
                  e.persistencia = r->error().tipo;
                  qCInfo(lcExportacion) << "plan sin exito; codigo: planificacion";
                  pendiente->completar(ResultadoPlanExportacion::fallo(std::move(e)));
              })
        .onCanceled(this, [this, pendiente] {
            m_planes.removeOne(pendiente);
            pendiente->completar(
                ResultadoPlanExportacion::fallo(ErrorExportacion::de(ErrorExportacion::Codigo::Detenido)));
        });
    return futuro;
}

QFuture<ResultadoExportacion> ExportacionFacturasService::iniciar(PlanExportacion plan, QString carpetaDestino,
                                                                  QString nombreZip, bool conPdf)
{
    if (m_cerrado) {
        return futuroListo(resultadoError(ErrorExportacion::de(ErrorExportacion::Codigo::Detenido)));
    }
    if (m_trabajo) {
        qCInfo(lcExportacion) << "exportacion rechazada; codigo: en_curso";
        return futuroListo(resultadoError(ErrorExportacion::de(ErrorExportacion::Codigo::EnCurso)));
    }
    if (plan.conteoPrevisto() == 0) {
        // D16: no se reserva ni se crea ningun archivo.
        ResultadoExportacion r;
        r.estado = ResultadoExportacion::Estado::SinFacturas;
        for (const OmisionExportacion& o : std::as_const(plan.omisiones)) {
            if (std::holds_alternative<OmisionMesExportacion>(o)) {
                ++r.lineasMes;
            } else if (std::get<OmisionFacturaExportacion>(o).motivo != MotivoOmisionFactura::SinPdf) {
                ++r.omitidas;
            }
        }
        qCInfo(lcExportacion).noquote() << "exportacion terminada; estado:" << claveEstable(r.estado);
        QMetaObject::invokeMethod(this, [this, r] { emit terminada(r); }, Qt::QueuedConnection);
        return futuroListo(r);
    }

    auto t = std::make_shared<Trabajo>();
    t->plan = std::move(plan);
    t->carpeta = std::move(carpetaDestino);
    t->nombre = sanearNombreZip(nombreZip);
    t->conPdf = conPdf;
    t->promesa.start();
    QFuture<ResultadoExportacion> futuro = t->promesa.future();
    const LimpiezaInicialExportacionFn& limpieza = m_nucleo->puertos.limpiezaInicial;
    Trabajo::enlazar(t, limpieza ? limpieza() : QFuture<int>(), t->limpieza);
    m_trabajo = t;
    m_estado = EstadoTrabajo::EnCurso;
    ejecutarEnHilo(t, [n = m_nucleo, t] { Ejecucion(n, t).ejecutar(); });
    return futuro;
}

Resultado<Exito, ErrorExportacion> ExportacionFacturasService::publicarComo(const QString& nombreZip)
{
    using R = Resultado<Exito, ErrorExportacion>;
    if (m_cerrado) {
        return R::fallo(ErrorExportacion::de(ErrorExportacion::Codigo::Detenido));
    }
    if (!m_trabajo || m_estado != EstadoTrabajo::EsperandoNombre) {
        return R::fallo(ErrorExportacion::de(ErrorExportacion::Codigo::Estado));
    }
    m_estado = EstadoTrabajo::EnCurso;
    const QString nombre = sanearNombreZip(nombreZip);
    std::shared_ptr<Trabajo> t = m_trabajo;
    ejecutarEnHilo(t, [n = m_nucleo, t, nombre] { Ejecucion(n, t).publicar(nombre); });
    return R::exito(Exito{});
}

void ExportacionFacturasService::cancelar()
{
    if (!m_trabajo || m_trabajo->cancelado.load()) {
        return;
    }
    qCInfo(lcExportacion) << "cancelacion solicitada";
    m_trabajo->cancelar();
    if (m_estado == EstadoTrabajo::EsperandoNombre) {
        // El hilo esta libre: descarta el `.partial` conservado.
        m_estado = EstadoTrabajo::EnCurso;
        std::shared_ptr<Trabajo> t = m_trabajo;
        ejecutarEnHilo(t, [n = m_nucleo, t] { Ejecucion(n, t).descartarEnEspera(); });
    }
}

QFuture<void> ExportacionFacturasService::cerrar()
{
    if (m_cierre) {
        return m_cierre->future();
    }
    m_cerrado = true;
    m_cierre = std::make_shared<QPromise<void>>();
    m_cierre->start();
    std::shared_ptr<QPromise<void>> cierre = m_cierre;
    connect(
        m_hilo, &QThread::finished, this,
        [this, cierre] {
            m_hilo->wait();
            if (!cierre->future().isFinished()) {
                cierre->finish();
            }
        },
        Qt::QueuedConnection);
    // A1: ningun plan pendiente queda sin resultado.
    const QList<std::shared_ptr<PlanPendiente>> planes = std::exchange(m_planes, {});
    for (const std::shared_ptr<PlanPendiente>& p : planes) {
        p->completar(ResultadoPlanExportacion::fallo(ErrorExportacion::de(ErrorExportacion::Codigo::Detenido)));
    }
    std::shared_ptr<Trabajo> t = m_trabajo;
    if (t) {
        t->cancelar();
    }
    // Corre despues del trabajo en curso (misma cola del hilo): descarta un
    // `.partial` en EsperandoNombre y termina el hilo. No captura `this`: puede
    // correr despues del destructor si este agoto su plazo.
    const bool encolado = QMetaObject::invokeMethod(
        m_contextoHilo,
        [n = m_nucleo, t] {
            if (t && t->parcial) {
                Ejecucion(n, t).descartarEnEspera();
            }
            QThread::currentThread()->quit();
        },
        Qt::QueuedConnection);
    if (!encolado) {
        m_hilo->quit();
    }
    return m_cierre->future();
}

void ExportacionFacturasService::notificarSolicitudEliminada(const SolicitudId& solicitud)
{
    if (!m_trabajo || solicitud.esNulo()) {
        return;
    }
    QMutexLocker l(&m_trabajo->mutex);
    m_trabajo->eliminadas.insert(solicitud);
}

void ExportacionFacturasService::ejecutarEnHilo(const std::shared_ptr<Trabajo>& trabajo, std::function<void()> paso)
{
    const bool encolado = QMetaObject::invokeMethod(m_contextoHilo, std::move(paso), Qt::QueuedConnection);
    if (!encolado) {
        trabajo->resultado = resultadoError(ErrorExportacion::de(ErrorExportacion::Codigo::Interno));
        QMetaObject::invokeMethod(this, [this, trabajo] { entregar(trabajo); }, Qt::QueuedConnection);
    }
}

void ExportacionFacturasService::entregar(const std::shared_ptr<Trabajo>& trabajo)
{
    if (trabajo->entregado) {
        return;
    }
    trabajo->entregado = true;
    const ResultadoExportacion resultado = trabajo->resultado;
    trabajo->promesa.addResult(resultado);
    trabajo->promesa.finish();
    if (m_trabajo == trabajo) {
        m_trabajo.reset();
        m_estado = EstadoTrabajo::Inactivo;
    }
    emit terminada(resultado);
}

} // namespace satcfdi
