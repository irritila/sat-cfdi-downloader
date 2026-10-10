#include "application/primeruso/ConsultaPrimerUsoPersistida.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "ports/SecretStore.h"
#include "ports/repositories/CredencialSatRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"

namespace satcfdi {

ConsultaPrimerUsoPersistida::ConsultaPrimerUsoPersistida(PersistenceDispatcher& dispatcher,
                                                         PerfilSatRepository& perfiles,
                                                         SolicitudMasivaRepository& solicitudes,
                                                         CredencialSatRepository& credenciales,
                                                         SecretStore& secretStore, RelojUtc reloj)
    : m_dispatcher(dispatcher)
    , m_perfiles(perfiles)
    , m_solicitudes(solicitudes)
    , m_credenciales(credenciales)
    , m_secretStore(secretStore)
    , m_reloj(reloj ? std::move(reloj) : relojSistema())
{
}

QFuture<ConsultaPrimerUso::ResultadoPrimerUso> ConsultaPrimerUsoPersistida::consultar()
{
    PerfilSatRepository* perfiles = &m_perfiles;
    SolicitudMasivaRepository* solicitudes = &m_solicitudes;
    CredencialSatRepository* credenciales = &m_credenciales;
    SecretStore* store = &m_secretStore;
    const QDateTime ahora = m_reloj();
    return m_dispatcher.despachar<ResultadoPrimerUso>([perfiles, solicitudes, credenciales, store, ahora]() {
        using R = ResultadoPrimerUso;
        EstadoPrimerUso e;
        auto lista = perfiles->listarVisibles();
        if (!lista) {
            return R::fallo(std::move(lista).error());
        }
        e.hayPerfiles = !lista.valor().isEmpty();
        for (const PerfilSat& p : lista.valor()) {
            auto fila = credenciales->obtenerPorPerfil(p.id);
            if (!fila || !fila.valor()) {
                continue;
            }
            const CredencialSat& c = *fila.valor();
            const auto ref = CredencialRef::desdeReferencias(c.certificadoRef, c.llavePrivadaRef, c.contrasenaRef);
            if (!ref) {
                continue;
            }
            auto estado = store->obtenerEstado(*ref, ahora);
            if (estado && estado.valor() == EstadoCredencial::Lista) {
                e.hayEFirmaLista = true;
                break;
            }
        }
        auto filas = solicitudes->listarVisibles();
        if (!filas) {
            return R::fallo(std::move(filas).error());
        }
        e.haySolicitudes = !filas.valor().isEmpty();
        return R::exito(e);
    });
}

} // namespace satcfdi
