#pragma once

#include <QObject>

// T005.1 corte A: ConsultaPreparacionPerfiles con FakePerfilesSatService y
// FakeCredencialesSatService (promesas manuales), y comportamiento de esos
// fakes para presentacion.
class TestPreparacionPerfiles : public QObject {
    Q_OBJECT

private slots:
    void catalogoYRegla();
    void listarPersistidosPublicaVerificando();
    void verificarMapeaCadaEstadoYVigencia();
    void inactivoConListaNoEsElegible();
    void errorOCancelacionEsEstadoNoDisponible();
    void listarVerificadosConRespuestasDesordenadas();
    void listarListosParaSolicitudesSoloListos();
    void errorDePerfilesSePropaga();
    void fakesRegistranSinSecretosYEmitenExplicitamente();
};
