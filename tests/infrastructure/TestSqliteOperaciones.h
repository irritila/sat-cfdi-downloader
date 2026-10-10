#pragma once

#include <QObject>

// SqliteOperacionesSolicitudRepository (T007, corte B2) sobre SQLite real en
// QTemporaryDir: migracion 003 sobre datos de 001+002, selecciones acotadas y
// su plan de consulta, transiciones con revalidacion de eliminacion y estado,
// CHECKs tras cada transicion y escrituras concurrentes ejecutor/dispatcher.
class TestSqliteOperaciones : public QObject {
    Q_OBJECT

private slots:
    void migracion003SobreBase002ConDatos();
    void migracion004SobreBase003ConDatos();
    void intencionesPorPaquete();
    void escriturasExigenTransaccion();
    void perfilesConTrabajo();
    void verificacionesDebidasAcotadas();
    void intencionesAcotadas();
    void descargasAutomaticasAcotadas();
    void vencimientosEstimadosAcotados();
    void paquetesPorIdYReintentables();
    void interrumpidosYRacha();
    void planDeConsultaUsaIndices();
    void envioTransiciones();
    void envioNoAplicaPorEstadoOEliminacion();
    void verificacionRegistraPaquetesSinDuplicar();
    void verificacionVencidaVenceNoDescargados();
    void verificacionNoAplicaYRevierte();
    void fallaVerificacionRacha();
    void descargaReclamoYDesenlaces();
    void descargaNoAplica();
    void vencimientoEstimado();
    void intencionesLimpiezaCondicional();
    void concurrenciaEjecutorYDispatcher();
};
