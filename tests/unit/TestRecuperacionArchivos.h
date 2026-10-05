#pragma once

#include <QObject>

// T008 corte B: recuperacion de archivos (D3, D9, D10) y consulta de
// existencia (D11) en OperacionExecutor con FakePackageStorage.
class TestRecuperacionArchivos : public QObject {
    Q_OBJECT

private slots:
    void temporalAsociadoADescargandoSeEliminaTrasLaRegla();
    void temporalNoAsociableSeEliminaConDiagnostico();
    void finalHuerfanoConSolicitudSeConservaYRegistraUnaVez();
    void finalDeUnPaqueteRegistradoNoEsHuerfano();
    void finalSinSolicitudSoloDiagnostico();
    void escaneoFallidoNoBorraYSigueLaRecuperacionSqlite();
    void existenciaNoCambiaElEstado();
    void logsSinBytesNiRutasAbsolutas();
};
