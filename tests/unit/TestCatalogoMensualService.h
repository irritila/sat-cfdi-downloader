#pragma once

#include <QObject>

// T023.2 (unit): CatalogoMensualCfdiService con repositorios en memoria, un
// indexador falso y el PersistenceDispatcher real: SQL fuera del hilo
// grafico, continuaciones en el hilo grafico, reloj capturado una vez,
// revalidacion de ZIP antes de releer, indexacion programada y errores.
class TestCatalogoMensualService : public QObject {
    Q_OBJECT

private slots:
    void consultarYLotesFueraDelHiloGrafico();
    void disponibilidadRevalidaYRelee();
    void disponibilidadSinRevalidarUsaIndexacionProgramada();
    void erroresSePropagan();
    void mesInicialPorOmisionSegunReloj();
};
