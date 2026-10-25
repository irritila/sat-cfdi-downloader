#pragma once

// T026.2: servicio de exportacion de facturas a un ZIP elegido por el usuario
// (T026 D9, D11, D13, D16, D17, D19-D21; ADR 0023).
//
// Ownership e hilos
// - QObject con afinidad al hilo grafico; lo crea y posee el composition
//   root. Posee su PROPIO QThread ("satcfdi-exportacion"): espera de la
//   limpieza inicial, reserva del `.partial`, huella de los ZIP fuente,
//   relectura por posicion, parseo, render del PDF, escritura en streaming y
//   publicacion corren ahi. Nunca usa el hilo grafico ni la cola del
//   OperacionExecutor (no llama al SAT ni cambia estados persistidos).
// - Todos los metodos publicos se llaman desde el hilo grafico. Las senales se
//   emiten en el hilo grafico y los futures de planear() e iniciar() se
//   completan en el hilo grafico.
// - Un trabajo a la vez (D11): con uno en curso (incluido el estado
//   EsperandoNombre), iniciar() devuelve de inmediato un error EnCurso y no
//   toca el trabajo vigente.
//
// Flujo (C4): planear(alcance) -> el dialogo muestra plan.conteoPrevisto()
// (D16) -> iniciar(plan, carpeta, nombre, conPdf) con ESE plan (no se vuelve
// a planear). Con 0 facturas: SinFacturas sin reservar ni crear nada.
//
// Por trabajo, en el hilo del servicio:
//  1. Espera la limpieza inicial de `.partial` huerfanos (limpiezaInicial);
//     -1, cancelada o sin valor -> Error LimpiezaInicial sin reservar.
//  2. Reserva el `.partial` (DestinoExportacion::reservar). Una falla,
//     incluida DestinoExistente, es terminal (Error Destino): no hay parcial;
//     el llamador pide otro nombre y vuelve a llamar iniciar().
//  3. Por cada factura del plan, en su orden:
//     - visibilidad: al cambiar de paquete respecto de la factura anterior se
//       consulta paqueteExportable (dispatcher, C3); un paquete no visible
//       se recuerda como tal. Ademas, notificarSolicitudEliminada() marca la
//       solicitud de inmediato; la marca se consulta antes de leer y, bajo
//       el mismo mutex que usa notificarSolicitudEliminada(), se decide y se
//       escribe el XML (C6): una senal anterior lo omite; una posterior ya no
//       lo encuentra pendiente (D19). Eliminada -> "ya no disponible".
//     - huella del ZIP fuente (C1) antes y despues de leer: tamano y mtime
//       iguales a la huella planeada; si difieren se calcula el SHA-256 del
//       ZIP (una vez por combinacion de metadatos observada) y debe ser el
//       planeado.
//     - relectura de SOLO la entrada (paqueteId, posicion), reparseo y
//       comparacion del UUID canonico con FacturaPlaneada::uuidCanonico.
//     - XML con FacturaPlaneada::nombre.xml(); con PDF, render en memoria con
//       un renderer propio de la exportacion (D13) y nombre.pdf(). Una falla
//       de PDF (o VersionNoSoportada) conserva el XML y agrega "sin PDF".
//     Mapeo de omisiones (identificador: nombre.base):
//       ZIP o entrada ausente, solicitud/paquete eliminado -> YaNoDisponible
//       ZIP invalido o ilegible, SHA imposible, XML ilegible o no CFDI -> Ilegible
//       SHA del ZIP distinto, cambio durante la lectura, UUID distinto
//         -> CambioDesdeIndexacion
//       PDF NoRepresentable / Fallo / VersionNoSoportada -> SinPdf (XML incluido)
//  4. `omitidas.txt` en la raiz del ZIP (D21) con formatearOmitidas(omisiones
//     del plan + las de lectura); sin lineas no hay archivo (D17).
//     Si todas las facturas se omiten al leer, el ZIP se publica igual con
//     solo `omitidas.txt` (D16 aplica al plan: 0 planeadas no crea nada).
//  5. Cierra el escritor y publica. DestinoExistente al publicar: el
//     `.partial` se conserva, el trabajo pasa a EsperandoNombre, se emite
//     destinoExistente() y el future de iniciar() sigue pendiente hasta
//     publicarComo(nombre) o cancelar().
//
// Cancelacion: cancelar() es idempotente y seguro de llamar en cualquier
// momento. Se observa entre facturas, antes de cada render, dentro del
// escritor (entre trozos) y durante las esperas (limpieza, visibilidad).
// Antes del rename descarta el `.partial` y el resultado es Cancelada; despues
// del rename no cambia nada (el resultado sigue siendo Publicada). Un error
// del destino o del escritor descarta el `.partial` y es terminal: nunca se
// publica un ZIP truncado.
//
// Cierre (D19): cerrar() cancela, descarta el `.partial` (tambien uno en
// EsperandoNombre), termina el trabajo con Cancelada y une el hilo; su future
// se completa en el hilo grafico cuando el hilo del servicio termino. Despues
// iniciar() y planear() devuelven Detenido y los planear() pendientes se
// resuelven con Detenido. Idempotente. Restauracion (T025.5) y salida esperan
// este future (con su propio plazo, C5) antes de sustituir datos o terminar.
// Una llamada bloqueada dentro de un puerto (p. ej. leerEntrada en un volumen
// colgado) no se interrumpe: el future no se completa hasta que regrese.
//
// Destructor (A3, C5): llama cerrar() y espera el hilo a lo sumo
// `plazoDestructor` (10 s). Si se agota, no destruye el QThread en ejecucion:
// anula el enlace con el servicio, resuelve el trabajo con Cancelada y deja el
// hilo abandonado. Al regresar la llamada bloqueada, el hilo (ya cancelado)
// descarta escritor y `.partial` sin tocar el servicio, ejecuta su quit() y el
// QThread se borra (encolado) en el hilo grafico. C7: el hilo retiene los
// puertos (copia en un nucleo compartido) hasta terminar: destino, lector y
// archivos son `shared_ptr` y las funciones van por valor, asi que nada de lo
// que toca tras el abandono depende de la vida del composition root. Si el
// proceso sale antes, el `.partial` lo borra la limpieza del siguiente
// arranque.
//
// Logs (D20): categoria "satcfdi.cfdi.exportacion"; solo fases, conteos y
// codigos estables (con errno). Nunca nombres de archivo, rutas, UUID, RFC,
// IdPaquete ni texto de error con datos. El resultado no lleva datos fiscales.

#include "application/cfdi/CalculoIndicePaquete.h"
#include "application/persistence/PuertosPersistencia.h"
#include "domain/cfdi/ExportacionFacturas.h"
#include "domain/common/Resultado.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/exportacion/DestinoExportacion.h"
#include "ports/exportacion/EscritorZipExportacion.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/persistence/Exito.h"

#include <QFuture>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QThread>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace satcfdi {

class ArchivosZipCfdi;
class CfdiArchiveReader;
class CfdiCatalogRepository;
class CfdiPdfRenderer;
class PersistenceDispatcher;
class PlanificadorExportacion;

enum class FaseExportacion {
    Preparando,  // espera de limpieza, reserva y apertura del escritor
    Xml,         // relectura y escritura del XML de cada factura
    Pdf,         // render y escritura del PDF de cada factura
    Finalizando, // omitidas.txt, directorio central y publicacion
};

QString claveEstable(FaseExportacion fase);

struct ErrorExportacion {
    enum class Codigo {
        EnCurso,         // ya hay una exportacion en curso (o esperando nombre)
        Detenido,        // el servicio se cerro (salida o restauracion)
        Estado,          // publicarComo() sin una exportacion esperando nombre
        Planificacion,   // planear() fallo al leer la instantanea (ver persistencia)
        LimpiezaInicial, // la limpieza de `.partial` huerfanos fallo o no se hizo
        Persistencia,    // la consulta de visibilidad de un paquete fallo
        Destino,         // reservar o publicar fallo (ver destino)
        Zip,             // el escritor ZIP fallo (ver zip)
        Interno,
    };

    Codigo codigo = Codigo::Interno;
    std::optional<ErrorPersistencia::Tipo> persistencia;
    std::optional<exportacion::ErrorDestinoExportacion> destino;
    std::optional<exportacion::ErrorZipExportacion> zip;

    static ErrorExportacion de(Codigo c)
    {
        ErrorExportacion e;
        e.codigo = c;
        return e;
    }

    friend bool operator==(const ErrorExportacion&, const ErrorExportacion&) = default;
};

QString claveEstable(ErrorExportacion::Codigo codigo);

// Resultado terminal de una exportacion (D9). Sin datos fiscales ni rutas.
struct ResultadoExportacion {
    enum class Estado {
        Publicada,   // el ZIP existe con su nombre final
        SinFacturas, // el plan no tenia facturas (D16): no se creo nada
        Cancelada,   // cancelar() o cerrar() antes del rename; sin ZIP ni `.partial`
        Error,       // ver error; sin ZIP ni `.partial`
    };

    Estado estado = Estado::Error;
    std::optional<ErrorExportacion> error; // solo con Error

    int exportadas = 0;    // XML incluidos en el ZIP
    int pdfExportados = 0; // PDF incluidos
    int sinPdf = 0;        // XML incluidos sin PDF (lineas "sin PDF")
    int omitidas = 0;      // CFDI cuyo XML no se exporto (plan + lectura; excluye "sin PDF")
    int lineasMes = 0;     // lineas de mes de omitidas.txt (no son CFDI, D9)

    // Con Publicada: tamano del ZIP y advertencia de durabilidad (el fsync de
    // una carpeta tras el rename fallo; el ZIP si esta publicado).
    quint64 tamanoBytes = 0;
    bool advertenciaDurabilidad = false;
    int errnoDurabilidad = 0;
    // Nombre final con que se publico (el de iniciar() o el de publicarComo()).
    // Lo eligio el usuario: SOLO para mostrarlo en la ventana de exportacion;
    // nunca va a notificaciones del sistema ni a logs (D20).
    QString nombrePublicado;

    friend bool operator==(const ResultadoExportacion&, const ResultadoExportacion&) = default;
};

QString claveEstable(ResultadoExportacion::Estado estado);

using ResultadoPlanExportacion = Resultado<PlanExportacion, ErrorExportacion>;

// Planea en el PersistenceDispatcher (PlanificadorExportacion::planear). Se
// llama en el hilo grafico; el future puede completarse en cualquier hilo.
using PlanearExportacionFn =
    std::function<QFuture<Resultado<PlanExportacion, ErrorPersistencia>>(const AlcanceExportacion&)>;

// Puerto minimo de visibilidad (C3): true si el paquete sigue visible y
// Descargado y su solicitud visible (no eliminada localmente, ADR 0015). Se
// llama en el hilo grafico; el future puede completarse en cualquier hilo.
// Un future cancelado o con fallo aborta la exportacion con Persistencia.
// Funcion vacia = sin revalidacion por dispatcher (solo pruebas); la marca de
// notificarSolicitudEliminada() sigue aplicando.
using ConsultaPaqueteExportableFn =
    std::function<QFuture<Resultado<bool, ErrorPersistencia>>(const SolicitudId& solicitud, const QString& paqueteId)>;

// Future de la limpieza inicial de `.partial` huerfanos
// (AppCompositionRoot::limpiezaExportacionInicial): valor = borrados, -1 si
// fallo. Se llama en el hilo grafico al iniciar cada exportacion.
using LimpiezaInicialExportacionFn = std::function<QFuture<int>()>;

// Crea un renderer propio para una exportacion (D13); se llama en el hilo del
// servicio y el renderer se destruye al terminar esa exportacion. Nunca nulo.
using FabricaRendererPdf = std::function<std::unique_ptr<CfdiPdfRenderer>()>;

// Puertos. C7: lo que usa el hilo del servicio es compartido (destino,
// lector, archivos; nunca nulos) o va por valor (fabricas, parser, reloj), y
// el servicio lo retiene hasta que su hilo termina, aun si el destructor lo
// abandona por plazo. Las funciones que se llaman en el hilo del servicio
// (fabricas, parser, reloj, antesDeAgregarXml) no deben capturar referencias
// a objetos de vida mas corta. planear, paqueteExportable y limpiezaInicial
// solo se llaman en el hilo del servicio-objeto (grafico) mientras este vive:
// pueden capturar referencias del composition root que lo crea.
struct PuertosExportacionFacturas {
    PlanearExportacionFn planear;
    ConsultaPaqueteExportableFn paqueteExportable;
    LimpiezaInicialExportacionFn limpiezaInicial;
    std::shared_ptr<exportacion::DestinoExportacion> destino; // thread-safe (reservar desde el hilo del servicio)
    exportacion::FabricaEscritorZip fabricaEscritor; // uno por exportacion
    std::shared_ptr<CfdiArchiveReader> lector; // hilo del servicio; sin estado compartido
    std::shared_ptr<ArchivosZipCfdi> archivos; // hilo del servicio; raiz efectiva de paquetes
    ParserCfdiFn parser;         // puro y reentrante
    FabricaRendererPdf fabricaRenderer;
    RelojUtc reloj = relojSistema(); // fecha de las entradas del ZIP (por valor, C7)
    // A3 (C5): espera maxima del destructor por el hilo del servicio.
    std::chrono::milliseconds plazoDestructor = std::chrono::seconds(10);
    // Costura de prueba (vacia en produccion): se llama en el hilo del
    // servicio con el indice de la factura justo antes de decidir, bajo el
    // mutex de la marca de eliminacion, si su XML se agrega.
    std::function<void(int indice)> antesDeAgregarXml;
};

class ExportacionFacturasService final : public QObject {
    Q_OBJECT

public:
    enum class EstadoTrabajo {
        Inactivo,
        EnCurso,         // preparando, escribiendo o publicando
        EsperandoNombre, // DestinoExistente al publicar: falta publicarComo() o cancelar()
    };
    Q_ENUM(EstadoTrabajo)

    explicit ExportacionFacturasService(PuertosExportacionFacturas puertos, QObject* parent = nullptr);
    ~ExportacionFacturasService() override; // cerrar() y espera del hilo

    ExportacionFacturasService(const ExportacionFacturasService&) = delete;
    ExportacionFacturasService& operator=(const ExportacionFacturasService&) = delete;

    // Plan del alcance (T026.1). El future siempre termina, en el hilo
    // grafico: Planificacion (con el tipo de persistencia; Interno si el
    // puerto lanzo o su future trajo una excepcion), Detenido (dispatcher
    // cerrado, future cancelado o cerrar() con el plan pendiente) o el plan.
    QFuture<ResultadoPlanExportacion> planear(const AlcanceExportacion& alcance);

    // Exporta `plan` a `carpetaDestino` (absoluta) con `nombreZip` (se sanea
    // con sanearNombreZip, D15). El future se completa con el resultado
    // terminal; se emite terminada() con el mismo valor. Rechazos inmediatos
    // (sin terminada()): Error EnCurso y Error Detenido. Plan sin facturas:
    // SinFacturas inmediato (con terminada()).
    QFuture<ResultadoExportacion> iniciar(PlanExportacion plan, QString carpetaDestino, QString nombreZip,
                                          bool conPdf);

    // Solo en EsperandoNombre: publica el MISMO `.partial` con otro nombre
    // (saneado, D15). Exito = aceptado (el resultado llega por el future de
    // iniciar() y terminada(); si el nombre tambien existe se vuelve a emitir
    // destinoExistente()). Fuera de ese estado: Estado (o Detenido).
    Resultado<Exito, ErrorExportacion> publicarComo(const QString& nombreZip);

    // Idempotente; sin efecto sin trabajo o despues del rename.
    void cancelar();

    // Ver "Cierre" arriba. Idempotente (devuelve el mismo future).
    QFuture<void> cerrar();

    EstadoTrabajo estado() const noexcept { return m_estado; }
    bool enCurso() const noexcept { return m_estado != EstadoTrabajo::Inactivo; }
    bool cerrado() const noexcept { return m_cerrado; }
    const QThread* hilo() const noexcept { return m_hilo; }

    // Fabricas de puertos sobre el PersistenceDispatcher para el composition
    // root. Las funciones devueltas se llaman en el hilo propietario del
    // dispatcher (el grafico); las referencias deben vivir mas que el servicio.
    static PlanearExportacionFn planearPorDispatcher(PersistenceDispatcher& dispatcher,
                                                     const PlanificadorExportacion& planificador);
    // listarPaquetesIndexables(solicitud) contiene `paqueteId`.
    static ConsultaPaqueteExportableFn paqueteExportablePorCatalogo(PersistenceDispatcher& dispatcher,
                                                                    CfdiCatalogRepository& catalogo);

public slots:
    // D19: conectar a SolicitudesService::solicitudEliminada. Las facturas
    // pendientes de esa solicitud en el trabajo en curso se omiten como
    // "ya no disponible". Sin trabajo no hace nada.
    void notificarSolicitudEliminada(const satcfdi::SolicitudId& solicitud);

signals:
    // Por fase y factura. `actual` de 0 a `total` (= plan.conteoPrevisto()).
    void progreso(satcfdi::FaseExportacion fase, int actual, int total, int exportadas, int sinPdf, int omitidas);
    // DestinoExistente al publicar: pedir otro nombre y llamar publicarComo().
    void destinoExistente();
    void terminada(const satcfdi::ResultadoExportacion& resultado);

private:
    struct Trabajo;
    struct Nucleo;
    struct PlanPendiente;
    class Ejecucion;

    void entregar(const std::shared_ptr<Trabajo>& trabajo);
    void ejecutarEnHilo(const std::shared_ptr<Trabajo>& trabajo, std::function<void()> paso);

    std::shared_ptr<Nucleo> m_nucleo; // puertos y enlace; compartido con el hilo
    QThread* m_hilo = nullptr;        // propio; ver ~ExportacionFacturasService
    QList<std::shared_ptr<PlanPendiente>> m_planes; // planear() pendientes (hilo grafico)
    QObject* m_contextoHilo = nullptr; // vive en m_hilo
    std::shared_ptr<Trabajo> m_trabajo; // solo hilo grafico
    EstadoTrabajo m_estado = EstadoTrabajo::Inactivo;
    bool m_cerrado = false;
    std::shared_ptr<QPromise<void>> m_cierre; // con cerrar()
};

} // namespace satcfdi

Q_DECLARE_METATYPE(satcfdi::FaseExportacion)
Q_DECLARE_METATYPE(satcfdi::ResultadoExportacion)
