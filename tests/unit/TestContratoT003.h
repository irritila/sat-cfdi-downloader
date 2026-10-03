#pragma once

#include <QObject>

// Pruebas minimas del contrato congelado en T003 corte 1. Las pruebas
// exhaustivas de reglas, sanitizer y servicios persistidos llegan en 2a.
class TestContratoT003 : public QObject {
    Q_OBJECT

private slots:
    void dedupKeyGoldenV1_data();
    void dedupKeyGoldenV1();
    void canonicaMapeoDc2();
    void canonicaInvarianteAOrdenYFormato();
    void canonicaRechazaEntradasInvalidas();
    void dedupKeyDesdeTexto();
    void matrizDuplicados();
    void catalogosIgualesAlEsquema();
    void timestampUtcIdaYVuelta();
    void dispatcherEjecutaFueraDelHiloGrafico();
    void demoEvaluaCreaYEliminaConSenales();
    void demoPerfilCrearYSembrar();
};
