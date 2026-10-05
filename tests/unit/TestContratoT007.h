#pragma once

#include <QObject>

// T007 corte A: contratos de OperacionesSat, politicas puras (D7, D8, D6,
// D10), SenalCancelacion, adaptador nulo y fakes (FakeOperacionesSat,
// FakeProgramador, FakeOperacionesSolicitud).
class TestContratoT007 : public QObject {
    Q_OBJECT

private slots:
    void catalogosYClaveDeFalla();
    void destinoDeEnvioD7();
    void agendaTrasExitoD8();
    void agendaTrasFallaYSuspensionD8();
    void desenlaceDescargaYVencimientoEstimado();
    void senalCancelacionEntreHilos();
    void adaptadorNuloNuncaSimulaExito();
    void fakeOperacionesSatGuionBarreraYCancelacion();
    void fakeProgramadorAvanzaSinTiempoReal();
    void repositorioEnvioYEliminacion();
    void repositorioVerificacionYPaquetes();
    void repositorioDescargaYVencimiento();
    void repositorioIntencionesCondicionales();
};
