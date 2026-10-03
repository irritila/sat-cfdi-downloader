#pragma once

#include <QObject>

class TestDemoPerfilesSatService : public QObject {
    Q_OBJECT

private slots:
    void listarActivosExcluyeInactivosYOrdenaPorRfc();
    void listarActivosVacio();
};
