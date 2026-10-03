#pragma once

#include "application/persistence/PuertosPersistencia.h"
#include "application/requests/SolicitudesService.h"

namespace satcfdi {

class PersistenceDispatcher;

// SolicitudesService sobre repositorios SQLite (T003 DA1/DA4).
//
// - Cada operacion despacha UNA tarea a PersistenceDispatcher con toda la
//   secuencia repositorio/UnitOfWork; nunca toca SQL en el hilo grafico.
// - La continuacion corre en este objeto (hilo grafico): tras commit emite
//   senales y despues completa el future publico.
// - crearLocal: valida dominio y perfil activo fuera de la transaccion;
//   BEGIN IMMEDIATE; clasificarDuplicado; Bloqueado/RequiereConfirmacion sin
//   confirmar -> rollback y error; insertarCreada; log solicitud_creada (y
//   duplicado_confirmado si aplica) via LogSanitizer; commit; senales.
//   Unicidad en ux_solicitud_masiva_dedup_bloqueante -> DedupBloqueado.
// - eliminar: una transaccion; mismo eliminado_en para solicitud, paquetes y
//   logs; idempotente.
//
// Ownership: no posee dispatcher ni puertos; deben vivir mas que este objeto.
// Implementado en T003 corte 2a.
class SolicitudesServicePersistido final : public SolicitudesService {
    Q_OBJECT

public:
    SolicitudesServicePersistido(PersistenceDispatcher& dispatcher,
                                 PuertosPersistencia puertos,
                                 RelojUtc reloj = relojSistema(),
                                 QObject* parent = nullptr);

    QFuture<ResultadoLista> listar() override;
    QFuture<ResultadoDetalle> obtener(const SolicitudId& id) override;
    QFuture<ResultadoEvaluarDuplicado> evaluarDuplicado(const NuevaSolicitudRequest& request) override;
    QFuture<ResultadoCrear> crearLocal(const NuevaSolicitudRequest& request,
                                       ConfirmacionDuplicado confirmacion) override;
    QFuture<ResultadoEliminar> eliminar(const SolicitudId& id) override;

private:
    PersistenceDispatcher& m_dispatcher;
    PuertosPersistencia m_puertos;
    RelojUtc m_reloj;
};

} // namespace satcfdi
