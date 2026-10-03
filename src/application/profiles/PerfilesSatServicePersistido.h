#pragma once

#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/PerfilesSatService.h"

namespace satcfdi {

class PersistenceDispatcher;
class PerfilSatRepository;
class UnitOfWork;

// PerfilesSatService sobre PerfilSatRepository (T003, ampliado en T005.1).
// Mismas reglas de hilo, senales y ownership que SolicitudesServicePersistido.
// - crear: valida fuera de transaccion; begin; insertar; commit;
//   perfilesCambiaron(). Unicidad ux_perfil_sat_rfc_vigente -> RfcDuplicado.
// - actualizarNombre: valida; begin; actualizarNombreVisible; commit;
//   perfilesCambiaron(). Sin fila -> PerfilInexistente.
class PerfilesSatServicePersistido final : public PerfilesSatService {
    Q_OBJECT

public:
    PerfilesSatServicePersistido(PersistenceDispatcher& dispatcher,
                                 PerfilSatRepository& perfiles,
                                 UnitOfWork& unidadDeTrabajo,
                                 RelojUtc reloj = relojSistema(),
                                 QObject* parent = nullptr);

    QFuture<ResultadoLista> listarNoEliminados() override;
    QFuture<ResultadoPerfil> obtener(const PerfilId& id) override;
    QFuture<ResultadoCrear> crear(const QString& rfc, const QString& nombre) override;
    QFuture<ResultadoActualizar> actualizarNombre(const PerfilId& id, const QString& nombre) override;

private:
    PersistenceDispatcher& m_dispatcher;
    PerfilSatRepository& m_perfiles;
    UnitOfWork& m_unidadDeTrabajo;
    RelojUtc m_reloj;
};

} // namespace satcfdi
