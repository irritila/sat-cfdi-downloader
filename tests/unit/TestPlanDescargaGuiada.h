#pragma once

#include <QObject>

// T023.3 (unit): plan puro de la descarga guiada y oferta del boton
// (ADR 0019/0020, D7, D12, D17, D19) sobre evaluarCoberturaMensual.
class TestPlanDescargaGuiada : public QObject {
    Q_OBJECT

private slots:
    void mesCerradoSinEvidenciaPideMesCompletoEnAmbas();
    void reutilizaEnCursoYNoPideDireccionCubierta();
    void mesEnCursoConRelojCentro();
    void completarMesPorHuecosYReserva();
    void inhibicion5002y5005();
    void mesFuturo();
    void reservaParcialCreaSegmentosLibres();
    void accionDistintaDeLaOfrecida();
    void creadasSinEnviarSeOfrecenParaReenviar();
};
