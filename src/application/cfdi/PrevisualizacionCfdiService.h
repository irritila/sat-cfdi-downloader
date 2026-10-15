#pragma once

#include "application/cfdi/CalculoIndicePaquete.h"
#include "domain/common/Resultado.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/repositories/CfdiCatalogRepository.h"

#include <QFuture>
#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>

namespace satcfdi {

class AlmacenPdfCfdi;
class ArchivosZipCfdi;
class CfdiArchiveReader;
class CfdiPdfRenderer;
class PersistenceDispatcher;

// PDF listo para mostrarse. `rutaArchivo` es absoluta y vive en la cache de
// PDF (o en el temporal de la app si la cache no se pudo escribir).
// `nombreSugerido`: "CFDI-<serie><folio>-<8 primeros del UUID>.pdf" (solo
// [0-9A-Za-z-_]), sin RFC ni nombres.
struct PdfCfdi {
    QString rutaArchivo;
    QString nombreSugerido;
};

struct ErrorPrevisualizacion {
    enum class Tipo {
        OrigenNoDisponible, // el ZIP falta, no se puede leer o el CFDI ya no esta en el indice
        VersionNoSoportada, // CFDI 3.2 u otra version (T015 D5): sin PDF
        Ilegible,           // XML ilegible, no es CFDI o ZIP corrupto: sin PDF
        Cancelada,          // otra peticion la reemplazo o el servicio se detuvo
        Fallo,              // error inesperado al leer el indice, generar o escribir
    } tipo = Tipo::Fallo;
};

// Puertos (referencias NO propietarias que el composition root crea antes y
// destruye despues del servicio).
struct PuertosPrevisualizacionCfdi {
    PersistenceDispatcher& dispatcher; // lectura del indice (paquete, huella)
    CfdiCatalogRepository& catalogo;   // solo dentro de tareas del dispatcher
    CfdiArchiveReader& lector;         // hilo del servicio
    ArchivosZipCfdi& archivos;         // hilo del servicio
    ParserCfdiFn parser;               // puro
    CfdiPdfRenderer& renderer;         // hilo del servicio
    AlmacenPdfCfdi& almacen;           // hilo del servicio
    // Revalida y reindexa la solicitud (IndexadorCfdi::revisarSolicitud); el
    // future se completa al terminar. Se llama en el hilo grafico.
    std::function<QFuture<void>(const SolicitudId&)> reindexar;
    int versionParser = 1;
};

// Previsualizacion PDF de un CFDI (T015.4, T015 D7-D11).
//
// Hilos y ownership: QObject con afinidad al hilo grafico; lo crea y posee el
// composition root. Posee su PROPIO QThread ("satcfdi-pdf-cfdi"): huella del
// ZIP, lectura de la entrada, parseo, render (QPdfWriter) y E/S de la cache
// corren ahi, una peticion a la vez. Nunca usa el hilo grafico ni la cola
// del OperacionExecutor (SAT). El indice se lee por el PersistenceDispatcher.
// Los futures se completan en el hilo grafico.
//
// generar(solicitud, paqueteId, posicion):
//  1. Lee del indice el paquete (ruta y huella) y la identidad del CFDI en
//     (paqueteId, posicion): UUID y estado de parseo. Sin paquete o sin ese
//     CFDI -> OrigenNoDisponible.
//  2. Revalida el ZIP: si falta -> OrigenNoDisponible (y pide reindexar para
//     que el explorador lo refleje). Si cambio (tamano/fecha y SHA-256) o el
//     indice no esta vigente -> reindexa primero (y espera) y reintenta una
//     vez; si sigue sin estar vigente -> OrigenNoDisponible. Nunca hay PDF
//     de un ZIP desactualizado.
//  3. Lee SOLO la entrada de esa posicion y la parsea. Si no es el CFDI
//     seleccionado (UUID distinto del indexado al pedirlo; sin UUID indexado,
//     estado de parseo distinto) -> OrigenNoDisponible sin PDF: nunca se
//     muestra un CFDI distinto del elegido (la lista se recarga con
//     indiceCambiado). VersionNoSoportada o Ilegible/NoEsCfdi -> error sin PDF.
//  4. Cache por UUID + SHA-256 del XML + version del generador: si existe se
//     devuelve; si no, se genera y se guarda (o en el temporal).
// La ultima peticion gana: una nueva llamada a generar() hace que las
// anteriores aun no terminadas se completen con Cancelada.
//
// guardarComo(rutaPdf, destino): copia atomica (temporal + rename) en el
// hilo del servicio; solo acepta PDF de la cache o del temporal. Reemplaza un
// destino existente (el dialogo nativo ya lo confirmo). Error -> Fallo; el
// destino queda como estaba.
//
// Logs (D11): solo tipo de error, evento de cache, paginas e Id local del
// paquete enmascarado; nunca RFC, nombres, UUID, montos, sellos ni rutas.
class PrevisualizacionCfdiService final : public QObject {
    Q_OBJECT

public:
    using ResultadoPdf = Resultado<PdfCfdi, ErrorPrevisualizacion>;
    using ResultadoGuardar = Resultado<bool, ErrorPrevisualizacion>;

    explicit PrevisualizacionCfdiService(PuertosPrevisualizacionCfdi puertos, QObject* parent = nullptr);
    ~PrevisualizacionCfdiService() override;

    PrevisualizacionCfdiService(const PrevisualizacionCfdiService&) = delete;
    PrevisualizacionCfdiService& operator=(const PrevisualizacionCfdiService&) = delete;

    QFuture<ResultadoPdf> generar(const SolicitudId& solicitud, const QString& paqueteId, int posicion);
    QFuture<ResultadoGuardar> guardarComo(const QString& rutaPdf, const QString& destinoAbsoluto);

    // Salida: las peticiones pendientes terminan con Cancelada y no se
    // aceptan nuevas. Idempotente. El destructor la llama y une el hilo.
    void detener();

    // Directorios donde quedan los PDF (lista permitida de Vista Previa).
    QString directorioCache() const;
    QString directorioTemporal() const;

    const QThread* hilo() const noexcept { return &m_hilo; }

    // Nombre sugerido para "Guardar PDF..." (puro; expuesto para pruebas).
    static QString nombreSugerido(const QString& serie, const QString& folio, const QString& uuid);

private:
    struct Peticion;
    struct Paso;
    struct LecturaIndice;

    void intentar(std::shared_ptr<Peticion> peticion, bool permitirReindexar);
    Paso preparar(const LecturaIndice& indice, Peticion& peticion);
    void terminar(Peticion& peticion, ResultadoPdf resultado);

    PuertosPrevisualizacionCfdi m_puertos;
    QThread m_hilo;
    QObject* m_contextoHilo = nullptr; // vive en m_hilo
    std::shared_ptr<std::atomic<quint64>> m_generacion;
    bool m_detenido = false;
};

} // namespace satcfdi
