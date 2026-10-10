#pragma once

#include "application/persistence/PuertosPersistencia.h"
#include "application/primeruso/ConsultaPrimerUso.h"

namespace satcfdi {

class CredencialSatRepository;
class PersistenceDispatcher;
class PerfilSatRepository;
class SecretStore;
class SolicitudMasivaRepository;

// ConsultaPrimerUso sobre el PersistenceDispatcher (T014.3 D1, ADR 0016): una
// tarea lee perfiles visibles, el estado de credencial de cada uno hasta
// encontrar uno Lista (fila credencial_sat + SecretStore::obtenerEstado con el
// reloj, sin descifrar material; misma regla que CredencialesSatService) y si
// hay solicitudes visibles. Un error de credencial de un perfil cuenta como "no
// Lista"; un error de persistencia de perfiles o solicitudes falla la consulta.
//
// Ownership: referencias NO propietarias. La tarea solo captura repositorios
// (viven hasta despues de PersistenceDispatcher::cerrar()) y el SecretStore
// (vive mas que el root), nunca servicios: una consulta encolada durante el
// cierre no toca objetos ya destruidos.
class ConsultaPrimerUsoPersistida final : public ConsultaPrimerUso {
public:
    ConsultaPrimerUsoPersistida(PersistenceDispatcher& dispatcher, PerfilSatRepository& perfiles,
                                SolicitudMasivaRepository& solicitudes, CredencialSatRepository& credenciales,
                                SecretStore& secretStore, RelojUtc reloj = relojSistema());

    QFuture<ResultadoPrimerUso> consultar() override;

private:
    PersistenceDispatcher& m_dispatcher;
    PerfilSatRepository& m_perfiles;
    SolicitudMasivaRepository& m_solicitudes;
    CredencialSatRepository& m_credenciales;
    SecretStore& m_secretStore;
    RelojUtc m_reloj;
};

} // namespace satcfdi
