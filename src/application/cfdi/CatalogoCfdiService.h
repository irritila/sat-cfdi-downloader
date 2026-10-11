#pragma once

#include "domain/common/Resultado.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/repositories/CfdiCatalogRepository.h"

#include <QFuture>
#include <QList>
#include <QObject>

namespace satcfdi {

class IndexadorCfdi;
class PersistenceDispatcher;

// API de consulta del indice de CFDI para T015.3 (explorador por solicitud).
//
// Asincronia: como los demas servicios, todo devuelve QFuture completado
// desde el PersistenceDispatcher (solo lectura, sin transaccion); el
// consumidor continua con QFuture::then(contextoGrafico, ...) y nunca espera
// en el hilo grafico. QML no ve SQL, archivos ni XML (D10).
//
// Ownership/hilo: QObject con afinidad al hilo grafico; lo crea y posee el
// composition root (despues del indexador y antes de cerrar el dispatcher).
class CatalogoCfdiService final : public QObject {
    Q_OBJECT

public:
    using ResultadoLista = Resultado<QList<CfdiListado>, ErrorPersistencia>;
    using ResultadoResumen = Resultado<ResumenIndiceSolicitud, ErrorPersistencia>;

    CatalogoCfdiService(PersistenceDispatcher& dispatcher, CfdiCatalogRepository& catalogo, IndexadorCfdi& indexador,
                        QObject* parent = nullptr);

    // CFDI de la solicitud: orden por fecha (descendente por defecto), filtro
    // de texto normalizado (sin acentos ni mayusculas) sobre RFC, nombres,
    // UUID, serie y folio, y filtro por tipo de comprobante. Solicitud nula o
    // sin indice -> lista vacia.
    QFuture<ResultadoLista> listar(const SolicitudId& solicitud, const FiltroListaCfdi& filtro = {});

    // Resumen del estado del indice de la solicitud (paquetes indexados,
    // pendientes, no disponibles, ilegibles; CFDI totales, ilegibles, no
    // soportados y parciales). `indexando` = hay trabajo en curso o en cola
    // para la solicitud y le quedan paquetes pendientes.
    QFuture<ResultadoResumen> resumen(const SolicitudId& solicitud);

    // (c) Revalida las huellas de los paquetes de la solicitud y reindexa lo
    // que cambio (T015.3 la llama al abrir el explorador). Se completa al
    // terminar el trabajo; los cambios se anuncian con indiceCambiado.
    QFuture<void> revisarSolicitud(const SolicitudId& solicitud);

    int versionParser() const noexcept;

signals:
    // El indice de la solicitud cambio (indexado, reindexado, origen no
    // disponible o eliminado con la solicitud): recargar lista y resumen.
    void indiceCambiado(const satcfdi::SolicitudId& solicitud);
    // El indexador empezo o termino de trabajar (para el indicador de
    // "indexando" del explorador).
    void actividadCambiada();

private:
    PersistenceDispatcher& m_dispatcher;
    CfdiCatalogRepository& m_catalogo;
    IndexadorCfdi& m_indexador;
};

} // namespace satcfdi
