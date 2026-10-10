#pragma once

#include <QObject>

// T007 corte B1: WorkerLocal sobre OperacionExecutor real (hilo propio) con
// FakeOperacionesSat, fakes en memoria y reloj/programador falsos.
class TestWorkerLocal : public QObject {
    Q_OBJECT

private slots:
    void recuperacionAntesDeCualquierCiclo();
    void verificacionDebidaSeEncolaUnaSolaVez();
    void workerNoSeleccionaCreadaYDescargaAutomatica();
    void vencimientoEstimadoAunEnPausa();
    void pausaNoLlamaAlPuerto();
    void pausaRegistraIntencionesYReanudaEnOrden();
    void intencionQueYaNoAplicaSeDescartaConLog();
    void intencionNuevaDuranteOtraNoSePierde();
    void gateDeCredencialUnaVezPorPerfil();
    void enviarSeEjecutaAunEnPausa();
    void manualAntesQueAutomaticoEnUnCiclo();
    void instantaneasDeEstadoYPendientes();
    // T014.2: reintento por paquete.
    void reintentoPorPaqueteActivoSoloEsePaquete();
    void reintentoPorPaquete5008SinTrafico();
    void reintentoPorPaqueteEnPausaSobreviveReinicio();
    void intencionPaqueteObsoletaSeDescartaConLog();
    void intencionesSolicitudYPaqueteEnOrdenSinDuplicar();
    void recuperacionConservaIntencionPorPaquete();
    void intencionPaqueteDeSolicitudEliminadaSeLimpiaSinLog();
    void reintentoPorPaqueteCon5008QuedaErrorSaneado();
    void intencionPaqueteRespetaGateDeCredencial();
};
