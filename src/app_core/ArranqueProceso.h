#pragma once

#include "AppBootstrapper.h"
#include "RestauracionArranque.h"

#include <QString>

namespace satcfdi {

class SingleInstanceCoordinator;

// Pasos 3 y 4 del arranque de main (T004 DA3): instancia unica y DESPUES
// bootstrap SQLite. Una instancia secundaria envia ActivateWindow y termina
// sin llamar a bootstrapper.preparar() (no toca SQLite).
//
// T025.5 (D13): entre ambos, una restauracion solicitada se aplica (o se
// continua o revierte segun su diario) con `restauracion`, en un hilo
// temporal que se une antes de seguir. Asi corre solo en la primaria, antes de
// MigracionRaizPaquetes (T019, dentro de preparar()), de SQLite y del worker.
// Si queda en Error (ni avanzar ni revertir) la app no arranca.
struct PreparacionProceso {
    enum class Tipo {
        Lista,        // primaria con base inicializada: continuar
        Secundaria,   // activacion enviada: salir con 0
        Error,        // instancia unica, restauracion o bootstrap fallaron: salir con 2
    };

    Tipo tipo = Tipo::Error;
    ArranquePreparado arranque; // solo si Lista
    QString mensajeError;       // saneado; solo si Error
    // T025.5: lo que paso con una restauracion pendiente (para la UI, T025.6).
    ResultadoRestauracionArranque restauracion;

    int codigoSalida() const { return tipo == Tipo::Secundaria ? 0 : 2; }
};

// T025.5 R2: true si al salir sigue vivo un hilo desacoplado de la
// restauracion (RestauracionService::hilosDesacopladosVivos() > 0). main lo
// consulta despues de destruir el composition root y, si es true, termina con
// _exit() en lugar de volver de main.
bool debeTerminarSinDestructores();

PreparacionProceso prepararProceso(SingleInstanceCoordinator& instancia,
                                   const AppBootstrapper& bootstrapper,
                                   const AplicadorRestauracion& restauracion = {});

} // namespace satcfdi
