#pragma once

#include <QObject>

class TestDominio : public QObject {
    Q_OBJECT

private slots:
    void uuidCanonicoAceptaSoloFormaCanonica_data();
    void uuidCanonicoAceptaSoloFormaCanonica();
    void idsGeneradosSonCanonicosYUnicos();
    void idNuloPorDefecto();
    void estadoResumenDerivaDeSatOLocal_data();
    void estadoResumenDerivaDeSatOLocal();
    void clavesEstablesDeEnums();
    void tipoDescargaDesdeClave();
};
