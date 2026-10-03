#pragma once

#include <QObject>

class TestResultado : public QObject {
    Q_OBJECT

private slots:
    void exitoContieneValor();
    void falloContieneError();
    void valorMovible();
    void mismoTipoEnValorYError();
};
