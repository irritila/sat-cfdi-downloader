#pragma once

#include <QObject>

// T008 (infrastructure): FilesystemPackageStorage sobre QTemporaryDir con
// PosixFileOps real y FileOps programable por operacion y por llamada.
class TestPackageStorageFs : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void guardadoExitosoPorChunks();
    void fallosAntesDePromover_data();
    void fallosAntesDePromover();
    void cancelacionEnChunkK();
    void fuenteFallidaLimpia();
    void permisoDenegado();
    void durabilidadYPromocion();
    void fsyncDirectorioTrasPromoverEsAdvertencia();
    void colisionConCentinela();
    void entradaInvalidaNoTocaFilesystem();
    void existenciaDeFinales();
    void escaneoArbolMixto();
    void eliminarSoloTemporalesPropios();
    void diagnosticosSinBytesNiRutasAbsolutas();
    void raizEfectiva();

private:
    bool m_carpetaUsuarioExistia = false;
};
