#pragma once

#include "application/cfdi/CalculoIndicePaquete.h"
#include "application/persistence/PuertosPersistencia.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/repositories/CfdiCatalogRepository.h"

#include <QFuture>
#include <QList>
#include <QObject>
#include <QPromise>
#include <QThread>

#include <atomic>
#include <memory>
#include <optional>

namespace satcfdi {

class ArchivosZipCfdi;
class CfdiArchiveReader;
class CfdiCatalogRepository;
class PersistenceDispatcher;
class UnitOfWork;

// Puertos del indexador: referencias NO propietarias que el composition root
// crea antes y destruye despues del indexador.
struct PuertosIndexadorCfdi {
    PersistenceDispatcher& dispatcher;    // lecturas y escrituras del catalogo
    CfdiCatalogRepository& catalogo;      // solo dentro de tareas del dispatcher
    UnitOfWork& unidadDeTrabajo;          // idem
    CfdiArchiveReader& lector;            // solo en el hilo del indexador
    ArchivosZipCfdi& archivos;            // solo en el hilo del indexador
    ParserCfdiFn parser;                  // puro; hilo del indexador
    int versionParser = 1;
    RelojUtc reloj;
};

// Indexador de CFDI (T015.2, T015 D2/D10/D11).
//
// Hilos y ownership:
// - QObject con afinidad al hilo grafico; lo crea y posee el composition root.
//   Posee su PROPIO QThread ("satcfdi-indice-cfdi") donde corre, uno a la vez,
//   el calculo bloqueante de cada paquete (stat, SHA-256, lectura del ZIP y
//   parseo). Nunca usa la cola del OperacionExecutor (D10): una indexacion
//   larga no retrasa ninguna operacion SAT.
// - Lecturas y escrituras del catalogo van por PersistenceDispatcher (hilo de
//   persistencia, su propia conexion). Cada paquete se escribe en UNA
//   transaccion BEGIN IMMEDIATE: borrar lo derivado + insertar sus CFDI +
//   estado/huella.
// - Trabajos en cola serial (FIFO, coalescidos por alcance). Cada trabajo lee
//   los paquetes Descargado visibles de su alcance y aplica
//   calcularIndicePaquete() a cada uno.
//
// Disparos:
// (a)+(d) iniciar(): una pasada sobre todos los paquetes Descargado (indexa
//         los que no tienen indice vigente y revalida los demas por tamano y
//         mtime). Al reiniciar no se repite trabajo: lo vigente no se relee.
// (b)     sincronizarSolicitud(id): lo conecta el root a
//         OperacionExecutor::solicitudActualizada (p. ej. tras descargar).
// (c)     revisarSolicitud(id): lo usara T015.3 al abrir el explorador.
//
// Salida: detener() (lo conecta el root a salidaIniciada) vacia la cola y
// cancela el paquete en curso entre entradas del ZIP: ese paquete NO se
// escribe y queda como estaba (sin indice = Pendiente), y se reindexa en la
// pasada del siguiente arranque. El destructor detiene y une el hilo.
//
// Senales (hilo grafico, despues del commit):
// - indiceCambiado(id): el indice de la solicitud cambio; se emite a lo sumo
//   una vez por solicitud y trabajo, al terminar sus paquetes.
// - ocupadoCambiado(bool): hay o no trabajo en curso o en cola.
class IndexadorCfdi final : public QObject {
    Q_OBJECT

public:
    explicit IndexadorCfdi(PuertosIndexadorCfdi puertos, QObject* parent = nullptr);
    ~IndexadorCfdi() override;

    IndexadorCfdi(const IndexadorCfdi&) = delete;
    IndexadorCfdi& operator=(const IndexadorCfdi&) = delete;

    // (a)+(d). Idempotente; no hace nada despues de detener().
    void iniciar();

    // (b)/(c). El future se completa cuando termina el trabajo que cubre la
    // solicitud (o al detener: cancelado). Tras detener(): future cancelado.
    QFuture<void> sincronizarSolicitud(const SolicitudId& solicitud);
    QFuture<void> revisarSolicitud(const SolicitudId& solicitud) { return sincronizarSolicitud(solicitud); }

    // Salida (no bloquea). Idempotente.
    void detener();

    bool ocupado() const noexcept { return m_actual.has_value() || !m_cola.isEmpty(); }
    bool detenido() const noexcept { return m_detenido; }
    // Hay trabajo en curso o en cola que alcanza a la solicitud.
    bool cubre(const SolicitudId& solicitud) const;
    int versionParser() const noexcept { return m_puertos.versionParser; }

    // Para pruebas y Q_ASSERT.
    const QThread* hilo() const noexcept { return &m_hilo; }

signals:
    void indiceCambiado(const satcfdi::SolicitudId& solicitud);
    void ocupadoCambiado(bool ocupado);

private:
    struct Trabajo {
        std::optional<SolicitudId> alcance; // nullopt: todos los paquetes
        QList<std::shared_ptr<QPromise<void>>> promesas;
        QList<PaqueteIndexable> paquetes;
        qsizetype siguiente = 0;
        bool leido = false;
    };

    QFuture<void> encolar(std::optional<SolicitudId> alcance);
    void avanzar();
    void procesarSiguientePaquete();
    void aplicar(const PaqueteIndexable& paquete, CalculoPaquete calculo);
    void continuar(const PaqueteIndexable& paquete, bool cambio);
    void cerrarSolicitud();
    void terminarTrabajo();
    void publicarOcupado();

    PuertosIndexadorCfdi m_puertos;
    EntornoCalculoIndice m_entorno; // usado solo en m_hilo
    std::shared_ptr<std::atomic_bool> m_cancelado;
    QThread m_hilo;
    QObject* m_contextoHilo = nullptr; // vive en m_hilo
    QList<Trabajo> m_cola;
    std::optional<Trabajo> m_actual;
    // Solicitud cuyos paquetes se estan procesando y si su indice cambio.
    std::optional<SolicitudId> m_solicitudEnCurso;
    bool m_solicitudCambio = false;
    bool m_iniciado = false;
    bool m_detenido = false;
    bool m_ocupadoPublicado = false;
};

} // namespace satcfdi
