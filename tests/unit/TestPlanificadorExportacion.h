#pragma once

#include <QObject>

// T026.1 (unit): PlanificadorExportacion con un catalogo en memoria:
// rango y direccion (D8), deduplicacion, orden determinista (D18), seleccion
// por identidad y desaparecida (D14), paquete no descargado (D4), lineas de
// mes por cobertura (D9, decision C1) y mes acreditado sin facturas (5004,
// D16).
class TestPlanificadorExportacion : public QObject {
    Q_OBJECT

private slots:
    void rangoAmbasOrdenaYDeduplica();
    void direccionEmitidasSoloEmitidas();
    void seleccionPorIdentidadYDesaparecida();
    void mes5004SinFacturasConteoCero();
    void lineasDeMesPorCobertura();
    void indexacionProgramadaEsDescargaPendiente();
    void paqueteNoDescargadoSeOmite();
    void planearCapturaUnaVezYValida();
    void planConAsignadorYOmitidas();
    void omisionesSinUuidConNombrePropio();
    void seleccionConIdentidadDeOtroUuid();
    void paqueteIlegibleEsCoberturaIncompleta();
    void fuenteConRutaYHuella();
};
