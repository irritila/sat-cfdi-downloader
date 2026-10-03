#pragma once

#include "application/common/Errores.h"
#include "application/requests/SolicitudDtos.h"
#include "domain/common/Resultado.h"
#include "domain/solicitudes/Duplicados.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QFuture>
#include <QList>
#include <QObject>

namespace satcfdi {

// Contrato funcional de solicitudes consumido por presentacion (T003 DA1).
//
// Asincronia: todas las operaciones devuelven QFuture. Las implementaciones
// pueden completarlo de inmediato (demo/fakes) o desde PersistenceDispatcher
// (persistida). El consumidor lo trata siempre como asincrono
// (QFuture::then(contexto, ...)) y nunca usa waitForFinished() en el hilo
// grafico.
//
// Ownership/hilo: QObject con afinidad al hilo grafico; lo crea y posee el
// composition root. Las senales se emiten en el hilo grafico, solo despues del
// commit y ANTES de completar el future publico correspondiente. Un rollback
// no emite senales.
class SolicitudesService : public QObject {
    Q_OBJECT

public:
    using ResultadoLista = Resultado<QList<SolicitudResumen>, ErrorPersistencia>;
    using ResultadoDetalle = Resultado<SolicitudDetalle, ErrorObtener>;
    using ResultadoCrear = Resultado<SolicitudId, ErrorCrear>;
    // ErrorCrear: Validacion, FiltroInvalido o Persistencia (nunca
    // DedupBloqueado/RequiereConfirmacion: esos son clasificaciones validas).
    using ResultadoEvaluarDuplicado = Resultado<EvaluacionDuplicado, ErrorCrear>;
    using ResultadoEliminar = Resultado<ResultadoEliminacion, ErrorPersistencia>;

    using QObject::QObject;
    ~SolicitudesService() override = default;

    // Solicitudes visibles, mas recientes primero (creada_en DESC).
    virtual QFuture<ResultadoLista> listar() = 0;

    // Detalle visible (cabecera, filtros, paquetes y logs visibles).
    // ErrorObtener::NoEncontrada si el id es nulo, no existe o esta eliminado.
    virtual QFuture<ResultadoDetalle> obtener(const SolicitudId& id) = 0;

    // Fachada de UI (DC3 de T002): equivale a
    // crearLocal(request, ConfirmacionDuplicado::SinConfirmar). No virtual:
    // las implementaciones sobrescriben crearLocal().
    QFuture<ResultadoCrear> crear(const NuevaSolicitudRequest& request)
    {
        return crearLocal(request, ConfirmacionDuplicado::SinConfirmar);
    }

    // Valida request y perfil, calcula dedup_key v1 y clasifica duplicados
    // sin escribir. Orientativo: crearLocal() vuelve a clasificar dentro de
    // BEGIN IMMEDIATE.
    virtual QFuture<ResultadoEvaluarDuplicado> evaluarDuplicado(const NuevaSolicitudRequest& request) = 0;

    // Crea la solicitud local en estado Creada en una transaccion:
    // Bloqueado -> ErrorCrear::DedupBloqueado (tambien si el INSERT viola
    // ux_solicitud_masiva_dedup_bloqueante); RequiereConfirmacion sin
    // confirmacion -> ErrorCrear::RequiereConfirmacion; con Confirmada se
    // inserta y se registra ademas log duplicado_confirmado (origen usuario).
    // Exito: emite listaCambiada() y solicitudActualizada(id).
    virtual QFuture<ResultadoCrear> crearLocal(const NuevaSolicitudRequest& request,
                                               ConfirmacionDuplicado confirmacion) = 0;

    // Eliminacion logica idempotente de solicitud, paquetes y logs (mismo
    // eliminado_en, una transaccion). cambio=true emite listaCambiada() y
    // solicitudEliminada(id); cambio=false no emite.
    virtual QFuture<ResultadoEliminar> eliminar(const SolicitudId& id) = 0;

signals:
    // El conjunto o el orden de solicitudes visibles cambio.
    void listaCambiada();
    // Una solicitud fue creada o cambio; ya es visible para listar()/obtener().
    void solicitudActualizada(const satcfdi::SolicitudId& id);
    // Una solicitud fue eliminada logicamente; obtener(id) ya devuelve NoEncontrada.
    void solicitudEliminada(const satcfdi::SolicitudId& id);
};

} // namespace satcfdi
