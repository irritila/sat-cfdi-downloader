#pragma once

#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/PerfilesSatService.h"

namespace satcfdi {

class PersistenceDispatcher;
class PerfilSatRepository;
class UnitOfWork;

// PerfilesSatService sobre PerfilSatRepository (T003). Mismas reglas de hilo,
// senales y ownership que SolicitudesServicePersistido.
// crearPerfilSimulado: valida (rfc::normalizarYValidar, razon social no
// vacia) fuera de transaccion; begin; insertar; commit; perfilesCambiaron().
// ErrorPersistencia::Unicidad -> ErrorCrearPerfil::Integridad.
// Implementado en T003 corte 2a.
class PerfilesSatServicePersistido final : public PerfilesSatService {
    Q_OBJECT

public:
    PerfilesSatServicePersistido(PersistenceDispatcher& dispatcher,
                                 PerfilSatRepository& perfiles,
                                 UnitOfWork& unidadDeTrabajo,
                                 RelojUtc reloj = relojSistema(),
                                 QObject* parent = nullptr);

    QFuture<ResultadoLista> listarActivos() override;
    QFuture<ResultadoCrearPerfil> crearPerfilSimulado(const NuevoPerfilSimuladoRequest& request) override;

private:
    PersistenceDispatcher& m_dispatcher;
    PerfilSatRepository& m_perfiles;
    UnitOfWork& m_unidadDeTrabajo;
    RelojUtc m_reloj;
};

} // namespace satcfdi
