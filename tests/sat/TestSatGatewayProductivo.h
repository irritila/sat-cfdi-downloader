#pragma once

#include <QObject>

// T009 (infrastructure): SatGatewayProductivo contra un servidor HTTP local
// (sin red real). Material de firma temporal generado por la prueba.
class TestSatGatewayProductivo : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void autenticaHeadersYToken();
    void operacionesConTokenWrapYSoapAction();
    void emitidosYRecibidosAtributosAdr0013();
    void respuestasExplicitasCreacionYVerificacion();
    void faultYHttpNo200();
    void fasesYDeadlinesPorOperacion();
    void httpIncompletoEsDespuesDeEnvio();
    void cancelacionAbortaLaPeticion();
    void descargaPorChunksAlReceptor();
    void descargaCanceladaSinArchivoFinal();
    void descargaSinPaqueteUtilizable();
    void preparacionSinTraficoDeRed();
    void hiloDelEjecutor();
    void diagnosticosSinSecretos();
    void faultYCuerposReflejadosNoSeExponen();
    void respuestasExplicitasSaneadas();
    void listaPermitidaDeCodigos();
    void endpointsSoloOficialesEnProduccion();
    void prefijoQNameHostilNoSeExpone();
};
