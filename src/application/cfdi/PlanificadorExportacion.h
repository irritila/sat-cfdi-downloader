#pragma once

#include "application/persistence/PuertosPersistencia.h"
#include "domain/cfdi/ExportacionFacturas.h"
#include "domain/common/Resultado.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/repositories/CatalogoMensualRepository.h"

#include <QDateTime>

#include <functional>

namespace satcfdi {

// Plan de una exportacion de facturas (T026.1, T026 D3, D4, D8, D9, D14,
// D16). No lee archivos ni el SAT: parte de la instantanea del catalogo
// (CatalogoMensualRepository::capturarParaExportar). La relectura, el PDF y
// las omisiones de lectura son de T026.2.
//
// Reglas:
// - Mes o rango: todos los CFDI deduplicados (D13) de cada mes del alcance en
//   la direccion pedida (nullopt = ambas).
// - Seleccion: exactamente lo elegido, por (paqueteId, posicion) dentro del
//   mes de la factura; si esa identidad ya no esta pero su UUID canonico
//   sigue en el catalogo del mes y la direccion (otra fuente tomo su lugar,
//   ADR 0019 D13), se exporta esa fuente. Si no, omision "ya no disponible".
//   Si la identidad existe pero su UUID canonico no es el elegido, no es la
//   factura elegida: misma regla que si no existiera. Sin UUID canonico
//   elegido basta la identidad. Una referencia repetida cuenta una vez. Sin
//   lineas de mes.
// - D4: un CFDI cuyo paquete aparece en la evidencia del mes en un estado
//   distinto de Descargado se omite con "no disponible en este equipo".
// - Cada factura planeada lleva la ruta y la huella del ZIP de su paquete
//   (InstantaneaExportacion::paquetes); sin ellas se omite con "no
//   disponible en este equipo".
// - Un UUID canonico repetido se planea una vez (el primero en el orden).
// - Orden determinista: direccion (Emitidos primero), mes, UUID canonico (sin
//   UUID al final), paquete y posicion. En ese orden un solo
//   AsignadorNombresExportacion nombra las facturas planeadas
//   (FacturaPlaneada::nombre) y las omitidas al planear (su identificador),
//   asi cada omision sin UUID tiene su propio `sin-uuid-<n>` (D9, D17, D18).
// - Lineas de mes (mes o rango, por direccion pedida; nunca en un mes
//   futuro), con evaluarCoberturaMensual: causa activa de la direccion
//   (incluye OrigenNoDisponible e Ilegible, que no conservan filas de CFDI)
//   -> "cobertura incompleta"; si no, solicitudes en curso de la direccion
//   -> "descargas pendientes"; si no, cobertura que no alcanza la frontera
//   -> "cobertura incompleta". Un mes acreditado sin CFDI (5004) no produce
//   linea ni facturas.
class PlanificadorExportacion {
public:
    // Hay trabajo de indexacion en curso o en cola para la solicitud
    // (ProgresoIndexacionCfdi::cubre). Vacio = nunca.
    using IndexacionProgramada = std::function<bool(const SolicitudId&)>;

    // `versionParser`: version vigente del indice. `indexacion` se invoca en
    // el hilo de planear(): debe ser seguro llamarla alli. Referencia no
    // propietaria al catalogo.
    PlanificadorExportacion(CatalogoMensualRepository& catalogo, RelojUtc reloj, int versionParser,
                            IndexacionProgramada indexacion = {});

    // Captura la instantanea del alcance (una transaccion de lectura) y la
    // planifica con el instante del reloj, capturado una vez. Hilo: tarea del
    // PersistenceDispatcher. Perfil nulo o seleccion vacia -> plan vacio sin
    // leer. Errores: los del repositorio; Interno si el rango esta invertido.
    Resultado<PlanExportacion, ErrorPersistencia> planear(const AlcanceExportacion& alcance) const;

    // Regla pura sobre una instantanea ya capturada (cualquier hilo).
    static PlanExportacion planificar(const AlcanceExportacion& alcance, const InstantaneaExportacion& instantanea,
                                      const QDateTime& instante, const IndexacionProgramada& indexacion = {});

private:
    CatalogoMensualRepository& m_catalogo;
    RelojUtc m_reloj;
    int m_versionParser;
    IndexacionProgramada m_indexacion;
};

} // namespace satcfdi
