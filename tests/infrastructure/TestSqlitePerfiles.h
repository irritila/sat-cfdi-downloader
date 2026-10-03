#pragma once

#include <QObject>

// T005.1: PerfilSatRepository (listarVisibles, actualizarNombreVisible) y
// PerfilesSatServicePersistido contra SQLite real.
class TestSqlitePerfiles : public QObject {
    Q_OBJECT

private slots:
    void listarVisiblesIncluyeInactivosYExcluyeEliminados();
    void actualizarNombreNoTocaRfc();
    void actualizarNombreExigeTransaccionYRespetaCheck();
    void servicioCreaDuplicadoYActualiza();
};
