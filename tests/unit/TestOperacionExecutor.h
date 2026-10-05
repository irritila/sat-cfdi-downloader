#pragma once

#include <QObject>

// T007 corte B1: OperacionExecutor con FakeOperacionesSat, fakes en memoria y
// reloj/programador falsos (sin tiempo real).
class TestOperacionExecutor : public QObject {
    Q_OBJECT

private slots:
    void envioDesenlacesD7_data();
    void envioDesenlacesD7();
    void envioNuncaSeReenviaNiSeSeleccionaCreada();
    void eliminadaDuranteOperacionNoCambiaNada();
    void verificacionTerminadaRegistraPaquetesEnUnaTransaccion();
    void verificacionVencidaVencePaquetesNoDescargados();
    void agendaSinCambioTresVecesYCambio();
    void fallaNoCuentaComoSinCambio();
    void tresFallasIgualesSuspendenYPersisten();
    void fallasTransitoriasNoSuspenden();
    void descargaDesenlaces_data();
    void descargaDesenlaces();
    void vencimientoEstimadoLocal();
    void recuperacionAlArrancar();
    void recuperacionNoEscribeSiSeEliminaDuranteElPuerto();
    void colaSerialConPrioridad();
    void hiloGraficoSigueRespondiendo();
    void detenerEsperaHasta10sYCancela();
    void detenerSinOperacionesNoGeneraLogs();
    void destruirCierraConexionEnSuHiloSinAdvertencias();
    void gateDeCredencialUnaVezPorPerfil();
    void intencionSeConsumeSoloSiNoCambio();
};
