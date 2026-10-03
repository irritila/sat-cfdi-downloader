#pragma once

#include "application/common/Errores.h"
#include "application/common/Resultado.h"
#include "application/requests/SolicitudDtos.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QFuture>
#include <QList>
#include <QObject>

namespace satcfdi {

// Contrato funcional de solicitudes consumido por presentacion (DA3).
//
// Asincronia: todas las operaciones devuelven QFuture. Las implementaciones
// pueden completarlo de inmediato (demo) o desde otro hilo (T003); el
// consumidor debe tratarlo siempre como asincrono (p. ej. QFuture::then con
// contexto QObject) y no bloquear el hilo grafico con waitForFinished().
//
// Ownership/hilo: QObject con afinidad al hilo grafico; lo crea y posee el
// composition root. Las senales se emiten en el hilo del objeto.
class SolicitudesService : public QObject {
    Q_OBJECT

public:
    using ResultadoLista = Resultado<QList<SolicitudResumen>, ErrorPersistencia>;
    using ResultadoDetalle = Resultado<SolicitudDetalle, ErrorObtener>;
    using ResultadoCrear = Resultado<SolicitudId, ErrorCrear>;

    using QObject::QObject;
    ~SolicitudesService() override = default;

    // Solicitudes visibles, mas recientes primero.
    virtual QFuture<ResultadoLista> listar() = 0;

    // Detalle por id; ErrorObtener::NoEncontrada si no existe o el id es nulo.
    virtual QFuture<ResultadoDetalle> obtener(const SolicitudId& id) = 0;

    // Fachada de UI para crear una solicitud local (DC3: T003 la conserva).
    // Errores de validacion: ErrorCrear::Tipo::Validacion.
    virtual QFuture<ResultadoCrear> crear(const NuevaSolicitudRequest& request) = 0;

signals:
    // Una solicitud fue creada o cambio. Se emite despues de que el cambio es
    // visible para listar()/obtener().
    void solicitudActualizada(const satcfdi::SolicitudId& id);
};

} // namespace satcfdi
