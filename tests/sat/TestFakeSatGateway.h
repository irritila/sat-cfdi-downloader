#pragma once

#include <QObject>

// T009 (unit): FakeSatGateway coincide con los fixtures de T006 parseados por
// el adaptador real, guion por operacion, token y cancelacion de bloqueos.
class TestFakeSatGateway : public QObject {
    Q_OBJECT

private slots:
    void fixturesCoincidenConElParser();
    void guionTokenYConteo();
    void bloqueoCancelableYLiberable();
    void descargaEntregaAlReceptor();
};
