#pragma once

#include <QObject>

// Criterio 5 de T006: clasificacion AntesDeEnvio / DespuesDeEnvio /
// RespuestaExplicita contra un QTcpServer local (sin red externa ni sleeps:
// se espera a condiciones con QTest::qWaitFor y deadlines cortos del cliente).
class TestClienteHttpSat : public QObject {
    Q_OBJECT

private slots:
    void antesDeEnvioConexionRechazada();
    void antesDeEnvioDeadlineSinRequestSent();
    void despuesDeEnvioCorteSinReenvio();
    void despuesDeEnvioDeadline();
    void respuestaExplicitaConResultadoYHeaders();
    void respuestaExplicitaConFault500();
    void peticionesSeriales();
};
