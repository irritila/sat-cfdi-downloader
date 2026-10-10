#pragma once

#include <QObject>

// SolicitudesServicePersistido y PerfilesSatServicePersistido sobre
// repositorios fake en memoria y un PersistenceDispatcher real.
class TestServiciosPersistidos : public QObject {
    Q_OBJECT

private slots:
    void crearInsertaCreadaConLogSaneado();
    void senalesTrasCommitYAntesDeCompletarFuture();
    void senalesDeEliminarYPerfilAntesDeCompletarFuture();
    void listarYObtenerMapeanFilas();
    void listarIncluyeNombreDelPerfil();
    void listarConteaPaquetesPorEstado();
    void detalleExponeReintentoPorPaquete();
    void obtenerInexistenteOEliminadaEsNoEncontrada();
    void matrizDuplicados_data();
    void matrizDuplicados();
    void perfilInvalidoOInactivo_data();
    void perfilInvalidoOInactivo();
    void filtroInvalidoNoAbreTransaccion();
    void violacionIndiceDedupEsBloqueado();
    void falloTrasInsertarHaceRollbackSinSenales_data();
    void falloTrasInsertarHaceRollbackSinSenales();
    void eliminarEsTransaccionalEIdempotente();
    void eliminarConFalloHaceRollbackSinSenales();
    void crearPerfilNormalizaYEmite();
    void crearPerfilDuplicadoEsRfcDuplicado();
    void crearPerfilInvalidoNoTocaPersistencia();
    void listarNoEliminadosYObtenerIncluyenInactivos();
    void actualizarNombreSoloCambiaNombre();
    void hiloGraficoNoSeBloquea();
};
