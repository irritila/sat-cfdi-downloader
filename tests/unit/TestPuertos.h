#pragma once

#include <QObject>

class TestPuertos : public QObject {
    Q_OBJECT

private slots:
    void puertosTienenDestructorVirtualYNingunEstado();
};
