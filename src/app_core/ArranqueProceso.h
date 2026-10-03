#pragma once

#include "AppBootstrapper.h"

#include <QString>

namespace satcfdi {

class SingleInstanceCoordinator;

// Pasos 3 y 4 del arranque de main (T004 DA3): instancia unica y DESPUES
// bootstrap SQLite. Una instancia secundaria envia ActivateWindow y termina
// sin llamar a bootstrapper.preparar() (no toca SQLite).
struct PreparacionProceso {
    enum class Tipo {
        Lista,        // primaria con base inicializada: continuar
        Secundaria,   // activacion enviada: salir con 0
        Error,        // instancia unica o bootstrap fallaron: salir con 2
    };

    Tipo tipo = Tipo::Error;
    ArranquePreparado arranque; // solo si Lista
    QString mensajeError;       // saneado; solo si Error

    int codigoSalida() const { return tipo == Tipo::Secundaria ? 0 : 2; }
};

PreparacionProceso prepararProceso(SingleInstanceCoordinator& instancia,
                                   const AppBootstrapper& bootstrapper);

} // namespace satcfdi
