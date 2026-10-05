#pragma once

#include <QObject>

// T009 (unit): OperacionesSatProductivo sobre FakeSatGateway (fixtures de
// T006), FakeAccesoCredencialSat y FakePackageStorage. Tabla D8 por operacion
// (parametrizada), sesion de token D6 con reloj inyectado, cancelacion,
// catalogo D10 y ausencia de secretos; integracion con OperacionExecutor
// (estado final aplicado por T007). Sin red real ni esperas de tiempo real.
class TestOperacionesSatProductivo : public QObject {
    Q_OBJECT

private slots:
    void mapeaSolicitudEmitidosYRecibidos();
    void tablaEnvio();
    void tablaVerificacion();
    void tablaDescarga();
    void sesionReutilizaYExpiraConReloj();
    void sesionSeInvalidaPorRechazoFallaYCambio();
    void tokenObtenidoDuranteInvalidacionNoSeConserva();
    void cancelacionAntesYDuranteLaRed();
    void cancelacionDuranteEscrituraSinFinal();
    void existeArchivoFinalYEstadoCredencial();
    void ultimoErrorSeDesglosa();
    void ejecutorAplicaEstadosDeLaTabla();
    void ejecutorPublicaTransicionesUnaVez();
    void faultHostilNoLlegaAPersistencia();
    void puertoCualquieraSeSaneaEnElEjecutor();
    void invalidacionConcurrenteConReutilizacion();
    void codigoNoReconocidoEsNoDocumentado();
};
