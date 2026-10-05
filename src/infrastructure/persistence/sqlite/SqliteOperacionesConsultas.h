#pragma once

#include <QString>

// SQL de las selecciones de SqliteOperacionesSolicitudRepository (T007, D9).
// INTERNO de src/infrastructure: se expone solo para que las pruebas de
// infraestructura ejecuten EXPLAIN QUERY PLAN sobre las MISMAS consultas que
// usa el repositorio. Parametros con nombre indicados en cada funcion.
namespace satcfdi::sqlite::operaciones {

QString sqlPerfilesConTrabajo();      // :ahora
QString sqlVencimientosEstimados();   // :ahora, :limite
QString sqlIntenciones();             // :perfiles (arreglo JSON de ids), :limite
QString sqlVerificacionesDebidas();   // :ahora, :perfiles, :limite
QString sqlDescargasAutomaticas();    // :perfiles, :limite
QString sqlObtenerPaquete();          // :id
QString sqlPaquetesReintentables();   // :id (solicitud)
QString sqlSolicitudesEnviando();     // sin parametros
QString sqlPaquetesDescargando();     // sin parametros

} // namespace satcfdi::sqlite::operaciones
