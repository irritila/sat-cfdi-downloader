#pragma once

// T024.2 (D9): exportacion de facturas (T026.3) gobernada por datos.
//
// ExportacionFacturasViewModel solo acepta el ExportacionFacturasService
// concreto (final, con su propio QThread), asi que este fake construye el
// servicio real sobre puertos sinteticos en memoria (sin disco ni libzip):
// - planear: plan con las facturas que la FuenteFacturasSintetica declara para
//   el alcance (mes, rango o seleccion), en el hilo grafico; un mes
//   Disponibles parcialmente agrega la linea "cobertura incompleta" de la
//   direccion que falta.
// - limpieza inicial retenida (retenerAlIniciar): el trabajo espera en
//   "Preparando" sin leer nada; el progreso visible lo fija emitirProgreso()
//   con valores declarados (sin tiempo real). liberar() deja terminar.
// - al liberar, el hilo del servicio recorre lector, parser, renderer, escritor
//   y destino en memoria: el resultado (exportadas, PDF, "sin PDF" declarados
//   por UUID, tamano declarado) es determinista, pero llega en un instante que
//   depende del hilo: quien lo capture debe esperar el estado Resultado.
// - errorAlReservar: el resultado es ese error de destino (p. ej.
//   DestinoExistente, que el dialogo muestra para corregir el nombre).
// - errorAlPlanear: planear falla con ese error de persistencia (el dialogo
//   muestra errorPlan y ofrece reintentar).

#include "FuentesSinteticas.h"

#include "application/cfdi/ExportacionFacturasService.h"

#include <QPromise>
#include <QSet>

#include <memory>
#include <optional>

namespace materiales {

struct DatosExportacion {
    bool retenerAlIniciar = true;
    quint64 bytesZip = 1'284'096;
    QSet<QString> sinPdf; // UUID canonicos cuyo PDF falla (el XML se incluye)
    std::optional<exportacion::ErrorDestinoExportacion::Codigo> errorAlReservar;
    std::optional<ErrorPersistencia::Tipo> errorAlPlanear;
};

class ExportacionSintetica {
public:
    explicit ExportacionSintetica(FuenteFacturasSintetica& fuente, DatosExportacion datos = {});
    ~ExportacionSintetica(); // libera la limpieza y cierra el servicio (une su hilo)

    ExportacionSintetica(const ExportacionSintetica&) = delete;
    ExportacionSintetica& operator=(const ExportacionSintetica&) = delete;

    ExportacionFacturasService* servicio() const { return m_servicio.get(); }

    // Progreso visible declarado (hilo grafico); el view model solo lo toma
    // con una exportacion en curso.
    void emitirProgreso(FaseExportacion fase, int actual, int total, int exportadas, int sinPdf, int omitidas);
    // Completa la limpieza inicial retenida: el trabajo sigue en su hilo.
    void liberar();

    PlanExportacion planDe(const AlcanceExportacion& alcance) const;

private:
    FuenteFacturasSintetica& m_fuente;
    DatosExportacion m_datos;
    std::shared_ptr<QPromise<int>> m_limpieza;
    std::unique_ptr<ExportacionFacturasService> m_servicio;
};

} // namespace materiales
