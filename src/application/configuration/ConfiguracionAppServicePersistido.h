#pragma once

#include "application/configuration/ConfiguracionAppService.h"
#include "application/persistence/PuertosPersistencia.h"

#include <QDateTime>
#include <functional>

namespace satcfdi {

class PersistenceDispatcher;
class ConfiguracionAppRepository;
class UnitOfWork;

// ConfiguracionAppService sobre ConfiguracionAppRepository (T004).
//
// Cada escritura es una tarea de PersistenceDispatcher: begin; actualizar
// campo (actualizada_en = reloj()); commit; en fallo rollback. El reloj se
// evalua en el hilo grafico al invocar la operacion. Tras commit, en el hilo
// grafico, emite configuracionCambiada(c) y despues completa el future.
// Si el servicio se destruye antes, el future queda cancelado.
//
// Ownership: no posee dispatcher, repositorio ni UnitOfWork; deben vivir mas
// que el servicio (orden de cierre de T003).
class ConfiguracionAppServicePersistido final : public ConfiguracionAppService {
    Q_OBJECT

public:
    ConfiguracionAppServicePersistido(PersistenceDispatcher& dispatcher,
                                      ConfiguracionAppRepository& configuracion,
                                      UnitOfWork& unidadDeTrabajo,
                                      RelojUtc reloj = relojSistema(),
                                      QObject* parent = nullptr);

    QFuture<ResultadoConfiguracion> obtener() override;
    QFuture<ResultadoConfiguracion> actualizarInicioAutomatico(bool habilitado) override;
    QFuture<ResultadoConfiguracion> actualizarMonitoreoPausado(bool pausado) override;
    QFuture<ResultadoConfiguracion> registrarUltimoCierre() override;

private:
    using Escritura = std::function<ResultadoConfiguracion(ConfiguracionAppRepository&, const QDateTime&)>;
    QFuture<ResultadoConfiguracion> escribir(Escritura escritura);

    PersistenceDispatcher& m_dispatcher;
    ConfiguracionAppRepository& m_configuracion;
    UnitOfWork& m_unidadDeTrabajo;
    RelojUtc m_reloj;
};

} // namespace satcfdi
