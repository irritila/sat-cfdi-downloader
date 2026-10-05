#pragma once

#include <QObject>

// T008 (unit): derivacion pura de rutas y saneamiento (D1, D2, D5), gramatica
// de finales/temporales (D6, D9) y FakePackageStorage. Sin filesystem.
class TestAlmacenamientoPaquetes : public QObject {
    Q_OBJECT

private slots:
    void derivacionDeterministaYForma();
    void idValidoSinHash();
    void saneamientoNoEscapaYDistingue_data();
    void saneamientoNoEscapaYDistingue();
    void entradaInvalida_data();
    void entradaInvalida();
    void gramaticaFinalesYTemporales();
    void cancelacionDesdeSenal();
    void fakeGuardaColisionaYFalla();
    void fakeEscaneoYEliminarSoloTemporales();
};
