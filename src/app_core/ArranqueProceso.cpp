#include "ArranqueProceso.h"

#include "SingleInstanceCoordinator.h"
#include "application/respaldo/RestauracionService.h"

#include <QThread>

#include <memory>

namespace satcfdi {

bool debeTerminarSinDestructores()
{
    return respaldo::RestauracionService::hilosDesacopladosVivos() > 0;
}

PreparacionProceso prepararProceso(SingleInstanceCoordinator& instancia,
                                   const AppBootstrapper& bootstrapper,
                                   const AplicadorRestauracion& restauracion)
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

    // T025.5: restauracion pendiente, antes de T019, SQLite y el worker. E/S
    // (renames, fsync, huella del staging) en un hilo temporal unido aqui.
    if (restauracion) {
        const std::unique_ptr<QThread> hilo(QThread::create([&p, &restauracion] { p.restauracion = restauracion(); }));
        hilo->setObjectName(QStringLiteral("satcfdi-restauracion-arranque"));
        hilo->start();
        hilo->wait();
        if (p.restauracion.estado == ResultadoRestauracionArranque::Estado::Error
            || p.restauracion.estado == ResultadoRestauracionArranque::Estado::Interrumpida) {
            p.tipo = PreparacionProceso::Tipo::Error;
            p.mensajeError = p.restauracion.mensajeError();
            return p;
        }
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
