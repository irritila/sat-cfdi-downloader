#pragma once

#include <QObject>

// T023.2 (unit): MesCalendario y conversiones a hora Centro (D19) y regla
// pura evaluarCoberturaMensual (ADR 0019, D11): matriz por direccion,
// precedencia de estados y causas, tramos contiguos/hueco/frontera distinta,
// mes en curso frente a mes cerrado, canonicidad y reloj (enero, cambio de
// anio, medianoche con la zona del proceso distinta).
class TestCoberturaMensual : public QObject {
    Q_OBJECT

private slots:
    void mesCalendario();
    void fechasSat();
    void canonicidad();
    void matrizMesCerrado();
    void precedenciaYCausas();
    void tramosYFronteras();
    void mesEnCursoHoyFrenteAAyer();
    void relojEneroYCambioDeAnio();
    void medianocheConZonaDelProcesoDistinta();
    void deterministaSinImportarOrden();
    // ADR 0020
    void huecosInicialInternoYFinal();
    void huecosMesEnCursoYReservaEnCurso();
    void causasSuperadasPorCoberturaPosterior();
    void causasQueSiguenActivas();
};
