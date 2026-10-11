#pragma once

#include "domain/common/Resultado.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/repositories/CfdiCatalogRepository.h"

#include <QFuture>
#include <QList>
#include <QObject>

namespace satcfdi {

// Consulta del indice de CFDI que la presentacion solicita (T015.3, T015
// D10). El composition root la implementa sobre CatalogoCfdiService; las
// pruebas, con fakes. Los futures se continuan en el hilo grafico
// (then(contexto, ...)); un future cancelado se trata como error de lectura.
// QML nunca ve SQL, archivos ni XML: solo los view models que la usan.
//
// Senales (hilo grafico):
// - indiceCambiado(id): el indice de esa solicitud cambio; recargar lista y
//   resumen.
// - actividadCambiada(): el indexador empezo o termino de trabajar; el
//   resumen (`indexando`) puede haber cambiado.
class ConsultaCatalogoCfdi : public QObject {
    Q_OBJECT

public:
    using ResultadoLista = Resultado<QList<CfdiListado>, ErrorPersistencia>;
    using ResultadoResumen = Resultado<ResumenIndiceSolicitud, ErrorPersistencia>;

    using QObject::QObject;
    ~ConsultaCatalogoCfdi() override = default;

    virtual QFuture<ResultadoLista> listar(const SolicitudId& solicitud, const FiltroListaCfdi& filtro) = 0;
    virtual QFuture<ResultadoResumen> resumen(const SolicitudId& solicitud) = 0;
    // Revalida las huellas de los ZIP de la solicitud (al abrir el
    // explorador); los cambios llegan con indiceCambiado.
    virtual QFuture<void> revisarSolicitud(const SolicitudId& solicitud) = 0;

signals:
    void indiceCambiado(const satcfdi::SolicitudId& solicitud);
    void actividadCambiada();
};

} // namespace satcfdi
