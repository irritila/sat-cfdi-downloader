#pragma once

#include "application/cfdi/HistorialDescargas.h"
#include "application/notificaciones/DestinoNavegacion.h"
#include "domain/common/Resultado.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QFuture>
#include <QList>
#include <QObject>

namespace satcfdi {

// T023.5: datos del Historial. La implementa app_core sobre
// CatalogoMensualCfdiService::historial; las pruebas usan un fake. Futures
// continuados en el hilo grafico; nunca se espera en el hilo grafico.
class FuenteHistorial : public QObject {
    Q_OBJECT

public:
    using ResultadoEntradas = Resultado<QList<EntradaHistorial>, ErrorPersistencia>;

    using QObject::QObject;
    ~FuenteHistorial() override = default;

    virtual QFuture<ResultadoEntradas> historial(const PerfilId& perfil) = 0;

signals:
    // Algo que afecta al historial cambio (descarga guiada, ejecutor, indice,
    // eliminaciones). Perfil nulo = cualquier perfil. Agrupado por la fuente.
    void datosCambiados(const satcfdi::PerfilId& perfil);
};

// T023.5: resuelve el destino de una notificacion de solicitud
// (DestinoNavegacion). Nunca falla: inexistente o error de lectura ->
// DestinoNavegacion::facturas(). La implementa app_core sobre
// CatalogoMensualCfdiService::ubicar.
class ResolucionDestinoNotificacion {
public:
    virtual ~ResolucionDestinoNotificacion() = default;
    virtual QFuture<DestinoNavegacion> resolver(const SolicitudId& solicitud) = 0;
};

} // namespace satcfdi
