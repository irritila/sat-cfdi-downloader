#pragma once

#include <QObject>

// Repositorios SQLite contra la base real migrada: lecturas/escrituras,
// restricciones traducidas a ErrorPersistencia, matriz de duplicados con
// fixtures y eliminacion logica atomica e idempotente.
class TestSqliteRepositorios : public QObject {
    Q_OBJECT

private slots:
    void perfilInsertarYConsultar();
    void perfilRfcVigenteDuplicadoEsUnicidad();
    void perfilCheckEsIntegridad();
    void solicitudCreadaConNulos();
    void solicitudListaOrdenadaYVisible();
    void solicitudDedupBloqueanteTraducido();
    void solicitudPerfilInexistenteEsIntegridadFk();
    void solicitudIdDuplicadoEsUnicidad();
    void restriccionesSinApiDeEscrituraTraducidas();
    void matrizDuplicados_data();
    void matrizDuplicados();
    void duplicadosPrecedenciaYReferencia();
    void paquetesYLogsPorSolicitud();
    void logRestriccionesTraducidas();
    void configuracionUnica();
    void eliminacionLogicaAtomicaEIdempotente();
    void eliminacionRevertidaNoDejaRastro();
    void filaIlegibleEsInterno();
    void integridadFinal();
};
