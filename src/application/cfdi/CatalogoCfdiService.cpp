#include "application/cfdi/CatalogoCfdiService.h"

#include "application/cfdi/IndexadorCfdi.h"
#include "application/persistence/PersistenceDispatcher.h"

#include <utility>

namespace satcfdi {

CatalogoCfdiService::CatalogoCfdiService(PersistenceDispatcher& dispatcher, CfdiCatalogRepository& catalogo,
                                         IndexadorCfdi& indexador, QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_catalogo(catalogo)
    , m_indexador(indexador)
{
    connect(&m_indexador, &IndexadorCfdi::indiceCambiado, this, &CatalogoCfdiService::indiceCambiado);
    connect(&m_indexador, &IndexadorCfdi::ocupadoCambiado, this, &CatalogoCfdiService::actividadCambiada);
}

QFuture<CatalogoCfdiService::ResultadoLista> CatalogoCfdiService::listar(const SolicitudId& solicitud,
                                                                        const FiltroListaCfdi& filtro)
{
    if (solicitud.esNulo()) {
        return QtFuture::makeReadyValueFuture(ResultadoLista::exito({}));
    }
    CfdiCatalogRepository* catalogo = &m_catalogo;
    return m_dispatcher.despachar<ResultadoLista>(
        [catalogo, solicitud, filtro] { return catalogo->listarCfdi(solicitud, filtro); });
}

QFuture<CatalogoCfdiService::ResultadoResumen> CatalogoCfdiService::resumen(const SolicitudId& solicitud)
{
    if (solicitud.esNulo()) {
        return QtFuture::makeReadyValueFuture(ResultadoResumen::exito({}));
    }
    CfdiCatalogRepository* catalogo = &m_catalogo;
    const int version = m_indexador.versionParser();
    return m_dispatcher
        .despachar<ResultadoResumen>(
            [catalogo, solicitud, version] { return catalogo->resumirSolicitud(solicitud, version); })
        .then(this, [this, solicitud](ResultadoResumen r) {
            if (!r.esExito()) {
                return r;
            }
            ResumenIndiceSolicitud valor = r.valor();
            valor.indexando = valor.paquetesPendientes > 0 && m_indexador.cubre(solicitud);
            return ResultadoResumen::exito(valor);
        });
}

QFuture<void> CatalogoCfdiService::revisarSolicitud(const SolicitudId& solicitud)
{
    return m_indexador.revisarSolicitud(solicitud);
}

int CatalogoCfdiService::versionParser() const noexcept
{
    return m_indexador.versionParser();
}

} // namespace satcfdi
