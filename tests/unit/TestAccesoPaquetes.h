#pragma once

#include <QObject>

// T009.1 (unit): resoluciones revelables del OperacionExecutor y la fachada
// AccesoPaquetesService con FakePackageStorage y la base en memoria: presente,
// ausente, paquete no Descargado, solicitud sin descargados o eliminada,
// validacion, base intacta y ejecucion fuera del hilo grafico.
class TestAccesoPaquetes : public QObject {
    Q_OBJECT

private slots:
    void archivoPresenteYAusente();
    void archivoNoAplica();
    void carpetaDeSolicitud();
    void raizDePaquetes();
    void errorDeValidacionNoExponeRuta();
    void soloLecturaYFueraDelHiloGrafico();
    void ejecutorDetenidoDevuelveError();
};
