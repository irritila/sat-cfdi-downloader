#pragma once

#include <QObject>

class TestDemoSolicitudesService : public QObject {
    Q_OBJECT

private slots:
    void listarCubreTodasLasClavesDeEstadoResumen();
    void listarCubreConYSinEstadoSatYPaquetes();
    void idsDemoSonCanonicosYUnicos();
    void futuresYaCompletados();
    void obtenerPorIdDevuelveDetalle();
    void obtenerIdInexistenteDevuelveNoEncontrada();
    void construidoVacioNoTieneSolicitudes();
    void crearValidaAgregaAlInicioYEmiteSenal();
    void crearRechazaValidacion_data();
    void crearRechazaValidacion();
    void crearAceptaFechasIguales();
};
