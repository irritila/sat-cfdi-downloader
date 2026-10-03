#pragma once

#include <QObject>

// ConfiguracionAppServicePersistido (T004) sobre FakeConfiguracion/
// FakeUnitOfWork y un PersistenceDispatcher real; y el contrato del
// FakeConfiguracionAppService reutilizable por app_core.
class TestConfiguracionAppService : public QObject {
    Q_OBJECT

private slots:
    void obtenerNoAbreTransaccionNiEmite();
    void escriturasConfirmanYEmitenAntesDeCompletar_data();
    void escriturasConfirmanYEmitenAntesDeCompletar();
    void falloEnEscrituraHaceRollbackSinSenal_data();
    void falloEnEscrituraHaceRollbackSinSenal();
    void escriturasSucesivasNoSeSobrescriben();
    void fakeServicioControlaFutures();
};
