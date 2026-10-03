#pragma once

#include <QObject>

// Driver QSQLITE, DM4, recurso embebido, DM2 y runner de migraciones (DM1)
// contra SQLite real en QTemporaryDir.
class TestSqliteMigraciones : public QObject {
    Q_OBJECT

private slots:
    void driverDisponibleYDm4();
    void recursoEmbebidoIgualAlFuente();
    void dividirSentenciasDm2();
    void dividirSentenciasRechazaSinTerminadorYBom();
    void migraBaseVacia();
    void segundaAperturaNoDuplica();
    void indicesParcialesYJson1();
    void migracionRotaHaceRollbackCompleto();
    void migracionRotaSobreBaseVaciaNoDejaSchemaMigrations();
    void versionFuturaSeRechazaSinModificarArchivo();
    void tablasSinSchemaMigrationsFalla();
    void listaInvalidaFalla();
    void integridadTrasMigrar();
};
