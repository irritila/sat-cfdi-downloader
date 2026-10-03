#include "ArranqueProceso.h"

#include "SingleInstanceCoordinator.h"

namespace satcfdi {

PreparacionProceso prepararProceso(SingleInstanceCoordinator& instancia,
                                   const AppBootstrapper& bootstrapper)
{
    PreparacionProceso p;
    switch (instancia.adquirir()) {
    case SingleInstanceCoordinator::Rol::Primary:
        break;
    case SingleInstanceCoordinator::Rol::SecondaryActivated:
        p.tipo = PreparacionProceso::Tipo::Secundaria;
        return p;
    case SingleInstanceCoordinator::Rol::Error:
        p.tipo = PreparacionProceso::Tipo::Error;
        p.mensajeError = instancia.error();
        return p;
    }

    // Inicializa SQLite en un hilo temporal y lo une antes de crear el grafo.
    auto arranque = bootstrapper.preparar();
    if (!arranque) {
        p.tipo = PreparacionProceso::Tipo::Error;
        p.mensajeError = arranque.error().mensaje;
        return p;
    }
    p.tipo = PreparacionProceso::Tipo::Lista;
    p.arranque = std::move(arranque).valor();
    return p;
}

} // namespace satcfdi
