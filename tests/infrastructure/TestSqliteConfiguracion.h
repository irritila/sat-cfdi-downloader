#pragma once

#include <QObject>

// configuracion_app sobre SQLite real (T004): valores por defecto, escrituras
// por campo con actualizada_en, transaccion obligatoria, rollback, errores y
// ConfiguracionAppServicePersistido con PersistenceDispatcher real.
class TestSqliteConfiguracion : public QObject {
    Q_OBJECT

private slots:
    void valoresPorDefectoDeInstalacionNueva();
    void escrituraSinTransaccionEsTransaccion();
    void actualizacionesPorCampoConTimestamps();
    void rollbackDescartaCambios();
    void instanteInvalidoEsInterno();
    void sinFilaEsNoEncontrado();
    void servicioPersistidoSobreSqlite();
};
