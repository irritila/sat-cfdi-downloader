#include "application/configuration/ConfiguracionAppServicePersistido.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "ports/repositories/ConfiguracionAppRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <utility>

namespace satcfdi {

// Las lambdas despachadas capturan solo punteros a puertos y copias de datos;
// nunca `this`.

ConfiguracionAppServicePersistido::ConfiguracionAppServicePersistido(
    PersistenceDispatcher& dispatcher, ConfiguracionAppRepository& configuracion,
    UnitOfWork& unidadDeTrabajo, RelojUtc reloj, QObject* parent)
    : ConfiguracionAppService(parent)
    , m_dispatcher(dispatcher)
    , m_configuracion(configuracion)
    , m_unidadDeTrabajo(unidadDeTrabajo)
    , m_reloj(std::move(reloj))
{
}

QFuture<ConfiguracionAppService::ResultadoConfiguracion> ConfiguracionAppServicePersistido::obtener()
{
    ConfiguracionAppRepository* repo = &m_configuracion;
    return m_dispatcher.despachar<ResultadoConfiguracion>([repo]() { return repo->obtener(); });
}

QFuture<ConfiguracionAppService::ResultadoConfiguracion>
ConfiguracionAppServicePersistido::actualizarInicioAutomatico(bool habilitado)
{
    return escribir([habilitado](ConfiguracionAppRepository& repo, const QDateTime& en) {
        return repo.actualizarInicioAutomatico(habilitado, en);
    });
}

QFuture<ConfiguracionAppService::ResultadoConfiguracion>
ConfiguracionAppServicePersistido::actualizarMonitoreoPausado(bool pausado)
{
    return escribir([pausado](ConfiguracionAppRepository& repo, const QDateTime& en) {
        return repo.actualizarMonitoreoPausado(pausado, en);
    });
}

QFuture<ConfiguracionAppService::ResultadoConfiguracion>
ConfiguracionAppServicePersistido::registrarUltimoCierre()
{
    return escribir([](ConfiguracionAppRepository& repo, const QDateTime& en) {
        return repo.registrarUltimoCierre(en);
    });
}

QFuture<ConfiguracionAppService::ResultadoConfiguracion>
ConfiguracionAppServicePersistido::escribir(Escritura escritura)
{
    const QDateTime ahora = m_reloj();
    ConfiguracionAppRepository* repo = &m_configuracion;
    UnitOfWork* uow = &m_unidadDeTrabajo;

    auto tarea = [repo, uow, ahora, escritura = std::move(escritura)]() -> ResultadoConfiguracion {
        using R = ResultadoConfiguracion;
        auto begin = uow->begin();
        if (!begin.esExito()) {
            return R::fallo(std::move(begin).error());
        }
        auto actualizada = escritura(*repo, ahora);
        if (!actualizada.esExito()) {
            (void)uow->rollback();
            return actualizada;
        }
        auto commit = uow->commit();
        if (!commit.esExito()) {
            (void)uow->rollback();
            return R::fallo(std::move(commit).error());
        }
        return actualizada;
    };

    return m_dispatcher.despachar<ResultadoConfiguracion>(std::move(tarea))
        .then(this, [this](ResultadoConfiguracion r) {
            if (r.esExito()) {
                emit configuracionCambiada(r.valor());
            }
            return r;
        });
}

} // namespace satcfdi
