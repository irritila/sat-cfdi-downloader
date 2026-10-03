#pragma once

#include <QObject>

// T005: migracion 002 y SqliteCredencialSatRepository contra SQLite real;
// CredencialesSatServicePersistido con SQLite real y FakeSecretStore.
class TestSqliteCredenciales : public QObject {
    Q_OBJECT

private slots:
    void migracion002SobreBase001YRepetida();
    void checksDeMetadata();
    void insertarObtenerReemplazarEliminar();
    void escriturasExigenTransaccionMetadataYPerfil();
    void listarReferenciasVigentesFailSafe();
    void referenciasPersistidasSinRutasNiSecretos();
    void servicioConSqliteRealImportaYReemplaza();
};
