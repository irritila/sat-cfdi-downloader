// T013 D1: las pruebas de presentacion usan el mismo estilo Basic que la app.
// Se fija al crear la aplicacion de prueba, antes de cargar cualquier QML.

#include "presentation/estilo/EstiloVisual.h"

#include <QCoreApplication>

namespace {

void fijarEstiloDePruebas()
{
    satcfdi::presentacion::fijarEstiloBasico();
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(fijarEstiloDePruebas)
