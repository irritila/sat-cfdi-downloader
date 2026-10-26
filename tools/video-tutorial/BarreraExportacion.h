#pragma once

// T024.2 (decision del coordinador sobre D9): barrera del guion para la
// exportacion de facturas. El resultado lo completa el hilo del
// ExportacionFacturasService (puertos en memoria de tools/fakes-materiales):
// su contenido es determinista, su instante no. Como las demas barreras
// (Barreras.h), congela el reloj de cuadros (no consume cuadros) y espera en
// tiempo real a que el view model salga de Planeando/Exportando, con timeout.
// Despues exige el estado terminal esperado: un regreso al dialogo (error del
// plan o del destino), un cierre o un resultado distinto fallan con mensaje.

#include "ResultadosExportacion.h"

#include <QString>

#include <functional>

namespace satcfdi {
class ExportacionFacturasViewModel;
}

namespace video {

// Vacio si la exportacion termino en `esperado` (resultadosExportacion()); si no, el error: sin
// exportacion abierta, view model inexistente, timeout, interrumpida, de vuelta
// en el dialogo (con el error del plan o del destino), cerrada o con otro
// resultado. `bombear`: procesa eventos y dibuja sin grabar (false = la
// grabacion ya fallo).
QString esperarExportacion(satcfdi::ExportacionFacturasViewModel* vm, const QString& esperado, int timeoutMs,
                           const std::function<bool()>& bombear);

} // namespace video
